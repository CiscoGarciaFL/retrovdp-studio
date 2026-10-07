# Target implementation guide

Status: authoritative target-completeness checklist.

This guide defines what RetroVDP Studio must decide and document before a new
video target is presented as supported. It complements the registry contract
in [TARGET_PROFILES.md](TARGET_PROFILES.md), the hardware catalog in
[VDP_SUPPORT_ROADMAP.md](VDP_SUPPORT_ROADMAP.md), and the editor behavior in
[VDP_DESIGN_TOOLS.md](VDP_DESIGN_TOOLS.md).

## Required workspace accounting

Every target must account for all three user workspaces:

| Workspace | Required decision |
| --- | --- |
| Screen Image | Supported modes, visible and logical geometry, palette and pixel encoding, generated memory regions, register state, preview source, limits, diagnostics, and exporters |
| Character | Pattern geometry and count, color model, map geometry and attributes, palette selection, import/extraction behavior, preview, persistence, and exporters |
| Sprite | Pattern geometry and count, object count, global or per-object sizing, color and palette rules, transparency, attributes, priority/collision/scanline limits, preview, persistence, and exporters |

An entry uses one of these dispositions:

- **Native** — the workspace models the target's hardware representation.
- **Compatible** — verified earlier-hardware behavior is used without changing
  its byte meaning.
- **Approximation** — the UI or data model uses a documented constrained
  representation and explains what cannot be expressed.
- **Unavailable** — the target has the feature, but the current release does
  not implement it; related controls are disabled.
- **Not applicable** — the hardware has no corresponding facility.

No workspace may silently inherit TMS9918A or F18A rules merely because its
target has tiles or sprites. If an applicable surface is not native or verified
compatible, it must be marked as an approximation or disabled.

## Completion checklist

For each applicable workspace:

1. Record primary hardware references and any disputed behavior.
2. Describe the native data model and the exact supported subset.
3. Register geometry, palette, capacity, attribute, and timing limits.
4. Make the interface derive its controls and labels from those rules.
5. Disable operations that cannot preserve the selected representation.
6. Keep target-specific data separate from compatibility data when both exist.
7. Save and reload the data without reducing color depth, capacity, or geometry.
8. Render previews from the target interpretation, including transparency and
   palette-bank rules.
9. Validate overflows and hardware limits with stable diagnostics.
10. Document native import, extraction, export, and any remaining global
    infrastructure limitation.

A target may start with one complete vertical slice, but it is not described
as fully integrated until Screen Image, Character, and Sprite each have an
explicit disposition. Unsupported applicable operations remain visibly
disabled rather than behaving like another target.

## Sega Master System Mode 4 disposition

The `sega-sms-vdp` target has the following current interpretation:

| Workspace | Disposition | Implemented scope |
| --- | --- | --- |
| Screen Image | Native | 256×192, 256×224, and PAL 256×240 Mode 4; RGB222 CRAM; two 16-color banks; 8×8 4bpp planar tiles; 16-bit name-table attributes; flip-aware tile reuse; display-register output; RAW and PNG export |
| Character | Native | 8×8 4bpp indexed tiles; 448 project slots in the standard layout; background CRAM bank; 32-column maps with 24, 28, or 30 visible rows; full-color drawing, extraction, transforms, preview, clipboard, and project persistence |
| Sprite | Native with one documented editor constraint | 64 sprite entries; global 8×8 or 8×16 size; 4bpp indexed pixels; dedicated sprite CRAM bank; index 0 transparency; rectangular preview/editing; fixed-depth UI; 8×16 rotation disabled |

The sprite editor currently owns one pattern per authored sprite slot. It does
not yet expose arbitrary SAT pattern-index aliasing. Hardware-native sprite
pattern/SAT export remains part of the shared structured-editor export work;
the existing application only exports Screen Image conversion results. This is
an explicit product limitation, not permission to emit TMS sprite bytes.

Legacy TMS-family Screen Image conversion modes remain selectable for
compatibility. They do not change the Character or Sprite workspaces away from
their native Mode 4 interpretation when the Master System target is active.

## Master System behavior that must not be approximated as TMS9918A

- Background tiles are four bitplanes, not a one-bit bitmap plus one color byte
  per row.
- Name-table entries carry tile index, horizontal/vertical flip, palette bank,
  and priority attributes.
- Background and sprites use separate 16-entry CRAM banks in Mode 4.
- Sprites use 4bpp pattern pixels with transparent index 0, not one opaque
  color selected by each sprite attribute.
- The target has 64 sprite entries and an eight-sprites-per-scanline display
  limit.
- Sprite size is global and is 8×8 or 8×16. A 16×16 square is not a native
  Mode 4 sprite size.
- Mode 4 has no per-sprite horizontal flip, vertical flip, palette-bank, or
  priority attribute. Pixel mirror/flip commands bake changes into pattern
  data; they do not create unsupported SAT flags.

## Sega Genesis/Mega Drive Mode V disposition

The `sega-genesis-vdp` target has the following current interpretation:

| Workspace | Disposition | Implemented scope |
| --- | --- | --- |
| Screen Image | Native constrained plane slice | Non-interlaced Mode V H32 256×224, H40 320×224, PAL H32 256×240, and PAL H40 320×240; four 16-entry RGB333 palette banks with shared backdrop entry; 8×8 packed 4bpp tiles; big-endian Plane A map entries with palette and flip attributes; flip-aware tile reuse; display-register output; RAW and PNG export |
| Character | Native | 8×8 packed 4bpp indexed tiles; 2,048 project slots; independent Plane A, Plane B, and Window maps; per-cell palette, flip, and priority attributes; mode-derived 32/40-column by 28/30-row visible maps; priority-composite preview with sprites; native `.TILES`, three `.MAP`, `.PAL`, and `.REG` export; full-color drawing, extraction, transforms, preview, clipboard, and project persistence |
| Sprite | Native editor interpretation with SAT serialization deferred | 80 authored sprite entries; independent 8/16/24/32-pixel width and height; packed 4bpp indexed pixels; palette banks 0–3; index 0 transparency; per-sprite size, flip, position, and high/low priority attributes; fixed-depth UI; rectangular rotation disabled |

The Genesis Sprite workspace presents one size-aware entry grid rather than the
global 8×8 and 16×16 banks used by older VDP targets. Each thumbnail reports
its entry's saved width and height, and selecting it preserves that rectangular
geometry. The active H32 or H40 mode controls whether the grid contains 64 or
80 entries.

Screen Image deliberately emits one flattened Scroll A conversion because a
single bitmap contains no reliable layer or priority information. Multi-layer
scenes are authored explicitly in the Character tiling workspace: Plane A,
Plane B, and Window each have their own map, and composite preview applies the
hardware low/high order with the active sprite set. Screen Image regions can be
extracted directly into whichever of those three layers is active. Character export writes the
shared tile set, all three maps, CRAM, and display-register state. Sprite
pattern and linked-SAT export remain a separate structured-export milestone.

The four standard non-interlaced modes are separate descriptors because H32
and H40 have different visible widths, plane-map storage, sprite-per-line
limits, and register state, while 224- and 240-line modes have different map
visibility. Interlaced Mode 2 is unavailable because its doubled cell and
pattern interpretation needs a separate editor and compiler contract.

## Genesis behavior that must not be approximated as another Sega VDP

- Mode V tiles are packed nibbles, not Master System four-plane tiles.
- CRAM contains four 16-entry RGB333 palettes encoded as big-endian words;
  it is not the Master System's two RGB222 byte banks.
- Plane map words use an 11-bit tile index plus horizontal flip, vertical flip,
  palette, and priority bits.
- Sprites may independently combine widths and heights from 8 to 32 pixels in
  8-pixel steps; they do not share one global 8×8/8×16 size.
- H32 permits 64 sprite entries and 16 sprites per scanline; H40 permits 80 and
  20. The project retains the full 80-entry authored set and reports the active
  display-mode scanline limit.
- Sprite index 0 is transparent. Palette, size, flip, priority, link order, and
  screen coordinates belong to SAT/object data rather than being baked into a
  TMS-style single-color pattern.

## NEC/Hudson HuC6270 disposition

The `huc6270` target has the following current interpretation:

| Workspace | Disposition | Implemented scope |
| --- | --- | --- |
| Screen Image | Native background slice | 256×224 and 320×224; 8×8 4-plane tiles; 32×32 or 64×32 little-endian BAT; 16 background palette banks selected per BAT entry; complete 512-entry HuC6260/VCE color table; initial VDC/VCE state; RAW and PNG export |
| Character | Native | 8×8 4bpp indexed tiles; up to 1,920 project slots; 16 palette banks; 40×28 visible authoring grid; indexed drawing, transforms, clipboard, preview, and project persistence |
| Sprite | Native editor interpretation with SAT serialization deferred | 64 entries; independent 16/32-pixel width and 16/32/64-pixel height; 4bpp indexed pixels; 16 sprite palette banks; index 0 transparency; rectangular rotation disabled; 16-sprites-per-scanline limit reported |

Screen Image allocates BAT at VRAM address zero and makes emitted BAT pattern
indexes point to the pattern region immediately following that BAT. Pattern
bytes use the HuC6270 plane-0/1 then plane-2/3 row layout. The `.REG` sidecar
contains twenty little-endian VDC words plus the selected VCE clock-control
word. Sprite pattern and SAT serialization remains part of structured-editor
export and is not synthesized by the screen-image converter.

## MOS VIC-II disposition

The `vic-ii` target has the following current interpretation:

| Workspace | Disposition | Implemented scope |
| --- | --- | --- |
| Screen Image | Native | 320×200 high-resolution character and bitmap modes; 160×200 logical multicolor character and bitmap modes; 2 KiB character or 8 KiB bitmap data; 1,000-byte screen memory; color RAM where required; 47-byte register image; RAW and PNG export |
| Character | Native high-resolution, constrained multicolor | 8×8 patterns, 256 slots, and a 40×25 map. The converter implements native two-bit wide-pixel allocation; the manual editor retains the shared indexed/binary character surface until per-cell VIC-II multicolor controls are generalized. |
| Sprite | Native editor interpretation with register serialization deferred | Eight 24×21 entries; high-resolution or multicolor depth; index 0 transparency; fixed geometry; eight-sprites-per-scanline limit reported; project persistence |

Palette RGB values are nominal preview references. The encoded color indexes,
screen/color memory, bitmap/character bytes, and mode-register bits are the
portable contract because analog output varies by VIC-II revision and video
standard. Raster interrupts, sprite multiplexing, illegal modes, and timed
color effects are outside this static-asset slice.

## MOS VIC (VIC-20) disposition

The `vic` target has the following current interpretation:

| Workspace | Disposition | Implemented scope |
| --- | --- | --- |
| Screen Image | Native | 176×184 high-resolution and 88×184 logical multicolor character modes; 2 KiB character data; 506-byte screen and color memory; 16-byte register image; RAW and PNG export |
| Character | Native high-resolution, constrained multicolor | 8×8 patterns, 256 slots, and a 22×23 map. The converter implements native wide-pixel multicolor allocation; the manual editor retains the shared binary character surface until VIC-specific per-cell color controls are generalized. |
| Sprite | Not applicable | The VIC has no hardware sprite facility, so the Sprite workspace is disabled and no synthetic sprite format is exposed. |

As with VIC-II, fixed-palette RGB values are nominal preview references while
the emitted indexes and memory/register layout are the durable hardware
contract.
