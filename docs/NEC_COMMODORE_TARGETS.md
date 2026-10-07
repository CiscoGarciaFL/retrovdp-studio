# HuC6270, VIC-II, and VIC target contract

Status: implemented target and byte-layout contract.

This document records the native output boundary for the `huc6270`, `vic-ii`,
and `vic` target profiles. It complements the workspace dispositions in
[TARGET_IMPLEMENTATION_GUIDE.md](TARGET_IMPLEMENTATION_GUIDE.md).

## Registered modes

| Target | Stable mode ID | Logical geometry | Native payload |
| --- | --- | --- | --- |
| HuC6270 | `huc6270-background-256x224` | 256×224 | 4-plane tiles, 32×32 BAT, VCE palette, VDC/VCE state |
| HuC6270 | `huc6270-background-320x224` | 320×224 | 4-plane tiles, 64×32 BAT, VCE palette, VDC/VCE state |
| VIC-II | `vic-ii-hires-character` | 320×200 | character, screen, color RAM, registers |
| VIC-II | `vic-ii-multicolor-character` | 160×200 | multicolor character, screen, color RAM, registers |
| VIC-II | `vic-ii-hires-bitmap` | 320×200 | bitmap, screen, registers |
| VIC-II | `vic-ii-multicolor-bitmap` | 160×200 | multicolor bitmap, screen, color RAM, registers |
| VIC | `vic-hires-character` | 176×184 | character, 22×23 screen/color memory, registers |
| VIC | `vic-multicolor-character` | 88×184 | multicolor character, 22×23 screen/color memory, registers |

## HuC6270 memory rules

- BAT words are little-endian. Bits 0–11 select the tile and bits 12–15 select
  one of sixteen background palettes.
- The 256-pixel mode emits a 2,048-byte BAT and 63,488-byte tile region. The
  320-pixel mode emits a 4,096-byte BAT and 61,440-byte tile region.
- Each tile occupies 32 bytes. Plane 0 and plane 1 bytes are interleaved in the
  first sixteen bytes; plane 2 and plane 3 are interleaved in the next sixteen.
- `.PAL` is the complete 1,024-byte VCE color table. Background palettes occupy
  its first half; unused entries remain deterministic zeroes.
- `.REG` is 42 bytes: twenty little-endian VDC register words followed by one
  HuC6260/VCE clock-control word.

## Commodore memory rules

- Character modes emit a fixed 2,048-byte character region. Screen and color
  memory contain one byte per visible cell: 1,000 bytes for VIC-II and 506
  bytes for VIC.
- VIC-II bitmap modes emit 8,000 bitmap bytes and 1,000 screen bytes.
  Multicolor bitmap additionally emits 1,000 color-RAM bytes.
- VIC-II `.REG` contains the 47 memory-mapped VIC-II registers used by the
  static mode. VIC `.REG` contains the sixteen VIC registers.
- Color-RAM bytes carry only hardware-significant low-nibble color data and
  the VIC-II multicolor-character enable bit where required.
- Preview palettes are nominal references, not claims that every chip revision
  or PAL/NTSC encoder produces identical RGB values.

## Export names

RAW output uses native sidecar names:

- HuC6270: `.TILES`, `.BAT`, `.PAL`, `.REG`
- VIC/VIC-II character: `.CHR`, `.SCR`, `.COL`, `.REG`
- VIC-II bitmap: `.BITMAP`, `.SCR`, optional `.COL`, `.REG`

RLE, TIFILES, and V9T9 continue to wrap the same registered table payloads.
PNG is generated from the decoded target preview.

## Primary references

- [HuC6270 VDC documentation maintained by the MiSTer TurboGrafx-16 core](https://github.com/MiSTer-devel/TurboGrafx16_MiSTer/blob/master/docs/vdcdox.txt)
- [Commodore 64 Programmer's Reference Guide, programming graphics](https://www.commodore.ca/manuals/c64_programmers_reference/c64-programmers-reference_guide-03-programming_graphics.pdf)
- [MOS 6560/6561 VIC data sheet](https://files.tassiebob.com/Retro%20Computing/Commodore/VIC-20/MOS%206560%20%26%206561%20VIC%20data%20sheet.pdf)

These references define byte layout, geometry, and register meaning. Nominal
RGB preview values remain implementation references because analog color is
revision- and standard-dependent.
