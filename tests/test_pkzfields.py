import importlib.util
import mmap
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parent.parent / "tools" / "pkzfields.py"
SPEC = importlib.util.spec_from_file_location("pkzfields", MODULE_PATH)
F = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = F
SPEC.loader.exec_module(F)


def leaf(chunk_id, version, payload):
    return struct.pack(">IHHI", chunk_id, version, 0, len(payload)) + payload


def container(chunk_id, version, children):
    payload = b"".join(children)
    return struct.pack(">IHHI", chunk_id, version, 1, len(payload)) + payload


class FieldDumpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.NamedTemporaryFile(delete=False)
        header = struct.pack(">IHH", 0x7EE, 2, 0) + b"Main\0".ljust(128, b"\0") + bytes(128)
        self.temp.write(container(1, 1, [leaf(0x11, 3, header)]))
        self.temp.close()

    def tearDown(self):
        os.unlink(self.temp.name)

    def test_core_schema_decodes_package_header(self):
        schemas = F.SchemaSet()
        schemas.load(str(Path(__file__).resolve().parent.parent / "schemas" / "goliath.xml"))
        with open(self.temp.name, "rb") as source:
            data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
            package = F.Package(data)
            chunks = list(package.walk())
            schema, layout = schemas.layout(chunks[1])
            self.assertIsNotNone(schema)
            self.assertIsNotNone(layout)
            decoded = F.Decoder(package, schemas, 32).chunk(chunks[1], schema, layout)
            values = {item["name"]: item["value"] for item in decoded["fields"]}
            self.assertEqual(values["packageId"], 0x7EE)
            self.assertEqual(values["name"], "Main")
            data.close()

    def test_eot_primitive_record_and_packed_counts(self):
        record = struct.pack(
            ">6f12I",
            1.0, 2.0, 3.0, 0.5, 0.75, 1.0,
            181, 19, (24 << 24) | 12, 0x233, 0xAA60, 89523,
            1, 0xAABBCCDD, 3, 0, 0xAABBCCDD, 0xAABBCCDD,
        )
        path = tempfile.NamedTemporaryFile(delete=False)
        path.write(container(1, 1, [leaf(0xCC, 7, record)]))
        path.close()
        try:
            schemas = F.SchemaSet()
            root = Path(__file__).resolve().parent.parent / "schemas"
            schemas.load(str(root / "goliath.xml"))
            schemas.load(str(root / "EOT-360.xml"))
            with open(path.name, "rb") as source:
                data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
                package = F.Package(data)
                chunk = list(package.walk())[1]
                schema, layout = schemas.layout(chunk)
                decoded = F.Decoder(package, schemas, 32).chunk(chunk, schema, layout)
                fields = {item["name"]: item for item in decoded["records"][0]["records"][0]["fields"]}
                self.assertEqual(fields["materialIndex"]["value"], 181)
                self.assertEqual(fields["vertexStride"]["value"], 24)
                self.assertEqual(fields["vertexCount"]["value"], 12)
                self.assertEqual(fields["materialIndex2"]["value"], 3)
                self.assertEqual(fields["vertexFormat"]["display"], "0x233")
                self.assertTrue(fields["vertexFormat"]["label"].startswith("XYZ | NORMAL | DIFFUSE"))
                self.assertEqual(F.item_errors(decoded), [])
                data.close()
        finally:
            os.unlink(path.name)

    def test_eot_morph_reads_its_own_geometry(self):
        def resource_header(crc):
            return leaf(0x138E, 5, struct.pack(">I", crc).ljust(88, b"\0"))

        def geometry(crc, morph_targets):
            info = bytearray(64)
            struct.pack_into(">I", info, 28, morph_targets)
            return container(0x138D, 1, [resource_header(crc), container(0x321, 3, [leaf(0x322, 7, bytes(info))])])

        block = struct.pack(">II3fI", 0, 1, 0.5, 0.25, 0.125, 0xFFFFFFFF)
        morph = struct.pack(">II", 8, 0xFFFFFFFF) + block
        post_load = container(0x26, 0, [resource_header(0xA), container(0x325, 3, [leaf(0x32E, 4, morph)])])
        no_subtitles = container(0x400, 1, [])
        path = tempfile.NamedTemporaryFile(delete=False)
        path.write(container(1, 1, [geometry(0xA, 2), geometry(0xB, 5), post_load, no_subtitles]))
        path.close()
        try:
            schemas = F.SchemaSet()
            for schema_path in F.default_schemas("eot"):
                schemas.load(schema_path)
            with open(path.name, "rb") as source:
                data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
                package = F.Package(data)
                decoder = F.Decoder(package, schemas, 0)
                decoded = {}
                for chunk in package.walk():
                    schema, layout = schemas.layout(chunk)
                    if layout is not None:
                        decoded[chunk.chunk_id] = decoder.chunk(chunk, schema, layout)
                self.assertEqual(decoded[0x32E]["fields"][0]["value"], [8, 0xFFFFFFFF])
                self.assertEqual(F.item_errors(decoded[0x32E]), [])
                self.assertNotIn(0x400, decoded)
                data.close()
        finally:
            os.unlink(path.name)

    def test_goliath_schema_expressions(self):
        schema = tempfile.NamedTemporaryFile("w", suffix=".xml", delete=False)
        schema.write("""<goliathChunks endian="big">
  <flagSets><flagSet name="F"><bit mask="0x1" name="A"/><bit mask="0x4" name="C"/></flagSet></flagSets>
  <structs>
    <struct name="Pair" size="4"><field offset="0" name="a" type="u16"/><field offset="2" name="b" type="u16"/></struct>
    <struct name="Blob"><field offset="0" name="n" type="u8"/><field offset="next" name="data" type="bytes" size="@n"/></struct>
  </structs>
  <chunks>
    <chunk id="0x20" name="Info" version="1"><field offset="0" name="total" type="u32"/></chunk>
    <chunk id="0x21" name="Data" version="2,3">
      <field offset="0" name="flags" type="u32" flags="F"/>
      <field offset="next" name="pairCount" type="u16"/>
      <field offset="next" name="pairs" struct="Pair" count="@pairCount"/>
      <field offset="next" name="sum" type="u16" count="sum:pairs.a"/>
      <field offset="next" name="extra" type="u8" if="flags&amp;0x4"/>
      <field offset="next" name="skipped" type="u8" if="flags==2"/>
      <field offset="next" name="blobs" struct="Blob" count="until:n=0"/>
      <field offset="next" name="tail" type="u8" count="@0x20.total - 1"/>
      <field offset="@0x20.total" name="third" type="u8" if="0x20.total==3"/>
    </chunk>
  </chunks>
</goliathChunks>""")
        schema.close()
        payload = (struct.pack(">IH", 0x5, 2) + struct.pack(">4H", 1, 10, 2, 20) + struct.pack(">3H", 7, 8, 9)
                   + b"\x63" + b"\x02ab" + b"\x00" + b"\x01\x02")
        path = tempfile.NamedTemporaryFile(delete=False)
        path.write(container(1, 1, [leaf(0x20, 1, struct.pack(">I", 3)), leaf(0x21, 3, payload)]))
        path.close()
        try:
            schemas = F.SchemaSet()
            schemas.load(schema.name)
            with open(path.name, "rb") as source:
                data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
                package = F.Package(data)
                decoder = F.Decoder(package, schemas, 0)
                decoded = [decoder.chunk(chunk, *schemas.layout(chunk)) for chunk in list(package.walk())[1:]][1]
                fields = {item["name"]: item for item in decoded["fields"]}
                groups = {group["name"]: group for group in decoded["records"]}
                self.assertEqual(F.item_errors(decoded), [])
                self.assertEqual(fields["flags"]["label"], "A | C")
                self.assertEqual([[f["value"] for f in r["fields"]] for r in groups["pairs"]["records"]], [[1, 10], [2, 20]])
                self.assertEqual(fields["sum"]["value"], [7, 8, 9])
                self.assertEqual(fields["extra"]["value"], 0x63)
                self.assertNotIn("skipped", fields)
                self.assertEqual(groups["blobs"]["count"], 2)
                self.assertEqual(groups["blobs"]["records"][0]["fields"][1]["value"], "6162")
                self.assertEqual(fields["tail"]["value"], [1, 2])
                self.assertEqual(fields["third"]["value"], 0x05)
                data.close()
        finally:
            os.unlink(path.name)
            os.unlink(schema.name)


if __name__ == "__main__":
    unittest.main()
