# Nintendo PPU target contract

Status: implemented static-asset target contract.

RetroVDP Studio implements separate `game-boy-ppu`, `game-boy-color-ppu`, and
`super-nes-ppu` profiles. They share planar codecs where byte-compatible, but
do not share ambiguous palette, bank, map-attribute, or OAM behavior.

## Screen Image

| Target mode | Geometry | Native output |
| --- | --- | --- |
| Game Boy background | 160×144 | 2bpp tiles, 32×32 map, BGP/OBJ palette registers, LCD state |
| Game Boy Color background | 160×144 | two-bank 2bpp tiles, 32×32 map and attribute map, BG/OBJ RGB555 palettes, LCD state |
| Super NES Mode 0 BG1 | 256×224 | 2bpp tiles, 32×32 16-bit map, 256-entry CGRAM image, PPU state |
| Super NES Mode 1 BG1 | 256×224 | 4bpp tiles, 32×32 16-bit map, 256-entry CGRAM image, PPU state |
| Super NES Mode 3 BG1 | 256×224 | 8bpp tiles, 32×32 16-bit map, 256-entry CGRAM image, PPU state |

Screen Image compiles one opaque background layer. It does not infer Game Boy
window placement, additional Super NES layers, color math, windows, HDMA, or
mid-frame state from a flat raster. Previews are reconstructed from the
quantized palette and encoded tile interpretation. RAW export writes `.TILES`,
`.MAP`, optional `.ATTR`, `.PAL`/`.CGRAM`, and `.REG` files.

## Character Patterns

- Game Boy exposes the complete 384-tile VRAM storage model and one 32×32 map.
  An active LCD addressing mode can reference 256 of those tiles. Native
  export selects signed or unsigned addressing and rejects a map that mixes
  the mutually exclusive tile ranges 0–127 and 256–383.
- Game Boy Color exposes 768 stored tiles across two VRAM banks, eight BG
  palettes, bank/palette/flip/priority map attributes, and one 32×32 map. The
  same signed/unsigned addressing selection applies to both banks.
- Super NES exposes 1,024 authored 8×8 patterns, eight palette banks, 32×32
  map authoring, and mode-selected 2bpp, 4bpp, or 8bpp planar serialization.
  The fixed Mode 3 export layout reserves the first 8 KiB of VRAM and therefore
  serializes/address-checks patterns 0–895; the remaining authored slots stay
  available for projects that choose another runtime VRAM layout.

Native editor export writes `.CHR`, `.MAP`, optional `.ATTR`, palette, sprite,
OAM, and register files. Map collisions and cross-set references are rejected.

## Sprites

- Game Boy and Game Boy Color expose 40 OAM entries, global 8×8/8×16 sizing,
  2bpp patterns, transparent index zero, position, flip, palette, and priority.
- Super NES exposes 128 OAM entries, 4bpp patterns, eight OBJ palettes,
  transparent index zero, position, flip, priority, and 544-byte low/high OAM.
  Export verifies that visible object sizes fit one hardware small/large size
  pair and that pattern data fits the 512-tile OAM address space.

The editor reports the Game Boy ten-object-per-line limit and the Super NES
32-object-per-line limit. Runtime sprite evaluation, collision behavior,
HDMA, raster effects, and full PPU emulation are outside this static-asset
contract.

## Evidence

The byte layouts follow the Nintendo Game Boy Programming Manual and Nintendo
Super NES development-manual register, VRAM, CGRAM, and OAM formats. Golden
structural tests cover every Screen Image mode and native export shape.
