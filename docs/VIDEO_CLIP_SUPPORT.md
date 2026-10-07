# Video clip support

RetroVDP Studio can turn a section of a video into a portable folder of
timestamped PNG frames and optional audio, play those frames in a source
monitor, and convert every frame through a saved Screen Image recipe. The
converted run contains target-native files plus uniform PNG previews that can
be opened in the same monitor.

This is the operating guide for the implemented video-clip workflow. The
broader product contract and planned extensions are described in
[BATCH_MODE.md](BATCH_MODE.md).

## Current feature status

| Capability | Status |
| --- | --- |
| Locate and validate external FFmpeg and FFprobe | Implemented |
| Probe one media file for streams, duration, rates, and chapters | Implemented |
| Select extraction start, duration, FPS, video stream, and audio stream | Implemented |
| Extract timestamped PNG frames | Implemented |
| Generate no audio, PCM WAV, FLAC, or Ogg Vorbis audio | Implemented |
| Apply native, fit, fill, or stretch framing during extraction | Implemented |
| Open a clip package with playback, stepping, scrubbing, and thumbnails | Implemented |
| Open one selected clip frame as a normal Screen Image source | Implemented |
| Convert every frame with one frozen Screen Image recipe | Implemented |
| Apply independent recipe scaling, framing, offsets, and background fill | Implemented |
| Use a bounded multicore frame-conversion pool | Implemented |
| Open converted preview frames as an output monitor | Implemented |
| Convert JSON, CSV/TSV, and Daphne/Hypseus clip mappings | Implemented |
| Load native clip maps from custom Unity and Godot ports | Reference loaders implemented |
| Extract directly from a mapping into multiple clip folders | Planned |
| Resolve global, clip-folder, and per-clip recipe overrides | Planned; global recipe is implemented |
| Treat a folder of VOB files as one ordered source | Planned |
| Navigate authored DVD `VIDEO_TS` titles and program chains | Planned |
| Play synchronized audio in the desktop monitor | Planned; audio metadata is displayed |

The current extractor accepts one local media file per command. A single VOB
file can be used when the installed FFmpeg build can probe and decode it, but
loose VOB ordering and DVD navigation are not inferred automatically.

## FFmpeg requirement

Video and audio probing and extraction use separately installed `ffmpeg` and
`ffprobe` executables. RetroVDP Studio does not currently redistribute them.
Installation instructions for Windows, macOS, Ubuntu/Debian, Fedora, and Arch
Linux are in [FFMPEG_SETUP.md](FFMPEG_SETUP.md).

The desktop can save explicit executable paths under
**Preferences → Media Tools**. If no paths are saved, the application searches
beside the application and then on `PATH`.

Test both tools from the command line:

```shell
retrovdp-cli --check-media-tools
```

Use `--json` for resolved paths, versions, and stable error information. An
explicit pair can be checked with:

```shell
retrovdp-cli --check-media-tools \
  --ffmpeg-path /path/to/ffmpeg \
  --ffprobe-path /path/to/ffprobe --json
```

Finding the applications does not guarantee every codec, filter, or DVD
feature. Those capabilities depend on the selected FFmpeg build.

## Probe a source

Probe a media file before choosing streams or extraction settings:

```shell
retrovdp-cli --probe-media source.mp4 --json
```

The result includes the normalized duration, video and audio streams, exact
rational frame rates, time bases, dimensions, codecs, language metadata, and
chapters reported by FFprobe. Use the global stream indexes from this result
with `--video-stream` and `--audio-stream`.

An explicit FFprobe executable can be selected with `--ffprobe-path`.

## Extract a clip package

This example extracts three seconds beginning five seconds into the source,
samples at 15 FPS, fits the frames into a 320×240 canvas, and creates PCM WAV
audio:

```shell
retrovdp-cli --extract-media source.mp4 --output clip-one \
  --start 5 --duration 3 --fps 15 \
  --extract-sizing fit --extract-width 320 --extract-height 240 \
  --audio wav --json
```

Important options are:

| Option | Meaning |
| --- | --- |
| `--start <seconds>` | Start time; defaults to zero |
| `--duration <seconds>` | Clip duration; omitted means the remaining source |
| `--fps <rate>` | Decimal or rational sampling rate, such as `15` or `30000/1001`; omitted uses the source rate |
| `--video-stream <index>` | Global FFprobe video-stream index |
| `--audio-stream <index>` | Global FFprobe audio-stream index |
| `--extract-sizing native` | Keep decoded frame dimensions |
| `--extract-sizing fit` | Preserve aspect ratio and letterbox inside the requested canvas |
| `--extract-sizing fill` | Preserve aspect ratio and crop to fill the requested canvas |
| `--extract-sizing stretch` | Scale directly to the requested canvas |
| `--extract-width`, `--extract-height` | Required together for `fit`, `fill`, and `stretch` |
| `--audio none` | Do not create an audio clip; this is the default |
| `--audio wav` | Generate PCM 16-bit WAV |
| `--audio flac` | Generate FLAC |
| `--audio ogg` | Generate Ogg Vorbis |

The destination must not already exist. Extraction is written to a sibling
staging directory and renamed to the requested destination only after all
frames, optional audio, and metadata have completed successfully.

## Source clip layout

A completed source package has this form:

```text
clip-one/
  clip.json
  frames/
    frame_000001.png
    frame_000002.png
    ...
  audio/
    source.wav
```

The `audio` directory is omitted when audio generation is disabled. `clip.json`
records:

- source path, timeline identity, and detected container format;
- selected start and duration in integer microseconds;
- exact extraction FPS and sizing mode;
- requested extraction width and height when scaled;
- every frame's zero-based ordinal, one-based number, relative file path,
  presentation timestamp, and duration; and
- optional audio path, format, source stream, start, and duration.

All frame and audio paths referenced by a package are relative and must remain
inside the package directory. The desktop validates the schema, timing,
bounds, and every referenced file before accepting the package.

## Desktop source monitor

Choose **File → Open Media Clip** and select the package's `clip.json` file.
The source monitor provides:

- timestamp-driven play and pause;
- first, previous, next, and last frame controls;
- a frame-position scrubber;
- a virtualized thumbnail filmstrip; and
- **Open Frame as Source**, which sends the selected PNG to the normal Screen
  Image workflow.

Playback follows stored presentation timestamps instead of assuming constant
frame spacing. Audio metadata is displayed, but the current monitor does not
play synchronized sound.

## Convert all frames with a recipe

Save the desired Screen Image settings as a recipe, then convert the source
package:

```shell
retrovdp-cli --convert-media-clip clip-one/clip.json \
  --recipe target.rvdp.json --output target-run \
  --conversion-workers auto --json
```

The recipe freezes the target, mode, palette and dithering settings, export
format, scaling filter, fit/fill/stretch behavior, offsets, background fill,
gamma, and other Screen Image conversion settings. Every frame uses the same
snapshot even if application settings change during the run.

In the desktop source monitor, **Convert Clip** asks for a saved Screen Image
recipe and an output parent directory. Conversion runs in background work and
opens the completed preview package as an **Output Monitor**.

The conversion destination must not already exist. Like extraction,
conversion uses a staging directory and commits the completed run atomically.

## Converted output layout

The completed target run has this form:

```text
target-run/
  clip.json
  run.json
  recipe.rvdp.json
  previews/
    frame_000001.png
    frame_000002.png
  native/
    frame_000001/
      frame_000001.TIAP
      frame_000001.TIAC
    frame_000002/
      frame_000002.TIAP
      frame_000002.TIAC
```

The actual native files depend on the selected target, mode, and export
format. Media-package frame base names are normalized consistently across
case-sensitive and case-insensitive filesystems.

`run.json` records the source package, recipe snapshot and SHA-256 digest,
resolved worker count, target, mode, export format, ordered frame outputs, and
warnings. The generated `clip.json` references the preview PNGs and can be
opened by the same monitor without interpreting target-specific native files.

## Parallel conversion

The conversion worker setting controls frame-level conversion, not FFmpeg
process count. FFmpeg extracts the source package before target conversion;
the bounded worker pool then processes those durable image frames.

`--conversion-workers auto` uses a conservative policy: it leaves one logical
processor available when possible, caps the automatic setting at eight, and
never creates more workers than there are frames. An explicit value can range
from `1` through the detected logical-processor count.

The desktop setting is under **Preferences → Behavior**. The resolved value is
written to `run.json` as `conversion.workers`. Sequential and parallel runs
are required to produce byte-identical native outputs and ordered metadata.

## Scaling and framing

Extraction framing and target conversion framing are independent:

1. Extraction decides the durable PNG frame geometry using `native`, `fit`,
   `fill`, or `stretch`.
2. The recipe independently prepares each PNG for the target display geometry
   using its saved scaling filter, framing mode, offsets, background fill, and
   target-specific rules.

Keeping extraction at native dimensions preserves the most source information.
Pre-scaling during extraction can reduce storage and conversion work, but it is
an intentional first transform followed by the recipe's target transform.

## Clip mappings and emulator ports

Clip-map conversion is independent of FFmpeg. RetroVDP Studio supports:

- versioned native JSON;
- CSV and TSV clip lists;
- Daphne and Hypseus framefiles; and
- the same native JSON contract through reference Unity and Godot 4 loaders.

Convert a Daphne/Hypseus framefile while attaching its external frame rate:

```shell
retrovdp-cli --convert-clip-map framefile.txt \
  --clip-map-input-format daphne --mapping-fps 30000/1001 \
  --clip-map-output-format json --output clips.json --json
```

The native schema supports signed logical frame numbers, source and audio
paths, optional end frames, default and per-entry recipe references, and
default and per-entry target references. Reference loaders are available at:

- [`examples/integrations/RetroVdpClipMap.cs`](../examples/integrations/RetroVdpClipMap.cs)
  for Unity; and
- [`examples/integrations/retro_vdp_clip_map.gd`](../examples/integrations/retro_vdp_clip_map.gd)
  for Godot 4.

Mapping conversion currently validates and translates metadata only. It does
not yet probe each mapping source, expand entries into clip-package folders,
or execute referenced recipes. See [CLIP_MAPS.md](CLIP_MAPS.md) for the full
schema, path resolution, and lossless/lossy adapter rules.

## VOB and DVD sources

The current single-file workflow may probe and extract an individual `.VOB`
file if the installed FFmpeg build supports its streams. It does not yet:

- concatenate a folder of loose VOB files;
- infer the correct ordering of VOB segments;
- treat a `VIDEO_TS` directory as a DVD title; or
- select DVD titles, program chains, chapters, angles, or authored playback
  order.

A DVD structure is not equivalent to a lexically sorted group of VOB files.
Future DVD support will require FFmpeg's `dvdvideo` demuxer and a build with
the corresponding DVD navigation libraries.

## Limits and failure behavior

- Clip metadata is bounded and a package may contain at most 100,000 frames.
- Frame and audio paths that are absolute, escape the package, or reference
  missing files are rejected.
- Extraction and conversion reject existing destination directories instead
  of merging or silently overwriting them.
- Conversion stops on the first failed frame and does not commit a partial run
  as complete.
- Raw user-provided FFmpeg argument strings are not accepted as recipe data.
- Subtitles, transitions, nonlinear editing, direct video-file output, and
  target-machine playback-program generation are not part of this feature.

For CLI diagnostics and exit codes, see [COMMAND_LINE.md](COMMAND_LINE.md).
For architecture, future batch expansion, cancellation, and palette policies,
see [BATCH_MODE.md](BATCH_MODE.md).
