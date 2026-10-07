# VDP support roadmap

Status: authoritative target catalog and hardware delivery sequence. Framework
architecture and work-package ordering are owned by
[ARCHITECTURE.md](ARCHITECTURE.md) and
[ARCHITECTURE_IMPLEMENTATION_PLAN.md](ARCHITECTURE_IMPLEMENTATION_PLAN.md).

## Purpose

This document defines the video hardware and target environments that
RetroVDP Studio intends to support, the order in which support should be
implemented, and the framework changes needed to add them without turning the
application into a collection of unrelated converters.

It is a planning contract, not a substitute for a chip data book. Exact mode
dimensions, register values, palette encodings, memory layouts, sprite limits,
and timing behavior must be verified against primary hardware or manufacturer
documentation before an implementation is accepted.

The product and project model remain defined in
[TARGET_PROFILES.md](TARGET_PROFILES.md). The editor model remains defined in
[VDP_DESIGN_TOOLS.md](VDP_DESIGN_TOOLS.md). This roadmap connects those designs
to a concrete target catalog and hardware sequence; it does not duplicate the
framework migration plan.

## Scope and terminology

The target catalog includes more than chips marketed as Video Display
Processors. It also includes Picture Processing Units, object processors,
CRTC-based adapters, character generators, and systems whose graphics are
primarily assembled by the CPU.

RetroVDP Studio should model that distinction explicitly:

| Target kind | Meaning | Examples |
| --- | --- | --- |
| Video processor | Dedicated video hardware with display modes, memory rules, palettes, and usually sprites | TMS9918A, V9938, V9958, Sega VDPs, VIC-II |
| Picture processor | Console display hardware conventionally described as a PPU | Game Boy, Game Boy Color, Super NES |
| Object processor | Hardware whose primary authoring unit is a scalable or chained object rather than a conventional background tile map | Atari Lynx Suzy, Neo Geo video hardware |
| Display adapter | A framebuffer or CRTC-oriented target without a native sprite engine | IBM CGA/EGA, Apple IIe, Amstrad CPC |
| Character display | A text or semigraphics target where display memory primarily selects fixed or constrained glyphs | Timex Sinclair 1000, TRS-80 Model I/III |

The earlier planning term Faux-VDP describes the final two groups, but it
should not appear in stable profile identifiers or user-facing compatibility
claims. A typed target-kind field communicates the actual difference and does
not imply that the target is less important.

A target profile identifies a hardware contract. A machine or regional
variant supplies configuration around that contract. A display mode defines
one legal combination of geometry, memory layout, palette rules, layers, and
objects within the profile.

For example, NTSC and PAL VIC-II variants should not be separate conversion
engines, and an MSX SCREEN number should not be treated as a new VDP profile.

## Support status

The roadmap uses four status values:

| Status | Meaning |
| --- | --- |
| Implemented | Conversion, validation, preview, export, and tests exist for the declared scope |
| Next | The next profile family scheduled for implementation |
| Planned | The target is in product scope and has an assigned implementation wave |
| Research | The target is in product scope, but primary references and an exact first slice must be approved before coding |

Implemented never means that every trick, undocumented behavior, raster
effect, or software ecosystem format is supported. Each profile publishes its
implemented modes and asset kinds independently.

## Target catalog

Stable identifiers listed for unregistered profiles are proposals. Registered
implemented identifiers are persistence contracts and must remain compatible.

### TI and Yamaha lineage

| Target | Proposed stable ID | Kind | Status | First useful scope |
| --- | --- | --- | --- | --- |
| Texas Instruments TMS9918A | tms9918a | Video processor | Implemented | Graphics II bitmap conversion, multicolor conversions, pattern/color tables, character and sprite authoring |
| F18A | f18a | Video processor | Implemented | TMS9918A-compatible output plus programmable-palette bitmap conversions and enhanced editor capabilities |
| Yamaha V9938 (MSX2 and Geneve 9640) | v9938 | Video processor | Implemented | TMS9918A-compatible modes, SCREEN 5–8 bitmap modes, programmable palette, VRAM-page exports |
| Yamaha V9958 (MSX2+ and Turbo R) | v9958 | Video processor | Implemented | Verified V9938 bitmap modes plus SCREEN 10–12 YJK/YAE color modes and scroll capability descriptors |
| Sega Master System 315-5124/315-5246 family | sega-sms-vdp | Video processor | Implemented | 192/224/PAL-240-line Mode 4, 4-bit planar tiles, flip/palette map attributes, dual CRAM banks, register state |
| Sega Genesis/Mega Drive 315-5313/YM7101 family | sega-genesis-vdp | Video processor | Implemented | H32/H40 224/PAL-240-line Mode V, packed 4bpp tiles, Plane A map attributes, four RGB333 palettes, character and rectangular sprite authoring |

V9938 is the proving target for the generalized framework because it is close
enough to the existing TMS9918A model to reuse tested concepts while forcing
variable geometry, larger VRAM, multiple native pixel encodings, programmable
palettes, and richer sprite attributes.

V9958 must extend verified V9938 components rather than inheriting every
capability automatically. Its additional color and scrolling behavior receives
separate descriptors, encoders, previews, and golden fixtures.

### Sega, Nintendo, and NEC tile engines

| Target | Proposed stable ID | Kind | Status | First useful scope |
| --- | --- | --- | --- | --- |
| Nintendo Game Boy | game-boy-ppu | Picture processor | Planned | 2-bit planar tiles, tile maps, four-shade assignments, OAM sprites |
| Nintendo Game Boy Color | game-boy-color-ppu | Picture processor | Planned | Game Boy-compatible assets plus color palettes, bank and map attributes, color OAM data |
| Super NES 5C77/5C78 family | super-nes-ppu | Picture processor | Planned | 2/4/8-bit planar tiles, mode-described layers, CGRAM palettes, tile maps, OAM sprites |
| NEC/Hudson HuC6270 family (PC Engine/TurboGrafx-16) | huc6270 | Video processor | Implemented | 256/320×224 4-bit planar patterns, background attribute table, 16 background palette banks, VDC/VCE state, compound sprite authoring |

Game Boy and Game Boy Color are separate profiles because color palettes,
attribute storage, and banking materially change the asset contract. They may
share codecs and compatibility analysis without sharing one ambiguous profile.

### Object-centric hardware

| Target | Proposed stable ID | Kind | Status | First useful scope |
| --- | --- | --- | --- | --- |
| Atari Lynx Suzy/Mikey graphics system | atari-lynx | Object processor | Planned | Palette-constrained sprite objects, line data encoding, Sprite Control Block generation, framebuffer preview |
| SNK Neo Geo video hardware | neo-geo | Object processor | Research | 16-pixel-wide sprite strips, FIX-layer characters, palette assignments, chaining and attribute data |

These targets should not be forced through a background-tile-first interface.
Their compiler contract begins with object lists and produces the target's
object metadata and pixel payloads together.

### Commodore video controllers

| Target | Proposed stable ID | Kind | Status | First useful scope |
| --- | --- | --- | --- | --- |
| MOS VIC-II family (Commodore 64) | vic-ii | Video processor | Implemented | High-resolution and multicolor characters and bitmap modes, screen/color memory, 24×21 sprite authoring, pixel-aspect preview |
| MOS VIC family (VIC-20) | vic | Video processor | Implemented | Character sets, screen/color memory, high-resolution and multicolor character output, pixel-aspect preview; sprites not applicable |

Chip revision and television standard belong in profile variants. The first
slice should cover documented static assets and display state. Raster-timed
effects and undocumented modes are later capabilities, not requirements for
initial support.

### Display adapters and character-display targets

| Target environment | Proposed stable ID | Kind | Status | First useful scope |
| --- | --- | --- | --- | --- |
| Amstrad CPC video system | amstrad-cpc | Display adapter | Planned | Mode 0/1/2 pixel packing, mode geometry, palette state, screen-memory interleave |
| IBM Color/Graphics Adapter | ibm-cga | Display adapter | Research | Packed pixels, scanline bank layout, palette/register state, display-aspect preview |
| IBM Enhanced Graphics Adapter | ibm-ega | Display adapter | Research | Planar framebuffer output, palette/register state, selected canonical graphics modes |
| Apple IIe video system | apple-iie | Display adapter | Research | High-resolution and double-high-resolution page layouts, artifact-aware preview, raw page export |
| Timex Sinclair 1000 / ZX81-class display | timex-ts1000 | Character display | Research | Display-file token stream, fixed glyph preview, text and block-graphics conversion |
| TRS-80 Model I/III display | trs80-model1-3 | Character display | Research | Text and semigraphics byte streams, screen-grid preview, software object composition |

The adapter targets receive the same project, source, validation, preview, and
export experience as VDP targets. Their implementation differs because the
output is usually a framebuffer or character stream and software sprites are
an optional generated asset rather than native hardware objects.

## Target families and reusable compiler pipelines

The application should build targets by composing reusable stages. A target
profile chooses and configures those stages; target names must not be scattered
through application-wide switch statements.

| Pipeline family | Shared work | Initial consumers |
| --- | --- | --- |
| TI/Yamaha table and bitmap | Pattern/name/color tables, VRAM pages, indexed and packed bitmap encoders, programmable palette data | TMS9918A, F18A, V9938, V9958 |
| Planar tile | Tile slicing and deduplication, 2/4/8-bit plane wrapping, palette-bank assignment, tile-map attributes | Game Boy, Game Boy Color, Master System, HuC6270, Super NES |
| Packed tile and map | Packed-nibble tiles, multi-plane maps, compound object assembly, palette banks | Genesis/Mega Drive |
| Character and wide-pixel | Character allocation, shared/local colors, logical-pixel scaling, screen/color memory | VIC-II, VIC, Amstrad CPC |
| Object stream | Object slicing, line encoding, chaining, scaling metadata, object-list validation | Atari Lynx, Neo Geo |
| Framebuffer adapter | Packed/planar framebuffer writing, scanline/address mapping, register-state sidecars | CGA, EGA, Apple IIe, Amstrad CPC |
| Character/semigraphics | Cell matching, glyph/token selection, semigraphics decomposition | Timex Sinclair 1000, TRS-80 |

Amstrad CPC appears in two families because its mode-dependent pixel packing
can reuse wide-pixel quantization while its memory layout belongs to the
framebuffer writer.

## Generalized asset pipeline

The durable pipeline is:

    source file or clipboard
        -> source decoder and provenance
        -> normalized raster or structured asset
        -> target profile plus display mode
        -> compile plan and compatibility analysis
        -> palette, layout, tile, or object allocation
        -> target-specific byte encoding
        -> hardware validation
        -> target-faithful preview
        -> one or more registered exporters

The normalized asset layer needs explicit asset kinds:

- raster image or frame sequence;
- palette;
- character or tile set;
- tile/name map;
- sprite pattern set;
- sprite/object list;
- animation sequence;
- framebuffer page; and
- register or display-state preset.

Open as Source may decode any supported format to a raster and then convert it
for any compatible target. Import into Editor preserves structure only when a
format decoder can produce one of the structured asset kinds and the selected
target can represent it honestly.

## Required framework evolution

This section records the capabilities demanded by the target catalog. The
authoritative order, transition adapters, and exit gates are Architecture
Stages 1, 2, 6, and 7 in
[ARCHITECTURE_IMPLEMENTATION_PLAN.md](ARCHITECTURE_IMPLEMENTATION_PLAN.md).

The existing TargetProfile registry is the seed of this system. It already
provides stable IDs, implementation status, a capability mask, VRAM size, and
conversion-mode registration. Before adding many targets, it needs to grow in
the following controlled steps.

### 1. Replace global assumptions with mode descriptors

The current conversion pipeline assumes a 256 by 192 prepared image and a
fifteen-color working palette. Those become properties of a selected display
mode or compiler, not application defaults.

Each display-mode descriptor needs:

- a stable mode ID and display name;
- logical and visible geometry;
- a rational pixel-aspect ratio;
- pixel or tile encoding;
- palette source, entry count, and color encoding;
- tile dimensions and map attributes;
- layer and priority rules;
- sprite/object capabilities and limits;
- address-layout or memory-region descriptors; and
- supported normalized asset kinds.

### 2. Make stable string IDs the serialization boundary

The current C++ TargetProfileId and ConversionMode enums are suitable for the
small implemented set but will become brittle across dozens of profiles and
modes. Saved projects, recipes, command-line arguments, and format metadata
must use stable strings.

Enums may remain as internal conveniences while a profile is compiled into the
application, but serialization must resolve through the registry. Renaming a
display label must never invalidate a project.

### 3. Replace the broad capability mask with typed capabilities

A flag such as Sprites or ProgrammablePalette is not enough to drive an editor
or compiler. The registry needs typed descriptions for:

- palette models;
- tile and map models;
- bitmap/framebuffer models;
- sprite and object models;
- memory regions and alignment;
- display layers and priority; and
- supported import, conversion, and export operations.

The existing flags can remain as fast summary queries, derived from the typed
data rather than maintained independently.

### 4. Introduce compiler, validator, and preview strategies

Each implemented profile/mode registers three independent behaviors:

1. A compiler converts normalized assets into target memory and metadata.
2. A validator proves that the result satisfies the selected hardware rules.
3. A previewer renders the encoded result, including pixel aspect and object
   limits, rather than merely showing the pre-encoded source image.

Exporters consume a validated target result. They do not perform conversion or
silently repair invalid data.

### 5. Generalize target output

TargetMemoryImage currently carries role-tagged byte tables, which should be
retained. The role system must expand beyond TMS9918A tables to support:

- VRAM pages and memory banks;
- palette RAM images;
- tile and object attribute maps;
- sprite/object control records;
- register-state sidecars;
- software sprite masks; and
- optional runtime helper data.

Every byte region needs a stable role ID, address or placement semantics,
alignment, and a declared relationship to the target mode.

### 6. Register formats independently

The format registry must answer two different questions:

- Can this file be decoded as a visual or structured source?
- Can this validated target result be exported in this format?

Applicability should be declared by target ID, mode ID, asset kind, and memory
roles. A central switch over every conversion mode will not scale to this
roadmap.

### 7. Add project-owned managed outputs

The future project model stores one generated output per source asset, target
profile, and display mode. Each managed output records:

- source identity and content hash;
- profile and mode stable IDs;
- compiler settings and palette decisions;
- generated target regions;
- validation diagnostics;
- preview state;
- export history; and
- whether manual edits make automatic regeneration unsafe.

The same source can therefore produce independent V9938, Master System, Game
Boy Color, and VIC-II outputs without one target overwriting another.

## Implementation waves

### Wave 0: framework hardening

Wave 0 corresponds to Architecture Stages 0–7. Complete those stage gates
instead of implementing this list as a second, parallel framework plan.

The following framework gates are now in place for the native Yamaha bitmap
slice:

- add TargetKind and typed display-mode descriptors;
- add stable mode IDs and registry lookup;
- make image preparation use mode geometry and pixel aspect;
- allow variable palette sizes and encodings;
- introduce compiler, validator, and preview strategy registration;
- expand target-memory roles without breaking existing exports;
- move exporter applicability into declarative registrations;
- add normalized structured asset types incrementally;
- retain compatibility with existing tms9918a, f18a, and recipe IDs; and
- add registry tests for duplicate IDs, invalid descriptors, and unsupported
  capability combinations.

Exit gate: the current TMS9918A and F18A suites pass unchanged through the new
registry and strategy boundaries.

### Wave 1: Yamaha V9938

Implement V9938 in vertical slices:

1. Audit the Yamaha data book and select the initial machine and regional
   variants.
2. Register the V9938 profile, memory regions, palette model, sprite model,
   and all documented display modes as descriptors.
3. Route verified TMS9918A-compatible modes through shared components.
4. Implement the first 256-pixel native 4-bit bitmap mode end to end.
5. Add the remaining selected planar/packed bitmap modes.
6. Add programmable palette compilation and preview.
7. Add Sprite Mode 2 pattern, color, and attribute output.
8. Add raw VRAM-page and ecosystem format exporters only after their byte
   contracts are documented.

Hardware command-engine scripts are optional runtime helpers, not the primary
asset representation. The first release should export deterministic memory and
display state before generating command sequences.

Exit gate: deterministic golden memory images, palette and sprite fixtures,
emulator or hardware validation, malformed-input tests, CLI coverage, and one
complete packaged workflow.

### Wave 2: Yamaha V9958

- reuse only V9938 components proven compatible;
- add YJK and YAE color encoders and preview decoding;
- add enhanced scrolling state as a typed mode capability;
- add downgrade analysis for V9938 and TMS9918A targets; and
- test V9958-only modes separately from inherited modes.

Exit gate: every advertised V9958 mode has independent golden data and visual
fixtures, and inherited V9938 fixtures remain byte-identical.

### Wave 3: reusable planar tile engines

Build the common planar-tile compiler while delivering targets in increasing
complexity. The Master System screen-image compiler and native Mode 4
Character/Sprite editor interpretations establish 4-bit wrapping, flip reuse,
two-bank palette assignment, rectangular sprites, map construction, and
overflow diagnostics. Native sprite asset serialization remains part of the
shared structured-editor export work:

1. Master System: add native sprite pattern and SAT serialization to the
   implemented editor interpretation.
2. Game Boy: 2-bit tiles, maps, palette assignments, and OAM.
3. Game Boy Color: color palettes, attribute maps, banking, and color OAM.
4. HuC6270: maintain the implemented 4-bit background compiler and compound
   sprite editor; add native structured sprite-table serialization.

Shared work includes tile slicing, stable deduplication, flip reuse, palette
bank assignment, planar wrapping, map construction, and overflow diagnostics.

### Wave 4: layered 16-bit tile systems

The implemented Genesis slice establishes packed-nibble tiles, four RGB333
palette banks, flip-aware deduplication, Plane A map output, H32/H40 register
state, 2,048-entry character authoring, and 80-entry rectangular sprite
authoring. Remaining Genesis work is multi-plane/window composition and native
structured-editor sprite pattern/SAT link serialization.

1. Genesis/Mega Drive: finish Scroll B/Window design surfaces, compound
   objects, and SAT link generation on the implemented Mode V foundation.
2. Super NES: mode descriptors, 2/4/8-bit planar encoders, layer maps, CGRAM,
   and OAM.

Mode-specific capabilities must control the interface. The Super NES should
not expose every bit depth, layer count, and color rule simultaneously.

### Wave 5: character and wide-pixel systems

1. VIC-II: maintain the implemented character/bitmap compilers and 24×21
   sprite editor; add native structured sprite-table serialization.
2. VIC: maintain the implemented high-resolution and multicolor character
   modes; sprites remain not applicable.
3. Amstrad CPC Modes 0, 1, and 2 with shared wide-pixel quantization and a
   dedicated screen-memory writer.

This wave establishes rational pixel-aspect preview, shared/global versus local
color allocation, and display-state sidecars.

### Wave 6: object-centric systems

1. Atari Lynx object line data and Sprite Control Blocks.
2. Neo Geo FIX characters, sprite strips, chaining, palettes, and attributes.

The editor must present objects, slicing, and chains directly. It must not
pretend these targets are ordinary fixed-size background tile maps.

### Wave 7: framebuffer and character-display adapters

Implement a generic address-layout writer and then deliver:

1. IBM CGA selected graphics modes;
2. IBM EGA selected planar modes;
3. Apple IIe high-resolution and double-high-resolution pages;
4. TRS-80 text and semigraphics;
5. Timex Sinclair 1000 display-file output.

Apple artifact color requires a separate display simulation from its stored
bits. Character-display targets require glyph or token matching rather than a
pixel framebuffer encoder.

## Software sprite compiler

Targets without native sprites may optionally generate software-blit assets.
This is an asset tool, not a fabricated hardware capability.

For binary masked sprites, a compiler may emit a clear mask and an object
payload for the runtime operation:

    destination = (background AND clear-mask) OR object-payload

That equation is not universal. Multi-plane adapters, artifact-color modes,
transparent color keys, and targets with read/modify/write restrictions may
need different payload sets or runtime routines. Each exporter declares the
algorithm, plane order, alignment, clipping assumptions, and required runtime
operation.

## Pixel aspect and preview policy

Stored pixel geometry and displayed pixel shape are separate properties.
Profiles store pixel aspect as a rational value and the preview offers:

- raw logical pixels for exact editing;
- aspect-correct display for visual review; and
- optional scanline, border, or composite simulation where validated.

The compiler always operates on logical pixels. Preview scaling must not alter
encoded data or silently resample an edited native asset.

## Per-target definition of done

A profile is not Implemented until all applicable items are complete:

1. Primary references are recorded with edition, revision, source URL, and a
   local content hash where redistribution permits.
2. Chip, machine, and regional variants are explicitly separated.
3. Profile and mode descriptors validate successfully.
4. At least one complete asset path compiles to target bytes.
5. Target bytes pass independent structural validation.
6. Preview renders from encoded target data.
7. Golden fixtures cover edge cases and representative artwork.
8. Exported files load in an approved emulator, hardware workflow, or both.
9. Invalid palettes, maps, sprites, addresses, and overflows produce stable
   diagnostics.
10. CLI and GUI expose only supported modes and formats.
11. Direct editor import is capability-gated and documents information loss.
12. Documentation states implemented scope, omissions, and validated variants.
13. Package-level smoke tests include the new target where practical.

In addition, every target must publish a disposition for Screen Image,
Character, and Sprite as defined by
[TARGET_IMPLEMENTATION_GUIDE.md](TARGET_IMPLEMENTATION_GUIDE.md). An
unimplemented applicable workspace is disabled and identified as unavailable;
it must not silently reuse another target's interpretation.

## Validation and reference policy

Community documentation and existing tools are useful for discovery and test
comparison, but a profile's normative byte contract must come from primary
manuals, manufacturer development material, verified hardware behavior, or an
explicitly documented reconciliation of those sources.

Every implementation begins with a short target evidence document containing:

- primary reference inventory;
- disputed or ambiguous behavior;
- selected first-slice modes;
- emulator and hardware test environments;
- golden fixture provenance; and
- features deliberately deferred.

Starting primary-reference sources for the early waves include:

- [Texas Instruments TMS9918A/TMS9928A/TMS9929A Video Display Processors Data
  Manual](https://www.bitsavers.org/components/ti/TMS9900/TMS9918A_TMS9928A_TMS9929A_Video_Display_Processors_Data_Manual_Nov82.pdf);
- [Yamaha V9938 MSX-Video Technical Data
  Book](http://www.bitsavers.org/pdf/yamaha/Yamaha_V9938_MSX-Video_Technical_Data_Book_Aug85.pdf);
- [Sega Genesis Software Development
  Manual](https://gendev.spritesmind.net/files/md/tech/Genesis%20Software%20Development%20Manual%20%5BVersion%202.0%5D%20%5B1991-07-09%5D.pdf);
- [Nintendo Game Boy Programming
  Manual](https://files.nekoblog.org/uploads/pdf/39999184-GameBoy-Programming-Manual.pdf);
- [Commodore 64 Programmer's Reference
  Guide](https://www.commodore.ca/manuals/c64_programmers_reference/c64-programmers_reference.htm);
- [IBM Personal Computer Options and Adapters Technical
  Reference](https://bitsavers.org/pdf/ibm/pc/cards/Technical_Reference_Options_and_Adapters_Volume_2_Apr84.pdf);
- [Atari Lynx developer
  documentation](https://atariage.com/Lynx/archives/developer_docs/index.html?SystemID=LYNX); and
- [SNK Neo Geo hardware and software development
  specifications](https://www.neogeodev.org/NG.pdf).

The reference list expands when a target enters active implementation.

## Decisions that remain open

The following decisions should be made during Wave 0 or the relevant target's
evidence review:

- final stable IDs for all unregistered targets;
- whether machine presets live inside a profile or in a separate machine
  registry;
- the first supported regional and chip revisions;
- the exact initial V9938 and V9958 mode set;
- how register-state sidecars are represented across exporters;
- whether emulator-driven golden verification runs in CI;
- which software-sprite runtime contracts merit first-class exporters; and
- which ecosystem file formats qualify for lossless native editor import.

The roadmap order may change when primary research exposes a dependency, but
new targets must continue to use the shared registry, normalized asset model,
compiler strategies, validators, previews, and independent managed outputs.
