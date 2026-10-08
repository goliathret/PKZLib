#!/usr/bin/env python3
"""Schema-driven field dumper for raw Goliath .pak files.
"""

from __future__ import annotations

import argparse
import ast
import json
import mmap
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
from collections import ChainMap
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


MAGIC_BUCOMPRESS = b"\xBA\xBE\xB1\xB0"
ID_MASK = 0x00FFFFFF
WIDE_LENGTH_FLAG = 0x80000000


def integer(text: str) -> int:
    return int(text, 0)


@dataclass
class Chunk:
    chunk_id: int
    version: int
    has_children: bool
    offset: int
    payload_offset: int
    payload_size: int
    header_size: int
    parent: "Chunk | None" = None
    children: list["Chunk"] = field(default_factory=list)

    @property
    def end(self) -> int:
        return self.payload_offset + self.payload_size


class Package:
    def __init__(self, data: mmap.mmap, little_endian: bool = False):
        self.data = data
        self.little_endian = little_endian
        self.endian = "<" if little_endian else ">"
        self.roots: list[Chunk] = []
        self.tail_offset = len(data)
        self._parse_roots()

    def _header(self, offset: int) -> tuple[int, int, int, int, int]:
        if offset + 12 > len(self.data):
            raise ValueError(f"short chunk header at 0x{offset:X}")
        raw_id, version, has_children = struct.unpack_from(self.endian + "IHH", self.data, offset)
        wide = (raw_id & 0xFF000000) == WIDE_LENGTH_FLAG
        header_size = 16 if wide else 12
        if offset + header_size > len(self.data):
            raise ValueError(f"short wide chunk header at 0x{offset:X}")
        payload_size = struct.unpack_from(self.endian + ("Q" if wide else "I"), self.data, offset + 8)[0]
        return raw_id & ID_MASK, version, has_children, header_size, payload_size

    def _parse_chunk(self, offset: int, limit: int, parent: Chunk | None) -> Chunk:
        chunk_id, version, has_children, header_size, payload_size = self._header(offset)
        payload_offset = offset + header_size
        end = payload_offset + payload_size
        if end > limit:
            raise ValueError(
                f"chunk 0x{chunk_id:X} at 0x{offset:X} ends at 0x{end:X}, beyond 0x{limit:X}"
            )
        chunk = Chunk(chunk_id, version, bool(has_children), offset, payload_offset,
                      payload_size, header_size, parent)
        if chunk.has_children:
            cursor = payload_offset
            while cursor < end:
                child = self._parse_chunk(cursor, end, chunk)
                chunk.children.append(child)
                cursor = child.end
            if cursor != end:
                raise ValueError(f"children of chunk 0x{chunk_id:X} do not fill its payload")
        return chunk

    def _parse_roots(self) -> None:
        cursor = 0
        size = len(self.data)
        while cursor < size:
            remaining = size - cursor
            if remaining < 12 or not any(self.data[cursor:cursor + min(12, remaining)]):
                self.tail_offset = cursor
                break
            root = self._parse_chunk(cursor, size, None)
            self.roots.append(root)
            cursor = root.end

    def walk(self) -> Iterable[Chunk]:
        def descend(chunk: Chunk) -> Iterable[Chunk]:
            yield chunk
            for child in chunk.children:
                yield from descend(child)
        for root in self.roots:
            yield from descend(root)


@dataclass
class Layout:
    element: ET.Element
    source: str
    dialect: str = "pkz-fields"

    def matches(self, chunk: Chunk) -> bool:
        a = self.element.attrib
        if self.dialect == "goliath":
            return "version" not in a or chunk.version in {integer(v) for v in a["version"].split(",")}
        checks = (
            ("version", chunk.version, lambda x, y: x == y),
            ("version-min", chunk.version, lambda x, y: x >= y),
            ("version-max", chunk.version, lambda x, y: x <= y),
            ("size", chunk.payload_size, lambda x, y: x == y),
            ("size-min", chunk.payload_size, lambda x, y: x >= y),
            ("size-max", chunk.payload_size, lambda x, y: x <= y),
        )
        for name, actual, compare in checks:
            if name in a and not compare(actual, integer(a[name])):
                return False
        if "parent" in a:
            if chunk.parent is None or chunk.parent.chunk_id != integer(a["parent"]):
                return False
        return True

    @property
    def specificity(self) -> int:
        if self.dialect == "goliath":
            return 1 if "version" in self.element.attrib else 0
        return sum(1 for key in ("version", "version-min", "version-max", "size",
                                 "size-min", "size-max", "parent")
                   if key in self.element.attrib)


@dataclass
class ChunkSchema:
    chunk_id: int
    name: str
    layouts: list[Layout] = field(default_factory=list)


class SchemaSet:
    def __init__(self):
        self.chunks: dict[int, ChunkSchema] = {}
        self.enums: dict[str, dict[int, str]] = {}
        self.flag_sets: dict[str, dict[int, str]] = {}
        self.structs: dict[str, ET.Element] = {}
        self.sources: list[str] = []

    def load(self, path: str) -> None:
        root = ET.parse(path).getroot()
        if root.tag == "goliathChunks":
            self.sources.append(path)
            self._load_goliath(root, path)
            return
        if root.tag != "pkz-fields":
            raise ValueError(f"{path}: root element must be <pkz-fields> or <goliathChunks>")
        self.sources.append(path)
        for enum in root.findall("enum"):
            name = enum.attrib["name"]
            values = self.enums.setdefault(name, {})
            for value in enum.findall("value"):
                values[integer(value.attrib["key"])] = value.attrib["name"]
        for element in root.findall("chunk"):
            chunk_id = integer(element.attrib["id"])
            schema = self.chunks.setdefault(
                chunk_id, ChunkSchema(chunk_id, element.attrib.get("name", f"chunk_{chunk_id:X}"))
            )
            schema.name = element.attrib.get("name", schema.name)
            layouts = element.findall("layout")
            for layout in layouts:
                schema.layouts.append(Layout(layout, path))

    def _load_goliath(self, root: ET.Element, path: str) -> None:
        for enum in root.iterfind("enums/enum"):
            values = self.enums.setdefault(enum.attrib["name"], {})
            for value in enum.iterfind("value"):
                values[integer(value.attrib["value"])] = value.attrib["name"]
        for flag_set in root.iterfind("flagSets/flagSet"):
            bits = self.flag_sets.setdefault(flag_set.attrib["name"], {})
            for bit in flag_set.iterfind("bit"):
                bits[integer(bit.attrib["mask"])] = bit.attrib["name"]
            for value in flag_set.iterfind("value"):
                bits[integer(value.attrib["value"])] = value.attrib["name"]
        for struct_node in root.iterfind("structs/struct"):
            self.structs[struct_node.attrib["name"]] = struct_node
        for element in root.iterfind("chunks/chunk"):
            chunk_id = integer(element.attrib["id"])
            schema = self.chunks.setdefault(
                chunk_id, ChunkSchema(chunk_id, element.attrib.get("name", f"chunk_{chunk_id:X}"))
            )
            schema.name = element.attrib.get("name", schema.name)
            if element.find("field") is not None:
                schema.layouts.append(Layout(element, path, "goliath"))

    def layout(self, chunk: Chunk) -> tuple[ChunkSchema | None, Layout | None]:
        schema = self.chunks.get(chunk.chunk_id)
        if schema is None:
            return None, None
        matches = [layout for layout in schema.layouts if layout.matches(chunk)]
        if not matches:
            return schema, None
        matches.sort(key=lambda item: item.specificity, reverse=True)
        return schema, matches[0]

    def resolve_chunk_filter(self, value: str) -> int:
        try:
            return integer(value)
        except ValueError:
            matches = [chunk_id for chunk_id, schema in self.chunks.items()
                       if schema.name.lower() == value.lower()]
            if len(matches) != 1:
                raise ValueError(f"unknown or ambiguous chunk name: {value}")
            return matches[0]


SCALARS = {
    "u8": "B", "i8": "b", "u16": "H", "i16": "h",
    "u32": "I", "i32": "i", "u64": "Q", "i64": "q",
    "f32": "f", "f64": "d",
}
VECTORS = {"vec2f": ("f", 2), "vec3f": ("f", 3), "vec4f": ("f", 4)}
GOLIATH_SCALARS = {
    "u8": "B", "s8": "b", "u16": "H", "s16": "h", "u32": "I", "s32": "i",
    "u64": "Q", "s64": "q", "f16": "e", "f32": "f", "f64": "d",
}
REFERENCE = re.compile(r"@(0x[0-9A-Fa-f]+\.\w+|\w+)")
DECODE_ERRORS = (KeyError, ValueError, ZeroDivisionError, struct.error)


def arithmetic(text: str) -> int:
    def walk(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return walk(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = walk(node.operand)
            return -value if isinstance(node.op, ast.USub) else value
        if isinstance(node, ast.BinOp):
            left, right = walk(node.left), walk(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, (ast.Div, ast.FloorDiv)):
                return left // right
            if isinstance(node.op, ast.Mod):
                return left % right
        raise ValueError(f"unsupported expression {text!r}")
    try:
        return walk(ast.parse(text.strip(), mode="eval"))
    except SyntaxError as error:
        raise ValueError(f"bad expression {text!r}") from error


class GoliathDecoder:
    def __init__(self, package: Package, schemas: SchemaSet, max_records: int):
        self.schemas = schemas
        self.data = package.data
        self.endian = package.endian
        self.max_records = max_records
        # Fields of the latest decoded chunk per ID, for @0xID.field.
        self.latest: dict[int, dict[str, Any]] = {}

    def remember(self, chunk: Chunk, context: dict[str, Any]) -> None:
        self.latest[chunk.chunk_id] = context

    def decode(self, chunk: Chunk, layout: Layout,
               context: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
        context["version"] = chunk.version
        fields, groups, _ = self._block(list(layout.element), chunk.payload_offset, chunk.end, context)
        return fields, groups

    def _reference(self, name: str, context: Any) -> int:
        if "." in name:
            chunk_text, _, key = name.partition(".")
            values = self.latest.get(integer(chunk_text))
            if values is None or key not in values:
                raise ValueError(f"@{name} needs chunk {chunk_text} decoded earlier")
            value = values[key]
        elif name in context:
            value = context[name]
        else:
            raise ValueError(f"@{name} names no earlier field")
        if not isinstance(value, int):
            raise ValueError(f"@{name} is not an integer")
        return value

    def _number(self, text: str, context: Any) -> int:
        text = text.strip()
        if text.startswith("sum:"):
            group, _, key = text[4:].partition(".")
            records = context.get(group)
            if records is None:
                raise ValueError(f"{text} needs every record of {group} (use --max-records 0)")
            return sum(int(record.get(key, 0)) for record in records)
        return arithmetic(REFERENCE.sub(lambda m: str(self._reference(m.group(1), context)), text))

    def _condition(self, text: str, context: Any) -> bool:
        def value(name: str) -> int:
            name = name.strip().lstrip("@")
            item = self._reference(name, context) if "." in name else context.get(name, 0)
            return item if isinstance(item, int) else 0
        if "&" in text:
            left, right = text.split("&", 1)
            return (value(left) & integer(right.strip())) != 0
        for op in ("!=", "=="):
            if op in text:
                left, right = text.split(op, 1)
                return (value(left) == integer(right.strip())) == (op == "==")
        return value(text) != 0

    def _flags_label(self, set_name: str, value: int) -> str | None:
        bits = self.schemas.flag_sets.get(set_name)
        if not bits:
            return None
        if value and value in bits:
            return bits[value]
        names, left = [], value
        for mask, name in sorted(bits.items()):
            if mask and value & mask == mask:
                names.append(name)
                left &= ~mask
        if names and left:
            names.append(f"0x{left:X}")
        return " | ".join(names) or None

    def _block(self, nodes: list[ET.Element], base: int, limit: int,
               context: Any) -> tuple[list[dict[str, Any]], list[dict[str, Any]], int]:
        fields: list[dict[str, Any]] = []
        groups: list[dict[str, Any]] = []
        cursor = base
        for node in nodes:
            if node.tag != "field":
                continue
            a = node.attrib
            try:
                if "if" in a and not self._condition(a["if"], context):
                    continue
                offset = a.get("offset", "next")
                start = cursor if offset == "next" else base + self._number(offset, context)
                if start > limit:
                    raise ValueError(f"starts at +0x{start - base:X}, past the payload end +0x{limit - base:X}")
                if "struct" in a:
                    group, cursor = self._struct(node, base, start, limit, context)
                    groups.append(group)
                else:
                    item, cursor = self._value(node, base, start, limit, context)
                    fields.append(item)
            except DECODE_ERRORS as error:
                (groups if "struct" in a else fields).append({"name": a.get("name", "?"), "error": str(error)})
        return fields, groups, cursor

    def _value(self, node: ET.Element, base: int, start: int, limit: int,
               context: Any) -> tuple[dict[str, Any], int]:
        a = node.attrib
        kind = a.get("type", "")
        match = re.fullmatch(r"(\w+)(?:\[(\d+)\])?", kind)
        if not match:
            raise ValueError(f"unsupported field type {kind!r}")
        base_type, width = match.group(1), int(match.group(2) or 1)
        if base_type == "cstring":
            end = self.data.find(b"\0", start, limit)
            if end < 0:
                raise ValueError(f"unterminated cstring at +0x{start - base:X}")
            value: Any = bytes(self.data[start:end]).decode("utf-8", "replace")
            size = end - start + 1
        elif base_type == "bytes":
            size = limit - start if a.get("size", "rest") == "rest" else self._number(a["size"], context)
            if size < 0 or start + size > limit:
                raise ValueError(f"need {size} bytes at +0x{start - base:X}")
            value = bytes(self.data[start:start + size]).hex()
        elif base_type == "char" or base_type in GOLIATH_SCALARS:
            elem = width if base_type == "char" else struct.calcsize(GOLIATH_SCALARS[base_type]) * width
            count_text = a.get("count")
            count = (1 if count_text is None else (limit - start) // elem if count_text == "rest"
                     else self._number(count_text, context))
            size = count * elem
            if count < 0 or start + size > limit:
                raise ValueError(f"need {size} bytes at +0x{start - base:X}")
            if base_type == "char":
                items: list[Any] = [bytes(self.data[start + i * elem:start + (i + 1) * elem]).split(b"\0", 1)[0]
                                    .decode("ascii", "replace") for i in range(count)]
            else:
                flat = struct.unpack_from(f"{self.endian}{count * width}{GOLIATH_SCALARS[base_type]}", self.data, start)
                items = list(flat) if width == 1 else [list(flat[i:i + width]) for i in range(0, len(flat), width)]
            value = items[0] if count == 1 else items
        else:
            raise ValueError(f"unsupported field type {kind!r}")

        result: dict[str, Any] = {"name": a.get("name", "?"), "offset": start - base, "type": kind,
                                  "size": size, "value": value}
        if isinstance(value, int):
            if a.get("enum") in self.schemas.enums and value in self.schemas.enums[a["enum"]]:
                result["label"] = self.schemas.enums[a["enum"]][value]
            if "flags" in a:
                result["display"] = f"0x{value:X}"
                label = self._flags_label(a["flags"], value)
                if label:
                    result["label"] = label
        if "pointsTo" in a:
            result["pointsTo"] = a["pointsTo"]
        context[result["name"]] = value[0] if isinstance(value, list) and value and isinstance(value[0], int) else value
        return result, start + size

    def _struct(self, node: ET.Element, base: int, start: int, limit: int,
                context: Any) -> tuple[dict[str, Any], int]:
        a = node.attrib
        name = a.get("name", "?")
        layout = self.schemas.structs.get(a["struct"])
        if layout is None:
            raise ValueError(f"unknown struct {a['struct']}")
        stride = integer(layout.attrib["size"]) if "size" in layout.attrib else None
        count_text = a.get("count")
        count: int | None = 1
        until: tuple[str, int] | None = None
        end = limit
        if count_text and count_text.startswith("until:"):
            key, _, terminator = count_text[6:].partition("=")
            count, until = None, (key.strip(), integer(terminator.strip()))
        elif count_text == "rest":
            count = (limit - start) // stride if stride else None
        elif count_text:
            count = self._number(count_text, context)
        elif "size" in a:
            end = start + self._number(a["size"], context)
            if end > limit:
                raise ValueError(f"{a['size']} bytes at +0x{start - base:X} run past the payload")
            count = (end - start) // stride if stride else None
        fixed = stride is not None and count is not None
        if fixed and start + count * stride > end:
            raise ValueError(f"{count} x {a['struct']} at +0x{start - base:X} end past the payload")

        records: list[dict[str, Any]] = []
        values: list[dict[str, Any]] | None = []
        cursor, index = start, 0
        while (count is None or index < count) and cursor < end:
            if fixed and self.max_records and index == self.max_records:
                cursor, values = start + count * stride, None
                break
            record_context = ChainMap({}, context)
            fields, groups, record_end = self._block(list(layout), cursor, cursor + stride if stride else end,
                                                     record_context)
            if not self.max_records or index < self.max_records:
                record = {"index": index, "offset": cursor - base, "fields": fields}
                if groups:
                    record["records"] = groups
                records.append(record)
            values.append(record_context.maps[0])
            following = cursor + stride if stride else record_end
            if following <= cursor:
                raise ValueError(f"{a['struct']} record {index} at +0x{cursor - base:X} does not advance")
            cursor, index = following, index + 1
            if until and values[-1].get(until[0]) == until[1]:
                break
        context[name] = values
        return {"name": name, "offset": start - base, "stride": stride or 0, "struct": a["struct"],
                "count": count if fixed else index, "shown": len(records), "records": records}, cursor


class Decoder:
    def __init__(self, package: Package, schemas: SchemaSet, max_records: int):
        self.package = package
        self.schemas = schemas
        self.data = package.data
        self.endian = package.endian
        self.max_records = max_records
        self.goliath = GoliathDecoder(package, schemas, max_records)

    def _read_one(self, field_node: ET.Element, start: int, limit: int) -> tuple[Any, int]:
        kind = field_node.attrib["type"]
        if kind in SCALARS:
            fmt = self.endian + SCALARS[kind]
            size = struct.calcsize(fmt)
            if start + size > limit:
                raise ValueError(f"need {size} bytes at +0x{start:X}")
            return struct.unpack_from(fmt, self.data, start)[0], size
        if kind in VECTORS:
            code, count = VECTORS[kind]
            fmt = self.endian + str(count) + code
            size = struct.calcsize(fmt)
            if start + size > limit:
                raise ValueError(f"need {size} bytes at +0x{start:X}")
            return list(struct.unpack_from(fmt, self.data, start)), size
        if kind in ("ascii", "utf8", "bytes"):
            length = integer(field_node.attrib["length"])
            if start + length > limit:
                raise ValueError(f"need {length} bytes at +0x{start:X}")
            raw = bytes(self.data[start:start + length])
            if kind == "bytes":
                return raw.hex(), length
            raw = raw.split(b"\0", 1)[0]
            return raw.decode("ascii" if kind == "ascii" else "utf-8", "replace"), length
        if kind == "cstring":
            end = self.data.find(b"\0", start, limit)
            if end < 0:
                raise ValueError(f"unterminated cstring at +0x{start:X}")
            return bytes(self.data[start:end]).decode("utf-8", "replace"), end - start + 1
        raise ValueError(f"unsupported field type {kind!r}")

    def _decorate(self, node: ET.Element, value: Any) -> dict[str, Any]:
        result: dict[str, Any] = {"value": value}
        if isinstance(value, int):
            if "mask" in node.attrib:
                value = value & integer(node.attrib["mask"])
            if "shift" in node.attrib:
                value >>= integer(node.attrib["shift"])
            result["value"] = value
            enum_name = node.attrib.get("enum")
            if enum_name and value in self.schemas.enums.get(enum_name, {}):
                result["label"] = self.schemas.enums[enum_name][value]
            if node.attrib.get("display") == "hex":
                result["display"] = f"0x{value:X}"
        return result

    def _field(self, node: ET.Element, base: int, limit: int,
               context: dict[str, Any]) -> dict[str, Any]:
        relative = integer(node.attrib.get("offset", "0"))
        start = base + relative
        count_text = node.attrib.get("count")
        if "count-field" in node.attrib:
            count = int(context[node.attrib["count-field"]])
        else:
            count = integer(count_text) if count_text else 1
        values = []
        cursor = start
        total_size = 0
        for _ in range(count):
            value, size = self._read_one(node, cursor, limit)
            values.append(value)
            cursor += size
            total_size += size
        value: Any = values[0] if count == 1 else values
        decorated = self._decorate(node, value)
        result = {
            "name": node.attrib["name"],
            "offset": relative,
            "type": node.attrib["type"],
            "size": total_size,
            **decorated,
        }
        context[result["name"]] = result["value"]
        return result

    def _record(self, node: ET.Element, base: int, limit: int,
                context: dict[str, Any]) -> dict[str, Any]:
        relative = integer(node.attrib.get("offset", "0"))
        stride = integer(node.attrib["stride"])
        if node.attrib.get("count") == "remaining":
            count = max(0, (limit - (base + relative)) // stride)
        elif "count-field" in node.attrib:
            count = int(context[node.attrib["count-field"]])
        else:
            count = integer(node.attrib["count"])
        table_end = base + relative + count * stride
        if table_end > limit:
            raise ValueError(
                f"{count} records at +0x{relative:X} with stride 0x{stride:X} "
                f"end at +0x{table_end - base:X}, beyond payload +0x{limit - base:X}"
            )
        shown = count if self.max_records == 0 else min(count, self.max_records)
        records = []
        for index in range(shown):
            record_base = base + relative + index * stride
            record_context: dict[str, Any] = {}
            fields = []
            for field_node in node.findall("field"):
                try:
                    fields.append(self._field(field_node, record_base,
                                              min(record_base + stride, limit), record_context))
                except (KeyError, ValueError) as error:
                    fields.append({"name": field_node.attrib.get("name", "?"), "error": str(error)})
            records.append({"index": index, "offset": relative + index * stride, "fields": fields})
        return {
            "name": node.attrib["name"], "offset": relative, "stride": stride,
            "count": count, "shown": shown, "records": records,
        }

    def chunk(self, chunk: Chunk, schema: ChunkSchema, layout: Layout) -> dict[str, Any]:
        context: dict[str, Any] = {}
        fields = []
        records = []
        if layout.dialect == "goliath":
            fields, records = self.goliath.decode(chunk, layout, context)
        else:
            for node in layout.element:
                if node.tag == "field":
                    try:
                        fields.append(self._field(node, chunk.payload_offset, chunk.end, context))
                    except (KeyError, ValueError) as error:
                        fields.append({"name": node.attrib.get("name", "?"), "error": str(error)})
                elif node.tag == "record":
                    try:
                        records.append(self._record(node, chunk.payload_offset, chunk.end, context))
                    except (KeyError, ValueError) as error:
                        records.append({"name": node.attrib.get("name", "?"), "error": str(error)})
        self.goliath.remember(chunk, context)
        return {
            "id": chunk.chunk_id,
            "idHex": f"0x{chunk.chunk_id:X}",
            "name": schema.name,
            "version": chunk.version,
            "offset": chunk.offset,
            "payloadOffset": chunk.payload_offset,
            "payloadSize": chunk.payload_size,
            "schema": layout.source,
            "path": chunk_path(chunk, self.schemas),
            "fields": fields,
            "records": records,
        }


def chunk_path(chunk: Chunk, schemas: SchemaSet) -> str:
    parts = []
    cursor: Chunk | None = chunk
    while cursor is not None:
        schema = schemas.chunks.get(cursor.chunk_id)
        parts.append(schema.name if schema else f"0x{cursor.chunk_id:X}")
        cursor = cursor.parent
    return "/" + "/".join(reversed(parts))


def display_value(field: dict[str, Any]) -> str:
    if "error" in field:
        return "ERROR: " + field["error"]
    text = field.get("display", repr(field.get("value")))
    if "label" in field:
        text += " (" + field["label"] + ")"
    return text


def print_group(group: dict[str, Any], indent: str) -> None:
    if "error" in group:
        print(f"{indent}{group['name']}: ERROR: {group['error']}")
        return
    suffix = "" if group["shown"] == group["count"] else f" (showing {group['shown']})"
    layout = f"stride 0x{group['stride']:X}" if group["stride"] else "variable size"
    print(f"{indent}{group['name']}: {group['count']} record(s), {layout}{suffix}")
    for record in group["records"]:
        values = ", ".join(f"{f['name']}={display_value(f)}" for f in record["fields"])
        print(f"{indent}  [{record['index']:>4}] +0x{record['offset']:X}: {values}")
        for nested in record.get("records", []):
            print_group(nested, indent + "    ")


def print_human(items: list[dict[str, Any]]) -> None:
    for item in items:
        print(f"{item['path']} @0x{item['offset']:X} id={item['idHex']} "
              f"v{item['version']} payload=0x{item['payloadSize']:X}")
        for field in item["fields"]:
            print(f"  +0x{field.get('offset', 0):04X} {field['name']}: {display_value(field)}")
        for group in item["records"]:
            print_group(group, "  ")


def group_errors(prefix: str, group: dict[str, Any]) -> Iterable[str]:
    if "error" in group:
        yield f"{prefix}/{group['name']}: {group['error']}"
        return
    for record in group["records"]:
        path = f"{prefix}/{group['name']}[{record['index']}]"
        for value in record["fields"]:
            if "error" in value:
                yield f"{path}/{value['name']}: {value['error']}"
        for nested in record.get("records", []):
            yield from group_errors(path, nested)


def item_errors(item: dict[str, Any]) -> list[str]:
    errors = [f"{item['path']}/{value['name']}: {value['error']}" for value in item["fields"] if "error" in value]
    for group in item["records"]:
        errors.extend(group_errors(item["path"], group))
    return errors


def default_schemas(game: str | None) -> list[str]:
    root = Path(__file__).resolve().parent.parent / "schemas"
    paths = [str(root / "goliath.xml")]
    if game == "eot":
        paths.append(str(root / "EOT-360.xml"))
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", help="raw .pak (use pkztool unpack on compressed .pkz first)")
    parser.add_argument("--game", choices=["eot"], help="load the named game schema after goliath.xml")
    parser.add_argument("--schema", action="append", default=[], help="additional XML schema (repeatable)")
    parser.add_argument("--chunk", action="append", default=[], help="only this chunk ID or schema name")
    parser.add_argument("--little-endian", action="store_true", help="read little-endian chunk headers and fields")
    parser.add_argument("--all-chunks", action="store_true", help="also list chunks without a matching layout")
    parser.add_argument("--max-records", type=int, default=32,
                        help="records shown per repeated table; 0 means unlimited (default: 32)")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of the human-readable dump")
    parser.add_argument("--strict", action="store_true", help="fail if any selected field cannot be decoded")
    args = parser.parse_args()

    if args.max_records < 0:
        parser.error("--max-records cannot be negative")
    schemas = SchemaSet()
    for path in default_schemas(args.game) + args.schema:
        schemas.load(path)
    try:
        wanted = {schemas.resolve_chunk_filter(value) for value in args.chunk}
    except ValueError as error:
        parser.error(str(error))

    with open(args.package, "rb") as source:
        data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            if data[:4] == MAGIC_BUCOMPRESS:
                parser.error("compressed PKZ input; run `pkztool unpack <in.pkz> <out.pak>` first")
            package = Package(data, args.little_endian)
            decoder = Decoder(package, schemas, args.max_records)
            items = []
            for chunk in package.walk():
                if wanted and chunk.chunk_id not in wanted:
                    continue
                schema, layout = schemas.layout(chunk)
                if schema is not None and layout is not None:
                    items.append(decoder.chunk(chunk, schema, layout))
                elif args.all_chunks:
                    items.append({
                        "id": chunk.chunk_id, "idHex": f"0x{chunk.chunk_id:X}",
                        "name": schema.name if schema else None, "version": chunk.version,
                        "offset": chunk.offset, "payloadOffset": chunk.payload_offset,
                        "payloadSize": chunk.payload_size, "schema": None,
                        "path": chunk_path(chunk, schemas), "fields": [], "records": [],
                    })
            if args.json:
                json.dump({
                    "package": os.path.abspath(args.package),
                    "endian": "little" if args.little_endian else "big",
                    "schemas": schemas.sources,
                    "tailOffset": package.tail_offset,
                    "chunks": items,
                }, sys.stdout, indent=2)
                print()
            else:
                print_human(items)
            errors = [error for item in items for error in item_errors(item)]
            if args.strict and errors:
                for error in errors:
                    print("pkzfields:", error, file=sys.stderr)
                return 1
        finally:
            data.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
