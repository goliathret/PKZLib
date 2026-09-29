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

    def test_eot_primitive_record_and_bitfields(self):
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
            schemas.load(str(root / "spider_man_edge_of_time.xml"))
            with open(path.name, "rb") as source:
                data = mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ)
                package = F.Package(data)
                chunk = list(package.walk())[1]
                schema, layout = schemas.layout(chunk)
                decoded = F.Decoder(package, schemas, 32).chunk(chunk, schema, layout)
                fields = {item["name"]: item["value"]
                          for item in decoded["records"][0]["records"][0]["fields"]}
                self.assertEqual(fields["materialIndex"], 181)
                self.assertEqual(fields["vertexStride"], 24)
                self.assertEqual(fields["vertexCount"], 12)
                self.assertEqual(fields["vertexBufferIndex"], 3)
                self.assertEqual(F.item_errors(decoded), [])
                data.close()
        finally:
            os.unlink(path.name)


if __name__ == "__main__":
    unittest.main()
