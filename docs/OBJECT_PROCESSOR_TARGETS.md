# Object processor targets

Status: implemented conversion, editor, persistence, and native export
contract for Atari Lynx and SNK Neo Geo.

## Object-first workspace behavior

Object processors are not presented as background-tile VDPs. When either
profile is active, Screen Image is labeled **Object Scene** and Sprite Editor is
labeled **Object Editor**. Each object entry owns its dimensions, indexed pixel
data, palette selection, transform attributes, visibility, and scene position.
Projects persist those values with the stable target IDs `atari-lynx` and
`neo-geo`.

The ordinary converted preview remains useful as a scene-level compiler input.
The editor export path instead compiles authored object entries and their
placement into native object metadata and pixel payloads together.

## Atari Lynx Suzy / Mikey

`atari-lynx-object-scene` prepares a 160×102 source and quantizes it to one
programmable 16-entry RGB444 palette. The conversion result contains:

- packed 4bpp, totally-literal line data with per-line offsets and a terminator;
- a Suzy Sprite Control Block with 1.0 horizontal and vertical scale;
- the 32-byte Mikey palette layout; and
- compact conversion metadata for geometry and framebuffer size.

The Object Editor supports independent 8, 16, 32, and 64 pixel width and height
choices plus per-object horizontal and vertical scaling. Native editor export
writes `.SPR` line data, a linked `.SCB` table,
`.PAL`, and `.REG`. SCB next/data fields are offsets relative to their exported
tables so a runtime can relocate the two blocks to its chosen RAM addresses.

## SNK Neo Geo

`neo-geo-fix-layer` and `neo-geo-chained-sprites` prepare a 320×224 source.
Both select sixteen working 4bpp palette banks with entry zero reserved for
transparency and emit the complete 256-bank, big-endian palette image.

FIX conversion emits 8×8 4-plane patterns plus the 40×28 visible map in the
hardware's 32-row column layout. Chained-object conversion emits 16×16 4-plane
tiles in vertical strips and the SCB1–SCB4 VRAM regions used for tile,
attribute, zoom, position, and strip-height data.

The Character Editor authors the 8×8 FIX set. The Object Editor authors
16-pixel-wide strips from 16 to 512 pixels high (one to 32 tiles), with shrink,
optional chaining to the previous strip, a palette bank, and flip metadata per
object. Native editor export writes
`.SPR`, `.VRAM`, `.FIX`, `.FIXMAP`, `.PAL`, and `.REG` files.

## RAW conversion suffixes

Scene conversion uses target-aware RAW suffixes: `.SCB` for Lynx object
attributes, `.VRAM` for Neo Geo object attributes, `.FIX`/`.FIXMAP` for the
Neo Geo fixed layer, `.SPR` for object pixels, and `.PAL`/`.REG` for palette
and metadata regions.
