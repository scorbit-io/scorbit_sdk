# table_signature

A signature for authored (original, non-ROM) Visual Pinball X tables. An unmodified copy of a table
gives the same signature as the original; a copy whose rules changed does not. It plays the role a
firmware checksum plays for a ROM. It is computed on the player's machine and reported, so it tells
an honest client's state to a server; it is not proof against a client that lies.

`include/table_signature/table_signature.h` is header-only C++17 with no dependencies, MIT
licensed, so it can be vendored into both proprietary and GPL code. It does no file I/O: the caller
reads the `.vpx` (an OLE compound file) and passes in what this file hashes.

## Version `vts1`

Three SHA-256 section digests, a top-level digest over them, and a per-procedure list.

| Section | Input |
|---|---|
| `script` | The script that runs: a sidecar `.vbs` beside the table if VPX would load one, else the `CODE` record of `GameStg/GameData`. Then every script it loads from the table's own folder by a literal `GetTextFile("…")` or `LoadScript "…"` name, and the names of scripts it loads from VPX's own folders. A script that loads any helper by a name that is not a literal has no signature (see Errors). |
| `items` | For every `GameStg/GameItemN`: its type, lower-cased `NAME`, timer enabled and interval, part group (`GRUP`), and the rule-bearing fields for its type (`itemAllowList`): its physics, and the placement and size of its collision shape. Then, for walls, ramps, rubbers and triggers, each drag point's outline fields in file order (`dragPointAllowList`), and for a primitive its mesh's vertex positions and triangle indices. Sorted by type and name. |
| `physics` | The table physics fields of `GameStg/GameData` (`tableAllowList`) and the physics of each material, sorted by name. |

The top-level digest is SHA-256 over `"vts1\0"` and the three section digests, in that order.

**Script canonical form.** Comments (`'` and `Rem`) are dropped; ` _` continuations are joined;
`:` and line ends become one statement separator, except a `:` inside a single-line `If` (code after
`Then` on the same line), which keeps its own separator because it keeps the next statement
conditional; identifiers, keywords and numbers are lower-cased (ASCII letters only, so a case
change in a non-ASCII identifier reads as a change); `&H` and `&O` literals take only their own
digits; string and date literals are kept byte-exact. A script that is not well-formed UTF-8
(overlong forms, surrogates and values above U+10FFFF included) is read as ISO-8859-1, as VPX does.
Whitespace, comments and ASCII identifier case do not change the digest.

**Fields** are hashed as stored, in allow-list order, each marked present or absent. A field's value
is the BIFF record's data after its 4-byte tag, exactly as VPX wrote it: it excludes the record's
own length and the tag, and includes anything VPX writes inside the data (a string field's length
prefix, for example). Two are normalised: `VPOS` and `VSIZ` keep only x, y and z (the first 12
bytes), and `SURF` and `GRUP`, which name another item, are ASCII-lower-cased after their length
prefix, since VPX looks the name up ignoring case. Images, materials' visual properties, editor state
and table info are not hashed, and neither is the position of an item with no collision shape.

**Drag points** are the child records between an item's `DPNT` record and its matching `ENDB`, one
`DragPoint` per point, in file order: the order decides which segments exist. A reader must track
that nesting, since a point's `VCEN` and `ENDB` reuse tags of the item itself.

**A primitive's mesh** (when `U3DM` is set) is passed decompressed: the caller inflates `M3CX`
(or reads `M3DX`) to `Vertex3D_NoTex2` records and passes only x, y and z of each, and inflates
`M3CI` (or reads `M3DI`) to its indices (16-bit, or 32-bit above 65535 vertices). The compressed
bytes are not hashed, because recompression changes them, and normals and texture coordinates are
left out because they do not collide. Floats and indices are hashed little-endian.

**Procedures.** Each `Sub`, `Function` and `Property` block, keyed by lower-cased name
(`class.name` inside a `Class`, `file.vbs:name` in a helper script, `""` for code outside any
procedure), with the first 8 bytes of the digest over its canonical form. It lets a server say which
procedures differ without receiving the script.

## Errors

Every function is pure and deterministic. None throws except `std::bad_alloc`, none aborts, and
malformed or truncated input (an unterminated string, invalid UTF-8, a missing field) produces a
digest rather than a failure.

The one input with no signature is a script that loads a helper by a name that is not a literal
(`GetTextFile(name)`, `LoadScript name`): an edit to that helper could not change the digest.
`compute` then returns a `Signature` whose `unavailable` is `kUnnamedHelper`, with no digests or
procedures; report that reason, never a partial signature.

## Vectors

`vectors.json` holds inputs and expected outputs for an unmodified table, a cosmetic edit (same
signature) and rule edits to the script, a timer, a flipper, the slope, a helper script, a
single-line `If`, a bumper's position and a wall's outline.
`tools/make_vectors.cpp` regenerates it; a change to the output is a change to the algorithm and
needs a new version.

## Versioning

Any change to the canonical form, a section's inputs or an allow-list is a new version. A client
should report every version it can compute, so registrations made under an older version stay
comparable.
