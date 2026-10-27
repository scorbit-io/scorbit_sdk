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
| `script` | The script that runs: a sidecar `.vbs` beside the table if VPX would load one, else the `CODE` record of `GameStg/GameData`. Then every script it loads from the table's own folder by a literal `GetTextFile("…")` or `LoadScript "…"` name, the names of scripts it loads from VPX's own folders, and the count of loads whose name is not a literal. |
| `items` | For every `GameStg/GameItemN`: its type, lower-cased `NAME`, timer enabled and interval, and the rule-bearing fields for its type (`itemAllowList`). Sorted by type and name. |
| `physics` | The table physics fields of `GameStg/GameData` (`tableAllowList`) and the physics of each material, sorted by name. |

The top-level digest is SHA-256 over `"vts1\0"` and the three section digests, in that order.

**Script canonical form.** Comments (`'` and `Rem`) are dropped; ` _` continuations are joined;
`:` and line ends become one statement separator; identifiers, keywords and numbers are lower-cased;
string and date literals are kept byte-exact. A script that is not valid UTF-8 is read as
ISO-8859-1, as VPX does. Whitespace, comments and identifier case do not change the digest.

**Fields** are hashed as stored, in allow-list order, each marked present or absent. Positions,
images, materials' visual properties, editor state and table info are not hashed.

**Procedures.** Each `Sub`, `Function` and `Property` block, keyed by lower-cased name
(`class.name` inside a `Class`, `file.vbs:name` in a helper script, `""` for code outside any
procedure), with the first 8 bytes of the digest over its canonical form. It lets a server say which
procedures differ without receiving the script.

## Vectors

`vectors.json` holds inputs and expected outputs for an unmodified table, a cosmetic edit (same
signature) and rule edits to the script, a timer, a flipper, the slope and a helper script.
`tools/make_vectors.cpp` regenerates it; a change to the output is a change to the algorithm and
needs a new version.

## Versioning

Any change to the canonical form, a section's inputs or an allow-list is a new version. A client
should report every version it can compute, so registrations made under an older version stay
comparable.
