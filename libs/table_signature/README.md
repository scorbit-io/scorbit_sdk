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
| `items` | For every `GameStg/GameItemN`: its type, lower-cased `NAME`, timer enabled and interval, part group (`GRUP`), and the rule-bearing fields for its type (`itemAllowList`): its physics, and the placement and size of its collision shape. Then, for walls, ramps, rubbers and triggers, each drag point's outline fields in file order (`dragPointAllowList`), and for a primitive its mesh's vertex positions and triangle indices. Sorted by type and name. Then every `GameStg/CollectionN`: its lower-cased name, its members' lower-cased names in file order (a script iterates them, and a member's index reaches the collection's events) and `EVNT`/`SSNG` (`collectionAllowList`), sorted by name. |
| `physics` | The table physics fields of `GameStg/GameData` (`tableAllowList`) and the physics of each material, sorted by name. |

The top-level digest is SHA-256 over `"vts1\0"` and the three section digests, in that order.

**Script canonical form.** Comments (`'` and `Rem`) are dropped; ` _` continuations are joined;
`:` and line ends become one statement separator, except a `:` inside a single-line `If` (code after
`Then` on the same line), which keeps its own separator because it keeps the next statement
conditional; identifiers, keywords and numbers are lower-cased (ASCII letters only, so a case
change in a non-ASCII identifier reads as a change); `&H` and `&O` literals take only their own
digits; string and date literals are kept byte-exact. A leading UTF-8 byte order mark is dropped,
and a script that is not well-formed UTF-8 (overlong forms, surrogates and values above U+10FFFF
included) is read as Windows-1252, as VPX's `string_from_utf8_or_cp1252` does.
Whitespace, comments and ASCII identifier case do not change the digest.

**Fields** are hashed as stored, in allow-list order, each marked present or absent; a tag that
appears more than once takes its last value, since VPX applies records in order. A field's value
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
`compute` finds such loads itself, in the script and in every helper passed in, so a caller's
`dynamicLoads` count can only add to them. A string literal that mentions `LoadScript` or `GetTextFile` may be
executed (`ExecuteGlobal "LoadScript ""x.vbs"""`), so its contents are scanned as code, three
levels deep: a literal name inside is a helper like any other, and anything else refuses the
signature. Code assembled at run time from pieces that never spell either name is not followed;
like the rest of the signature, this tells an honest client's state and is not proof against one
that hides a load. A caller passes every helper a script reaches by literal
name, including those named by other helpers.
`compute` then returns a `Signature` whose `unavailable` is `kUnnamedHelper`, with no digests or
procedures; report that reason, never a partial signature.

## Vectors

`vectors.json` holds inputs and expected outputs for an unmodified table, a cosmetic edit (same
signature) and rule edits to the script, a timer, a flipper, the slope, a helper script, a
single-line `If`, a bumper's position, a wall's outline and a collection's members.
`tools/make_vectors.cpp` regenerates it; a change to the output is a change to the algorithm and
needs a new version.

## Reading a `.vpx`

`include/table_signature/vpx_reader.h` (header-only C++17, standard library and the OS file API
only, same MIT license) reads a table into `Inputs` and calls `compute`. `inflate.h` beside it is
the zlib decoder it uses for primitive meshes, altered from Mark Adler's puff.c and carrying its
zlib-style notice. `<filesystem>` needs `-lstdc++fs` on GCC 8 and `-lc++fs` on older libc++, and
macOS 10.15 or later.

`parse` takes the file's bytes and does no I/O. `signFile` takes a UTF-8 path and opens it read-only
without locking it (`FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE` on Windows, so VPX can
still save over it). It reads only the compound file's header, FAT, directory and the streams hashed
here, never images or sounds. Both are `noexcept` and return a `Code`; its values are fixed and only
appended to:
`ok` 0, `table_unreadable_permission` 1, `table_unreadable` 2, `legacy_protected` 3,
`unnamed_helper` 4, `missing_helper` 5, `signature_too_large` 6, `malformed` 7, `cancelled` 8.

Every size and count has a cap (`Limits`). `maxTotalRead` covers what is read and what is kept
from it: each parsed record, string and material, and each mesh both inflated and as kept. Scripts
are capped in bytes and tokens (`maxScriptBytes`, `maxScriptTokens`), since each is tokenized
twice. A cancel flag is checked between streams, between scripts and while folders are listed.
Nothing is logged, and the result depends on nothing but the files read.

Scripts are resolved as VPX does, from folders passed in (`Paths`). Pass VPX's install folder
(`vpxDirectory`) and preferences folder (`prefDirectory`) when they are known: a script that loads a
VPX-shipped helper such as `core.vbs` is `missing_helper` when neither is searched. A name is searched in VPX's
`SearchScript` order, and the first match wins:
1. the table's folder, its `user/` and `scripts/`;
2. VPX's install folder, its `user/`, `scripts/` and `tables/`;
3. VPX's preferences folder, its `user/` and `scripts/`.

Each part of a name is matched ignoring ASCII case. Only files and folders match, never a FIFO or
device, and never a file that is itself a link; every script found is opened refusing a link.
Under the table's folder, whose scripts are read and hashed, a linked folder or junction never
matches either (unlike VPX), so a link shipped with a table cannot make the reader hash a file
outside it. In VPX's install and preferences folders a linked folder is followed, since installs
often link `scripts/` or `user/`. A middle
folder swapped for a link between listing it and opening a file inside is not detected; only the
file itself is opened refusing a link. At most `maxListedEntries` folder entries are listed.

An entry the reader refuses still takes its place in VPX's order: if the first match for the
sidecar or a helper is a link, FIFO or device that VPX would load, the table is
`table_unreadable`. The reader never falls through to a later file or to the embedded script,
which would sign something VPX does not run.

Cloud-synced files:
- **Windows:** only links and junctions (name-surrogate reparse tags) are refused. A cloud
  placeholder, such as OneDrive Files On-Demand, is opened normally so its contents are fetched,
  after checking on the same file that it is not a link. If its contents can't be fetched (offline),
  the read fails and the table is `table_unreadable`.
- **macOS:** an evicted iCloud ("dataless") file is a regular file to the search. Reading it
  either downloads it first (the system default, which may block), or fails when the process's
  I/O policy forbids materializing dataless files or the download fails. A failed read makes the
  table `table_unreadable`.

So on both platforms a cloud file is either read in full or makes the table unreadable, never
silently skipped.

A sidecar `<table>.vbs`, found by that search or
else as `<folder>.vbs` in the table's folder, replaces the `CODE` record. Otherwise a `CODE` record
under legacy protection (`SECB` with script editing disabled) is `legacy_protected`. The `CODE`
script ends at its first NUL, as VPX reads it.

Each literal helper name, breadth-first and once, goes through the same search. A match under the
table's folder has its content hashed and its own loads followed; a match in VPX's folders has its
name hashed. A relative name (`lib\x.vbs`) is followed and hashed as `lib/x.vbs`. Some names are not
followed, unlike VPX: a name with a root, a drive or a `..` part, or more than eight parts. A name found nowhere makes
`GetTextFile` fail in VPX, a run-time error, so the table has no signature: `missing_helper`, with the
names in `missingHelpers`. A name that is not followed counts as missing.

How the reader takes the file apart:
- Items are the streams `GameStg/GameItem0` to `GameItem<SEDT-1>`, as VPX loads them, with `SEDT`
  from `GameData`; a missing one is skipped, as VPX skips it, and other streams are not read. Each is a 4-byte item type, then
  records to `ENDB`.
  A textbox's or decal's `FONT` record has a length that covers only its tag; the font descriptor
  after it (11 bytes, then the name) is that field's value.
- Collections are `GameStg/Collection0` to `Collection<SCOL-1>`, a missing one skipped as VPX
  does: `NAME` and repeated `ITEM`, both UTF-16 strings. Names are
  converted to UTF-8.
- Physics materials come from the `PHMA` record of `GameStg/GameData`, an array of 48-byte entries:
  a 32-byte name, then elasticity, elasticity falloff, friction and scatter angle. Tables saved by
  VPX 10.8 or later (version 1080 and up) also write a `MATR` sub-object per material. When `MATR`
  is present at that version it replaces the `PHMA` materials. VPX writes the same physics values
  to both, so the reader uses `PHMA`. If the two ever differ, read `MATR` and make it a new version.
- A table whose `GameStg/Version` is 30 or below writes records without lengths and is
  `malformed`.

`tools/make_vpx_fixtures.cpp` writes synthetic tables under `fixtures/vpx/` and their expected
signatures to `vpx_fixtures.json`, computed from the model each file was written from.
`tools/vpx_writer.h` holds that model, a minimal compound-file writer, and the malformed corpus the
tests run the reader against.

## Versioning

Any change to the canonical form, a section's inputs or an allow-list is a new version. A client
should report every version it can compute, so registrations made under an older version stay
comparable.
