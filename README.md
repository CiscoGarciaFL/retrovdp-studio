# RetroVDP Studio

<p align="center">
  <img src="app/assets/RetroVDPStudio.svg" width="220" alt="RetroVDP Studio logo">
</p>

RetroVDP Studio is a cross-platform graphics workspace for classic video
display processors. It combines source preparation and conversion with
hardware-aware screen, character, pattern, palette, and sprite tools.

The application is organized around three independent ideas:

1. **Sources are reusable.** Common images and supported retro formats can be
   opened as source material without being restricted to the hardware or
   software that originally created them.
2. **Targets own their output.** Each selected VDP profile applies its own
   display modes, palettes, memory organization, sprite limits, validation,
   and export formats. A future project may retain several managed target
   outputs derived from the same source.
3. **Native imports preserve structure only when it is meaningful.** Pattern,
   sprite, tile-map, palette, and other editors offer direct import only when
   the source data can be represented by the active target. Other supported
   files remain available through the visual conversion path.

## Status

The current release implements TMS9918A, F18A, V9938, V9958, Sega Master
System, Sega Genesis/Mega Drive, NEC/Hudson HuC6270, MOS VIC-II, MOS VIC,
Nintendo Game Boy, Nintendo Game Boy Color, and Super NES conversion profiles.
V9938 SCREEN 5–8 and V9958 SCREEN 10–12 are
compiled through the registered Yamaha bitmap strategies, including
programmable palette and YJK/YAE output. The Master System profile compiles
192-, 224-, and PAL 240-line Mode 4 screens into 4-bit planar tiles, name-table
attributes, two 16-color CRAM banks, and display-register state. The Master
System Character and Sprite workspaces use native Mode 4
interpretations: 448 8×8 4bpp background tiles, 64 sprite entries, global
8×8/8×16 sprite sizing, and separate background/sprite palette banks. The
Genesis profile compiles H32 256-pixel and H40 320-pixel Mode V screens at
224 or PAL 240 lines into packed 4bpp tiles, a Plane A map, four 16-entry
RGB333 CRAM banks, and display-register state. Its Character workspace exposes
2,048 indexed tiles plus independent Plane A, Plane B, and Window maps with
palette, flip, and priority attributes. A composite preview applies native VDP
priority order across those maps and the active sprite set, and Screen Image
regions can be extracted straight into the selected layer. Character export
writes packed `.TILES`, three big-endian map files, `.PAL`, and `.REG` assets;
its Sprite workspace exposes 80 entries, four palette banks, and every
rectangular 8-to-32-pixel hardware size. Native linked-SAT export remains part
of the shared export work. HuC6270 conversion emits 256×224 and 320×224
backgrounds as native 4-plane tiles, BAT entries, the complete VCE palette
table, and VDC/VCE state. VIC-II conversion covers high-resolution and
multicolor character and bitmap modes; VIC-20 conversion covers native
high-resolution and multicolor character modes. Both produce native screen,
color, bitmap/character, and register sidecars as applicable. The input
pipeline loads common Qt raster formats, PCX, and supported retro formats with
explicit safety limits. The export layer provides deterministic RAW, RLE,
TIFILES, V9T9, MSX, Coleco, Adam, Extended BASIC, ROM, and PNG exporters with
generated-file manifests and overwrite preflight. The Qt workspace provides
menu-driven tabbed/horizontal/vertical previews, an adjacent or overlay Side
Panel, debounced background conversion,
presets, undo/reset, palette inspection, persistent settings, export summaries,
and accessible narrow/wide layouts. It also provides a headless command-line
frontend for deterministic one-shot conversion and export, stable exit codes,
JSON diagnostics, and automation-friendly overwrite handling.

The current roadmap is [`PROJECT_PLAN.md`](PROJECT_PLAN.md). The audited target
architecture and its incremental migration are in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`docs/ARCHITECTURE_IMPLEMENTATION_PLAN.md`](docs/ARCHITECTURE_IMPLEMENTATION_PLAN.md).
[`docs/README.md`](docs/README.md) maps the remaining product contracts,
operating guides, research, compatibility evidence, and release records so
completed historical phases do not compete with the active plan.

## Technology

- C++20 conversion and file-format core
- Qt 6.8 or newer
- Qt Quick Controls 2 user interface
- CMake build system
- Zed-friendly C++ development through `clangd` and CMake compilation data
- CTest-driven validation and golden-file compatibility tests

## Build

Install Qt 6.8 or newer with Qt Quick, CMake, and a native C++20 toolchain.
The committed presets use Qt MinGW on Windows, GCC or Clang on Linux, and
AppleClang on macOS:

```shell
cmake --preset <platform-preset>
cmake --build --preset <platform-preset>
ctest --preset <platform-preset>
```

Choose `windows-mingw-debug`, `windows-mingw-release`, `linux-debug`, or
`macos-debug` for `<platform-preset>`. Use the Windows Release preset for
normal use and performance evaluation; Debug is intended for diagnostics and
is substantially slower in the exhaustive-search modes. The Windows presets
match the toolchain installed at
`C:\Qt` on the current development machine. Linux and macOS expect Qt, CMake,
and Ninja to be discoverable in the shell environment. Machine-specific
overrides belong in the ignored `CMakeUserPresets.json` file.

Build trees cache the absolute source path. After moving or renaming the
checkout, remove or replace the affected build tree before configuring it
again; an old cache cannot be reused from the previous path.

The build directory initially contains only the application executable. To
copy Qt, MinGW, plug-ins, and QML runtime files beside it so the executable can
be launched directly from Explorer, run:

```powershell
.\tools\deploy_windows_preview.ps1
```

The command above defaults to the Debug configuration; its runnable folder is
`build\windows-mingw-debug\bin`. Deployment explicitly uses the release Qt
runtime shipped by the installed MinGW kit, even though the application itself
retains Debug symbols.

To incrementally rebuild, deploy, and launch the optimized Release application
in one step, run:

```powershell
.\tools\run_windows_preview.ps1
```

The release-specific launcher is an equivalent convenience alias. To reuse a
known-current build without rebuilding, pass `-SkipBuild`:

```powershell
.\tools\run_windows_release_preview.ps1
.\tools\run_windows_preview.ps1 -SkipBuild
```

Close the preview window before rebuilding the application because Windows
locks a running executable.

The same build also creates `retrovdp-cli` in the `bin` directory. A
minimal headless conversion is:

```shell
retrovdp-cli --input artwork.png --output converted --mode bitmap-9918a --format tifiles
```

Add `--json` for machine-readable results. See
[`docs/COMMAND_LINE.md`](docs/COMMAND_LINE.md) for all modes, presets, formats,
overwrite behavior, and exit codes.

The media-sequence program uses separately installed FFmpeg and FFprobe tools.
Tool discovery is available in **Preferences → Media Tools** and through
`retrovdp-cli --check-media-tools`; the CLI can also probe one media file and
extract timestamped PNG frames plus optional audio into an atomic clip package.
The desktop **Open Media Clip** command loads that package into a source monitor
with playback controls, frame stepping, scrubbing, and a thumbnail filmstrip.
**Convert Clip** applies one frozen Screen Image recipe asynchronously, writes
per-frame native target assets and uniform target-faithful previews, then opens
the completed output run in the same monitor. Batch conversion can use a
configurable bounded pool of frame workers without launching extra FFmpeg
processes. The CLI can also normalize and
convert native JSON, CSV/TSV, and Daphne/Hypseus clip mappings; the native
schema includes reference loaders for Unity and Godot ports.
See
[`docs/FFMPEG_SETUP.md`](docs/FFMPEG_SETUP.md) for platform setup instructions,
[`docs/VIDEO_CLIP_SUPPORT.md`](docs/VIDEO_CLIP_SUPPORT.md) for the complete
implemented video-clip workflow,
[`docs/CLIP_MAPS.md`](docs/CLIP_MAPS.md) for mapping interchange, and
[`docs/BATCH_MODE.md`](docs/BATCH_MODE.md) for the active video, audio,
clip-mapping, and frame-conversion plan.

Zed users can run the matching configure, build, and test entries from the
task picker. CMake writes `compile_commands.json` into each build directory so
Zed's `clangd` language server receives the project's actual compile flags.

## Development checks

The repository includes `.editorconfig`, `.clang-format`, and `.qmlformat.ini`
so C++, QML, and basic text formatting remain consistent across editors and
operating systems. GitHub Actions configures, builds, and tests every change on
Windows with MinGW, Ubuntu with GCC, and both Intel and Apple Silicon macOS
runners with AppleClang.

## Project structure

- `app/` — current Qt desktop shell, QML interface, application pipeline, and CLI
- `include/retrovdp/core/` — public, platform-neutral core API
- `include/retrovdp/formats/` — portable export requests and manifests
- `include/retrovdp/imageio/` — Qt image-loading adapter API
- `src/core/` — conversion and independent codec implementation
- `src/formats/` — deterministic retro output writers
- `src/imageio/` — common raster, PNG, and filesystem adapters
- `tests/` — unit, workflow, and golden-output tests
- `docs/` — architecture, product contracts, operating guides, and evidence

The target module and directory structure is defined in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). It is introduced incrementally;
the current tree is not expected to be moved in one refactor.

The untouched upstream reference checkout is kept outside this repository so
original and ImgSource-related material cannot enter the new implementation.
Its exact audited commit is pinned in the behavioral-baseline document.

## Attribution

The RetroVDP Studio cross-platform architecture, Qt interface, user
experience, a broad multi-target design are created by Cisco Garcia / CiscoGarciaFL.

RetroVDP Studio is inspired by
[Convert9918](https://github.com/tursilion/convert9918), created by Mike Brent
(Tursi). Convert9918 remains a historical inspiration and behavioral reference.

See [`NOTICE.md`](NOTICE.md) for full attribution and [`LICENSE`](LICENSE) for
the governing terms. Commercial use and distribution under different terms require
prior permission from the author.
