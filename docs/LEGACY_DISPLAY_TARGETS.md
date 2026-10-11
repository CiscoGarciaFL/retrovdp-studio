# Display adapter and character-display targets

The final six target profiles use the same project, conversion, preview,
validation, persistence, CLI, and export contracts as the VDP, PPU, and object
processor profiles. Their Screen Image modes produce these hardware-native
regions:

| Target | Modes | Native memory output |
| --- | --- | --- |
| Amstrad CPC | Mode 0, 1, 2 | 16 KiB framebuffer with eight-line interleave, CPC bit placement, 17 ink/border entries, mode state |
| IBM CGA | 40×25 and 80×25 text; 320×200 and 640×200 graphics | character/attribute cells or a 16 KiB even/odd scanline framebuffer, palette state, registers |
| IBM EGA | 80×25 text; 320×200 and 640×350 graphics | character/attribute cells or four consecutive native bitplanes, 16 palette registers, display state |
| Apple IIe | text, lo-res, hi-res, double hi-res | sparse 1 KiB text/lo-res page, 8 KiB hi-res page, or aux/main 16 KiB double-hi-res pages |
| Timex Sinclair 1000 / ZX81 | 32×24 text, 64×48 semigraphics | full 793-byte display file with leading and per-line `0x76` HALT markers |
| TRS-80 Model I/III | 64×16 text, 128×48 semigraphics | 1 KiB display RAM; semigraphics uses bit 7 plus native upper-left-to-lower-right 2×3 cell bits |

Raw export names framebuffer-like data `.VRAM`, Apple pages `.PAGE`, ZX81
streams `.DFILE`, palette state `.PAL`, and mode metadata `.REG`. Character
workspaces follow each profile's cell geometry; none of these targets claims a
native hardware-sprite workspace, so software composition remains outside the
Sprite Editor contract.
