# Command-line interface

`retrovdp-cli` is the headless automation frontend for RetroVDP Studio.
It uses the same bounded image loaders, transform and adjustment pipeline,
conversion core, export manifest generators, and atomic file writer as the
desktop application. It does not open a window or read the desktop
application's persisted settings.

It can instead load the versioned recipe saved by the desktop interface. The
recipe supplies the source path and every Screen Image conversion-panel value;
the script supplies the output directory:

```shell
retrovdp-cli \
  --recipe artwork.rvdp.json \
  --output converted
```

`--input`, `--target`, `--mode`, `--preset`, and `--format` override their
recipe values when explicitly supplied. Character and Sprite recipes retain
their editor and set state but are rejected by the CLI until those
hardware-data converters are implemented.

## One-shot conversion

Both the source file and destination directory are required:

```shell
retrovdp-cli \
  --input artwork.png \
  --output converted \
  --target tms9918a \
  --mode bitmap-9918a \
  --preset balanced \
  --format tifiles
```

The output directory is created when necessary. Existing files are rejected
as a complete preflight operation; no file is changed unless `--overwrite` is
explicitly supplied.

Use `retrovdp-cli --help` for the installed executable's complete option
summary.

## Targets, modes, presets, and formats

The implemented target names are `tms9918a`, `f18a`, `v9938`, `v9958`,
`sega-sms-vdp`, and `sega-genesis-vdp`. The default target is `tms9918a`; the default mode is
`bitmap-9918a`, the default preset is `balanced`, and the default format is
`tifiles`. A target accepts only modes declared compatible by its profile.

Supported modes are:

- `bitmap-9918a`
- `greyscale-bitmap-9918a`
- `black-and-white-bitmap-9918a`
- `multicolor-9918`
- `dual-multicolor-9918`
- `half-multicolor-9918a`
- `bitmap-color-only-9918a`
- `paletted-bitmap-f18a`
- `scanline-palette-bitmap-f18a`
- `screen-5-v9938`
- `screen-6-v9938`
- `screen-7-v9938`
- `screen-8-v9938`
- `screen-10-v9958`
- `screen-11-v9958`
- `screen-12-v9958`
- `mode-4-sms-192`
- `mode-4-sms-224`
- `mode-4-sms-240-pal`
- `mode-5-genesis-h32`
- `mode-5-genesis-h40`
- `mode-5-genesis-h32-pal`
- `mode-5-genesis-h40-pal`

Master System Mode 4 supports `raw` and `png` export. A RAW manifest contains
`.TILES` planar pattern data, a `.MAP` name table, `.PAL` RGB222 CRAM bytes,
and `.REG` values for VDP registers 0–10.

Genesis Mode V supports `raw` and `png` export. A RAW manifest contains
`.TILES` packed-nibble pattern data, a big-endian `.MAP` Plane A name table,
`.PAL` big-endian RGB333 CRAM words, and `.REG` values for VDP registers 0–23.

Supported presets are `balanced`, `crisp-pixel-art`, `smooth-photograph`, and
`ordered-retro`. The shorter aliases `crisp`, `smooth`, and `ordered` are also
accepted. A preset establishes deterministic conversion settings and scaling;
`--mode` then selects the target hardware layout.

Supported export formats are `tifiles`, `v9t9`, `raw`, `rle`, `msx-sc2`,
`coleco-cvpaint`, `adam-powerpaint`, `adam-hgr`, and `png`. Applicability is
validated against the selected conversion mode before any output is written.

`--base-name` overrides the source-derived output name. Characters outside
letters, digits, underscore, and hyphen are replaced with underscores.

## Automation and JSON

Pass `--json` to receive exactly one compact JSON object on standard output.
A successful object includes the resolved input/output paths, stable target
identifier, selected mode, preset and format, generated file paths and byte
counts, and any manifest warnings:

```json
{"status":"ok","target":"tms9918a","mode":"bitmap-9918a","preset":"balanced","format":"raw","files":[{"path":"/output/ART.TIAP","bytes":6144}],"warnings":[]}
```

Failures contain `status: "error"`, a stable diagnostic `code`, a human-readable
`message`, and the numeric `exitCode`. Output conflicts additionally list all
conflicting paths under `details.conflicts`.

## Media-tool diagnostics

Validate the external FFmpeg and FFprobe executables without supplying an
image conversion job:

```shell
retrovdp-cli --check-media-tools
```

The search checks beside the application and then `PATH`. Explicit tools can
be tested with `--ffmpeg-path <file>` and `--ffprobe-path <file>`. Add `--json`
to receive resolved paths, version identities, availability, and errors as one
JSON object. Installation guidance is in [FFMPEG_SETUP.md](FFMPEG_SETUP.md).

## Media probing and clip extraction

Inspect one local media file and report its normalized duration, video/audio
streams, exact rational rates, and chapters:

```shell
retrovdp-cli --probe-media source.mp4 --json
```

Create a clip package containing PNG frames, optional audio, and `clip.json`:

```shell
retrovdp-cli --extract-media source.mp4 --output clip-one \
  --start 5 --duration 3 --fps 15 \
  --extract-width 320 --extract-height 240 --extract-sizing fit \
  --audio wav --json
```

`--start` defaults to zero, omitted `--duration` uses the remaining source,
and omitted `--fps` uses the source average or native rate. Sizing is `native`,
`fit`, `fill`, or `stretch`; non-native modes require both dimensions. Audio
is `none` (the default), `wav`, `flac`, or `ogg`. `--video-stream` and
`--audio-stream` select global FFprobe stream indexes.

The destination must not already exist. Extraction is performed in a sibling
staging directory and renamed into place only after every requested frame,
audio file, and manifest has completed. The initial implementation accepts a
single media file; ordered VOB sets, DVD title navigation, mapping-driven
extraction, and recipe override execution are later slices.

Convert every frame in an extracted package with one frozen Screen Image
recipe:

```shell
retrovdp-cli --convert-media-clip clip-one/clip.json \
  --recipe target.rvdp.json --output target-run \
  --conversion-workers auto --json
```

The destination must not exist. Each frame passes through the same conversion
core and recipe snapshot. The completed run contains `native/frame_NNNNNN/`
target files, `previews/frame_NNNNNN.png` target-faithful monitor images, the
exact `recipe.rvdp.json` snapshot, `run.json`, and a monitor-compatible
`clip.json`. A SHA-256 recipe digest and final target, mode, format, timing, and
per-frame paths are recorded. The staging directory is atomically renamed only
after every frame and manifest succeeds.

`--conversion-workers` controls bounded frame-level parallelism for batch
conversion. It accepts `auto` or an integer from `1` through the detected
logical-processor count. `auto` leaves one processor available when possible
and caps the default at eight workers. Workers convert already-extracted image
frames; this option does not launch additional FFmpeg processes. The completed
run manifest records the resolved worker count under `conversion.workers`.

## Clip-map conversion

Validate and convert native JSON, CSV/TSV, or Daphne/Hypseus mappings without
decoding media:

```shell
retrovdp-cli --convert-clip-map framefile.txt \
  --clip-map-input-format daphne --mapping-fps 30000/1001 \
  --clip-map-output-format json --output clips.json --json
```

Input/output formats are `auto`, `json`, `csv`, `tsv`, `daphne`, `hypseus`,
and `framefile`. Daphne/Hypseus framefiles do not contain FPS, so
`--mapping-fps` explicitly attaches a decimal or rational rate; if omitted,
the native document retains an unspecified rate and the command reports a
warning. Native JSON and delimited forms preserve global and per-entry recipe
and target references. Legacy framefile export warns when richer fields must
be omitted. Existing output is rejected unless `--overwrite` is explicit. See
[CLIP_MAPS.md](CLIP_MAPS.md) for the schema and path rules.

## Exit codes

| Code | Meaning |
| ---: | --- |
| `0` | Conversion and export completed successfully. |
| `2` | Command syntax, required option, target, mode, preset, format, or base name is invalid. |
| `3` | The source image could not be loaded safely. |
| `4` | Image transformation or conversion failed. |
| `5` | The selected export is unavailable or could not be generated. |
| `6` | Output conflicts were found or files could not be written. |
| `7` | A required external executable is missing, invalid, or could not be launched. |

The CLI never prompts. Automation must use `--overwrite` deliberately when
replacement is intended and should treat every nonzero code as failure.

## Verification

`cli_workflow_validation` launches the built CLI as a separate process. It
checks help output, explicit target selection, a complete image-to-RAW
conversion, JSON results and file sizes, overwrite preflight, explicit
overwrite, invalid-mode diagnostics, input-load failure codes, and successful
and missing FFmpeg/FFprobe dependency checks. It also covers media probing and
atomic frame/audio clip extraction with fixture executables, frozen-recipe
batch conversion, native per-frame files, output-monitor previews, and
clip-map adapter round trips.
