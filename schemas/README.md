# PKZ field schemas

`tools/pkzfields.py` keeps binary chunk traversal in PKZLib and moves field
layouts into composable XML files. `goliath.xml` contains layouts believed to
be engine-wide. A game schema adds only layouts verified for that title; load
one with `--game` or any number of custom files with `--schema`.

```sh
python tools/pkzfields.py game.pak --game eot --chunk GenSub_ResourceHeader
python tools/pkzfields.py level.pak --game eot --chunk RenderOctree_Prims --max-records 4
python tools/pkzfields.py game.pak --game eot --json > fields.json
```

The input must be a raw chunk tree. Use `pkztool unpack in.pkz out.pak` before
examining a compressed BUCompress container.

## Schema structure

```xml
<pkz-fields name="Example Game">
  <enum name="kind">
    <value key="0" name="None"/>
    <value key="1" name="Mesh"/>
  </enum>

  <!-- A name-only entry improves paths but does not decode a payload. -->
  <chunk id="0x123" name="Container"/>

  <chunk id="0x456" name="ExampleRecord">
    <layout version="2" size-min="16" parent="0x123">
      <field name="flags" offset="0" type="u32" display="hex"/>
      <field name="kind" offset="4" type="u32" enum="kind"/>
      <field name="count" offset="8" type="u32"/>
      <record name="items" offset="12" stride="8" count-field="count">
        <field name="position" offset="0" type="vec2f"/>
      </record>
    </layout>
  </chunk>
</pkz-fields>
```

Layouts may select `version`, `version-min`, `version-max`, `size`,
`size-min`, `size-max`, and `parent`. The most specific matching layout wins.
Later schema files can add more-specific layouts and rename chunk IDs.

Fields support `u8/i8/u16/i16/u32/i32/u64/i64/f32/f64`, `vec2f/vec3f/vec4f`,
fixed-length `ascii`, `utf8`, `bytes`, and NUL-terminated `cstring`. Numeric
fields may specify `mask`, `shift`, `display="hex"`, and `enum`. A fixed field
array uses `count`; a field array sized by an earlier scalar uses
`count-field`.

Records require a byte `stride` and use a fixed `count`, an earlier
`count-field`, or `count="remaining"`. `--max-records` limits display without
changing the decoded record count; zero prints all records.

Use `--strict` in automation to return a failure when a schema reads outside a
payload or otherwise cannot decode a selected field.

Unknown values should be named by offset (`unknown3C`, for example) until an
engine consumer or cross-package correlation gives them a defensible meaning.
That convention keeps speculation out of the shared schema.
