# RetroVDP Studio project plan

Status: active roadmap, updated 2026-10-07.

Presentation checkpoint: project naming and configured targets now persist in
the version-1 project envelope; the main window exposes project lifecycle
commands, a Project Bar with active-target selection, a common Source tab, and
target-filtered mode presentation. Tabbed view owns mode selection while split
views use the Mode menu.

This is the concise program-level plan. Architectural boundaries are defined
in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and the ordered engineering
migration is defined in
[docs/ARCHITECTURE_IMPLEMENTATION_PLAN.md](docs/ARCHITECTURE_IMPLEMENTATION_PLAN.md).
Feature documents define behavior but do not maintain competing implementation
phase lists.

## Mission

RetroVDP Studio creates, edits, validates, and exports graphics assets for
projects that target classic video hardware. Reusable source material can
produce independent screen-image, character/pattern-map, palette, and
sprite/object outputs for several target hardware profiles. Each output obeys
the selected hardware rules and can be exported manually or collected into a
deterministic deployment manifest.

## Product invariants

- Sources are reusable; target outputs are independently owned.
- The target's encoded artifact is the truth for validation, preview, and
  export.
- Target and format support is registered through stable IDs and typed
  descriptors.
- Project and hardware models remain usable without Qt/QML.
- Native import preserves structure only when the active target can represent
  it honestly.
- Destructive regeneration, overwrite, downgrade, or data loss is explicit.
- Existing TMS9918A/F18A output and supported recipe/CLI contracts remain
  compatible through architecture migration.

## Delivered foundation

The following program is implemented and maintained rather than planned again:

- a portable C++20 conversion core for the audited TMS9918A, F18A, Yamaha
  V9938/V9958, Sega Master System, Sega Genesis/Mega Drive, HuC6270, VIC-II,
  and VIC conversion modes;
- bounded common-raster, PCX, and supported retro-format input;
- deterministic target-table generation and golden fixtures;
- RAW, RLE, TIFILES, V9T9, MSX, Coleco, Adam, Extended BASIC, ROM, and PNG
  export with manifest preflight and atomic writes;
- desktop source preparation, live conversion, settings, previews, and export;
- target-independent source drawing, selection, transforms, system-font text,
  raster placement, clipboard exchange, and undo/redo;
- headless CLI conversion/export with stable diagnostics and exit codes;
- initial Character and Sprite editor surfaces, pattern editing, placement,
  clipboard exchange, undo/redo, and version-1 recipes;
- target-described Screen Image-to-Character extraction and configurable
  horizontal/vertical Pattern Previewer;
- Active Target-driven Screen Image, Character, and Sprite options, including
  descriptor-owned sprite constraints and display-mode option visibility;
- cross-platform build, test, package, and release procedures; and
- recorded baseline, compatibility, performance, and release evidence.

Completed migration checklists are retained only as historical verification
records where they still explain compatibility decisions. They are not active
roadmaps.

## Current program — architecture modernization

The next program is the staged migration in the architecture implementation
plan:

1. protect current behavior with registry/schema/applicability contracts;
2. introduce stable IDs and typed display-mode descriptors;
3. generalize target artifacts and format registration;
4. extract a portable project/asset model and one serializer;
5. move workflow into application use cases and thin the Qt controllers;
6. register compiler, validator, and preview strategies;
7. add managed multi-target outputs; and
8. prove the design with the first V9938 vertical slice.

The work is incremental. The current GUI, CLI, converters, and recipes remain
available at every stage.

## Feature programs

These specifications feed the architecture program and then continue through
vertical product slices:

| Program | Authority | Current state |
| --- | --- | --- |
| Target/project model | [docs/TARGET_PROFILES.md](docs/TARGET_PROFILES.md) | Model agreed; managed multi-target output not implemented |
| Hardware target catalog | [docs/VDP_SUPPORT_ROADMAP.md](docs/VDP_SUPPORT_ROADMAP.md) | TMS9918A/F18A, V9938/V9958 bitmap and YJK/YAE, Master System Mode 4, and Genesis Mode V profiles implemented |
| Character and sprite authoring | [docs/VDP_DESIGN_TOOLS.md](docs/VDP_DESIGN_TOOLS.md) | Editor foundations implemented; portable project model, allocation, validation, and full export remain |
| Media sequences | [docs/BATCH_MODE.md](docs/BATCH_MODE.md) | Active; extraction/monitoring, frozen-recipe native batch output, uniform output playback, and initial mapping adapters implemented |
| Release program | [docs/RELEASE_PROCESS.md](docs/RELEASE_PROCESS.md) | Beta pipeline active; stable-release gates remain |
| Performance improvements | [docs/CONVERSION_ACCELERATION_RESEARCH.md](docs/CONVERSION_ACCELERATION_RESEARCH.md) | Research backlog; parity and architecture gates take priority |

## Milestones

### A — Extensible registry foundation

Complete architecture Stages 0–2. Existing output is unchanged, but target,
mode, region, and format identity is stable and descriptor-driven.

### B — Shared project core

Complete Stages 3–4. Character, sprite, and source data are portable project
assets; GUI and CLI use one versioned serializer.

### C — Application boundary

Complete Stages 5–6. Frontends are adapters over shared use cases, and every
implemented mode registers compile, validate, and preview behavior.

### D — Multi-target campaign workflow

Complete Stage 7. A project owns shared sources and independently editable,
stale-aware outputs for several targets, with reproducible native deployment
manifests.

### E — V9938 proving release

Complete the first Stage 8 vertical slice and its hardware/reference review.
Use the result to approve or revise the remaining support waves.

## Media-sequence delivery track

Media work proceeds through vertical slices that preserve the same application
and target boundaries as the architecture milestones:

1. **Media-tool foundation** — discover and validate FFmpeg/FFprobe, persist
   executable overrides, expose desktop and CLI diagnostics, publish setup
   instructions, and test missing/invalid tools. Implemented.
2. **Probe and timeline** — inspect one file and build a rational synchronized
   media timeline with bounded ranges and selectable video/audio streams.
   The single-file portion is implemented; ordered segment sets remain.
3. **Clip package extraction** — generate timestamped source frames and
   optional audio with extraction scale/framing and atomic metadata. The
   single-file CLI slice is implemented; crop, progress, cancellation, disk
   estimates, resume validation, and desktop workflow remain.
4. **Filmstrip and source monitor** — virtualized thumbnails, scrubbing,
   frame stepping, timestamp playback, and selected-frame handoff are
   implemented. Audio-master synchronization and decoded-image prefetch remain.
5. **Target batch and output monitor** — global frozen-recipe conversion,
   independent target framing, target-native per-frame outputs, target-faithful
   previews, atomic run metadata, common-monitor playback, and bounded
   deterministic frame-worker parallelism are implemented.
   Folder and per-clip recipe overrides, progress, cancellation, and resume remain.
6. **Mapping adapters** — versioned native JSON, CSV/TSV, Daphne/Hypseus
   framefile import/export, and documented Unity/Godot JSON loaders are
   implemented. Mapping-driven extraction and recipe resolution remain.
7. **DVD structures** — capability-probe and use FFmpeg `dvdvideo` title,
   chapter, program-chain, and angle selection without treating authored DVDs
   as lexical VOB concatenations.
8. **Batch hardening** — folder-of-clips operation, continuation policy,
   content digests, interruption recovery, storage limits, and the complete
   cross-platform test/package matrix.

## Cross-cutting quality gates

- Deterministic and golden output remains intentional and reviewed.
- Untrusted input and project data is bounded before allocation.
- Core/project/target modules build without Qt.
- UI enablement is derived from descriptors rather than chipset-name branches.
- GUI and CLI consume the same schema and application contracts.
- Export never performs hidden conversion or repair.
- Save/export operations are atomic and preflight collisions.
- Background work is cancellable and cannot publish a stale generation.
- Every implemented target slice includes descriptor, compiler, validator,
  preview, exporter, and project round-trip coverage as applicable.

## Key risks

| Risk | Control |
| --- | --- |
| Big-bang refactor destabilizes proven conversion | Add seams beside old APIs, migrate one consumer, verify parity, then delete |
| Generic model erases hardware differences | Normalize asset kinds and contracts, not target-specific encoding rules |
| Descriptor and implementation drift | Validate registries and require strategies/roles in contract tests |
| Recipe migration loses future or unknown data | Central serializer, explicit versions, fixtures, and refusal of destructive saves |
| Controller split only redistributes coupling | Move ownership into project/application layers before dividing presentation classes |
| Premature plug-in design freezes the wrong ABI | Use statically registered strategies until an external plug-in need is demonstrated |
| V9938 drives shortcuts before foundations exist | Enforce framework exit gates before its native mode implementation |

## Immediate next work

1. Move image preparation geometry and palette requirements to the registered
   display-mode descriptors.
2. Name and isolate the PowerPaint 240x160 framing policy.
3. Finish registry-backed target/mode presentation adapters while retaining
   version-1 recipe compatibility.
4. Begin Stage 2's general target-artifact and export applicability contracts.
5. Re-run the complete platform matrix and record the descriptor milestone.

For the active media track, the immediate next slice is audio-master playback
synchronization, followed by mapping-driven clip-package extraction and
global/per-entry recipe resolution.

## Documentation governance

[docs/README.md](docs/README.md) classifies authoritative specifications,
operating guides, research, and historical evidence. When implementation
changes a public contract, update the owning document in the same change.
Completed checklists are either removed or labeled historical; they must not
continue to present themselves as the active plan.
