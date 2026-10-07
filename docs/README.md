# Documentation map

Status: documentation governance index, updated 2026-09-30.

This index identifies which documents are authoritative plans, current
contracts, operating guides, research, or historical evidence. It prevents a
completed phase checklist from competing with the current architecture plan.

## Active program and architecture

| Document | Role |
| --- | --- |
| [../PROJECT_PLAN.md](../PROJECT_PLAN.md) | Concise program status, milestones, and immediate next work |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Accepted module boundaries, contracts, dependency rules, and design decisions |
| [ARCHITECTURE_IMPLEMENTATION_PLAN.md](ARCHITECTURE_IMPLEMENTATION_PLAN.md) | Ordered architecture migration, work packages, and exit gates |
| [TARGET_PROFILES.md](TARGET_PROFILES.md) | Product contract for reusable sources, target profiles, managed outputs, and format applicability |
| [TARGET_IMPLEMENTATION_GUIDE.md](TARGET_IMPLEMENTATION_GUIDE.md) | Required Screen Image, Character, and Sprite accounting for every target, including approximation and disablement rules |
| [VDP_SUPPORT_ROADMAP.md](VDP_SUPPORT_ROADMAP.md) | Planned hardware catalog, target families, and per-target acceptance policy |
| [VDP_DESIGN_TOOLS.md](VDP_DESIGN_TOOLS.md) | Character, pattern, sprite/object, allocation, and hardware-editor behavior |
| [BATCH_MODE.md](BATCH_MODE.md) | Active synchronized media-sequence contract for video, audio, mappings, extraction, playback, and batch conversion |
| [CLIP_MAPS.md](CLIP_MAPS.md) | Implemented clip-map schema, CSV/TSV and Daphne/Hypseus adapters, CLI, and engine loaders |
| [FFMPEG_SETUP.md](FFMPEG_SETUP.md) | Cross-platform setup and validation guide for the external FFmpeg/FFprobe tools |

Implementation ordering comes only from the architecture implementation plan.
Feature documents may state prerequisites and acceptance criteria, but should
not maintain a separate numbered engineering roadmap.

## Current behavior and operating contracts

| Document | Role |
| --- | --- |
| [INTERFACE_WORKFLOW.md](INTERFACE_WORKFLOW.md) | Implemented desktop workflow and interface verification |
| [COMMAND_LINE.md](COMMAND_LINE.md) | Implemented CLI syntax, diagnostics, and exit codes |
| [IMAGE_INPUT.md](IMAGE_INPUT.md) | Input formats, normalization, metadata, and safety limits |
| [EXPORT_FORMATS.md](EXPORT_FORMATS.md) | Export applicability, byte contracts, and write policy |
| [PERFORMANCE.md](PERFORMANCE.md) | Current performance and memory measurement method |
| [RELEASE_PROCESS.md](RELEASE_PROCESS.md) | Packaging, signing, artifact, and release procedure |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | Distributed third-party notices and obligations |

These documents describe implemented behavior unless a section explicitly
says proposed or deferred.

## Historical and compatibility evidence

| Document | Role |
| --- | --- |
| [BEHAVIORAL_BASELINE.md](BEHAVIORAL_BASELINE.md) | Audited behavior of the original reference application |
| [ORIGINAL_DEFECTS.md](ORIGINAL_DEFECTS.md) | Known original defects and compatibility decisions |
| [PHASE3_COMPATIBILITY.md](PHASE3_COMPATIBILITY.md) | Historical conversion-core exit review |
| [PHASE6A_PARITY_REVIEW.md](PHASE6A_PARITY_REVIEW.md) | Historical user-facing parity exit review |
| [PERFORMANCE_COMPARISON.md](PERFORMANCE_COMPARISON.md) | Dated comparison with the original application |
| [releases/](releases/) | Immutable release-specific notes |
| [../tests/golden/README.md](../tests/golden/README.md) | Golden-corpus provenance and maintenance |

Historical evidence remains useful for explaining why current behavior exists.
It is not a backlog. New work belongs in the active plan or an issue, not as an
unchecked item appended to a historical review.

## Research

| Document | Role |
| --- | --- |
| [CONVERSION_ACCELERATION_RESEARCH.md](CONVERSION_ACCELERATION_RESEARCH.md) | Candidate CPU/SIMD/multicore/GPU improvements and their parity risks |

Research does not authorize implementation and has no priority over the active
project plan. Accepted research work must first be represented in the active
plan with tests and exit gates.

## Lifecycle rules

1. Every public contract has one authoritative document.
2. Update the owning document in the same change as the behavior.
3. Do not copy phase lists between documents; link to the active plan.
4. A completed implementation checklist is removed if it adds no enduring
   evidence, or relabeled historical when it records compatibility decisions.
5. Release notes are immutable except for factual corrections.
6. Proposed hardware facts remain proposals until verified against primary
   references and accepted by tests.
7. README presents a short product overview and links here rather than listing
   every planning detail.
