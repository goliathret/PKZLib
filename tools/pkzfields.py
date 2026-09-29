#!/usr/bin/env python3
"""Schema-driven field dumper for raw Goliath .pak files.
"""

from __future__ import annotations

import argparse
import json
import mmap
import os
import struct
import sys
import xml.etree.ElementTree as ET
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

    def matches(self, chunk: Chunk) -> bool:
        a = self.element.attrib
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
        self.sources: list[str] = []

    def load(self, path: str) -> None:
        root = ET.parse(path).getroot()
        if root.tag != "pkz-fields":
            raise ValueError(f"{path}: root element must be <pkz-fields>")
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


class Decoder:
    def __init__(self, package: Package, schemas: SchemaSet, max_records: int):
        self.package = package
        self.schemas = schemas
        self.data = package.data
        self.endian = package.endian
        self.max_records = max_records

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


def print_human(items: list[dict[str, Any]]) -> None:
    for item in items:
        print(f"{item['path']} @0x{item['offset']:X} id={item['idHex']} "
              f"v{item['version']} payload=0x{item['payloadSize']:X}")
        for field in item["fields"]:
            print(f"  +0x{field.get('offset', 0):04X} {field['name']}: {display_value(field)}")
        for group in item["records"]:
            if "error" in group:
                print(f"  {group['name']}: ERROR: {group['error']}")
                continue
            suffix = "" if group["shown"] == group["count"] else f" (showing {group['shown']})"
            print(f"  {group['name']}: {group['count']} record(s), stride 0x{group['stride']:X}{suffix}")
            for record in group["records"]:
                values = ", ".join(f"{f['name']}={display_value(f)}" for f in record["fields"])
                print(f"    [{record['index']:>4}] +0x{record['offset']:X}: {values}")


def item_errors(item: dict[str, Any]) -> list[str]:
    errors = []
    for value in item["fields"]:
        if "error" in value:
            errors.append(f"{item['path']}/{value['name']}: {value['error']}")
    for group in item["records"]:
        if "error" in group:
            errors.append(f"{item['path']}/{group['name']}: {group['error']}")
            continue
        for record in group["records"]:
            for value in record["fields"]:
                if "error" in value:
                    errors.append(
                        f"{item['path']}/{group['name']}[{record['index']}]/"
                        f"{value['name']}: {value['error']}"
                    )
    return errors


def default_schemas(game: str | None) -> list[str]:
    root = Path(__file__).resolve().parent.parent / "schemas"
    paths = [str(root / "goliath.xml")]
    if game == "eot":
        paths.append(str(root / "spider_man_edge_of_time.xml"))
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
