# Media sequences: video, audio, clip mapping, and batch conversion

## Status and intent

This document is the product contract for the active media-sequence program.
Implementation ordering is owned by [../PROJECT_PLAN.md](../PROJECT_PLAN.md).
The delivered foundation discovers and validates separately installed
`ffmpeg` and `ffprobe` executables, probes a single media file into the portable
timeline model, and extracts an atomic clip package containing timestamped PNG
frames, optional WAV/FLAC/Ogg audio, and `clip.json`. Mapping import/export and
target conversion are implemented; desktop extraction, mapping-driven batch
expansion, and audio playback remain planned until their corresponding
delivery gates are complete. The desktop can load the
generated package into a timestamp-driven source monitor with a virtualized
thumbnail filmstrip, frame stepping/scrubbing, and selected-frame handoff to
the existing Screen Image conversion workflow. A source clip can also be
converted with one frozen Screen Image recipe: the batch implementation writes
native target files, uniform target-faithful preview PNGs, a recipe snapshot
and digest, `run.json`, and an output-monitor `clip.json` atomically. Frame
conversion supports bounded, deterministic parallel workers; FFmpeg remains
outside this worker pool.

Batch Mode will turn a time-ordered or explicitly ordered source into an
enumerated sequence of still frames, then apply one snapshot of the normal
RetroVDP Studio compile settings to each frame. The same compiler, validator,
target modes, and exporters used for a single image must be used for every
batch item.

The goal is deterministic, synchronized clip-package production rather than a
general nonlinear video editor. A package can contain source frames, selected
or generated audio, target-native frame outputs, target-faithful preview
frames, and the metadata required to reproduce their timing and transforms.

## Supported source families

The design must accommodate these source families through a common frame
enumerator:

1. **Video files** — containers and codecs supported by the selected FFmpeg
   executables. Likely examples include MP4, MOV, AVI, MKV, WebM, MPEG, WMV,
   and individual VOB files, but support is capability-probed rather than
   inferred from an extension.
2. **Animated images** — animated GIF first, followed by animated WebP and APNG
   when the installed decoder exposes their frames and timing reliably.
3. **Slideshow folders** — a directory of still images ordered by natural
   filename order, an explicit file list, or a small manifest that specifies
   order and duration.
4. **Numbered image sequences** — files such as `scene_0001.png` through
   `scene_0240.png`, including sequences with missing numbers when the user
   chooses to permit gaps.
5. **Selected still images** — an explicitly ordered multi-selection, useful
   for converting artwork sets without creating a slideshow manifest first.
6. **Ordered media segments** — an explicit sequence of compatible files,
   including loose VOB sets, represented as one virtual timeline while
   retaining physical segment boundaries.
7. **DVD structures** — a `VIDEO_TS` directory, disc image, or readable device
   when the installed FFmpeg exposes its `dvdvideo` demuxer. The user selects
   a title or explicit program-chain coordinates, chapter range, and angle.

Subtitle rendering and preservation remain deferred. Chapter, title,
program-chain, angle, and audio-stream information is retained when it affects
source selection or synchronization.

## Synchronized media timeline

Every source normalizes into a virtual `MediaTimeline`. It owns one video
timeline, zero or more audio streams, physical source segments, and imported
clip mappings. Time is represented by integer or rational values; floating
point FPS is presentation-only.

A `ClipDefinition` records a stable clip ID, source timeline, original mapping
coordinates, normalized in/out range, exact timebase, stream selections,
extraction transform, audio policy, conversion-recipe reference, optional
per-clip override, loop/lead metadata, and importer-specific fields needed for
lossless round trips.

For an ordered folder, segment boundaries are explicit. A clip may cross a
boundary only after the input streams pass compatibility checks. A DVD folder
is not a loose VOB sequence: playback order comes from IFO program chains, so
the application must never concatenate every VOB by lexical order.

## Audio model

Audio generation is selectable per job and overridable per clip:

- none;
- copy the selected source stream when the output contract permits it;
- PCM WAV;
- FLAC;
- Ogg Vorbis for Daphne/Hypseus-compatible packages; or
- a later registered engine-specific encoding profile.

Settings include stream/language selection, sample rate, channel layout,
codec quality, trim range, synchronization offset, optional padding and
fades, and optional peak or loudness normalization. Normalization is off by
default. The run manifest records video and audio ranges independently and
never hides an applied offset.

When audio is present during playback, its clock is authoritative. Source and
output monitors select one corresponding audio stream at a time.

## Extraction and conversion transforms

Extraction and target conversion each own a complete, explicit transform.
Extraction may deinterlace, rotate, crop, scale, correct aspect, pad, sample
FPS, and normalize pixel/color format before durable frames are written.
Conversion may independently crop, fit/fill/stretch, scale to target geometry,
apply pixel aspect, offset, and fill before palette and target compilation.

The clip manifest records original decoded geometry, extracted geometry, and
target geometry. Native extraction followed by target-side framing is the
default; pre-scaling is an intentional storage/performance choice and must not
silently enable a second transform.

## Clip mapping adapters

The initial adapters normalize into the portable `ClipMapDocument` model,
which retains signed logical frame coordinates and declarative recipe/target
references until timeline probing can produce `MediaTimeline` and
`ClipDefinition` values. Implemented adapters are:

1. RetroVDP versioned JSON clip maps;
2. Daphne/Hypseus framefiles with relative media paths, logical frame starts,
   MPEG-2 segments, and optional matching Ogg audio;
3. CSV/TSV clip lists and numbered-sequence manifests; and
4. compact JSON exports with reference C# and GDScript loaders for custom
   Unity and Godot ports.

The exact version-1 fields, resolution rules, compatibility limits, CLI, and
engine loaders are documented in [CLIP_MAPS.md](CLIP_MAPS.md). Arbitrary Singe
Lua is not executed or interpreted as a manifest. Specific
declarative formats can receive adapters after representative fixtures define
their real behavior. Preservation of unknown adapter-specific metadata remains
a later schema extension; version-1 conversions preserve the documented
native fields.

## Common frame model

Every source is normalized into a stream of `FrameRecord` values before
conversion. The eventual implementation should keep the public model
independent of FFmpeg, Qt Multimedia, or any other decoder API.

Each frame record needs at least:

- a zero-based internal ordinal;
- a one-based display/export number;
- the source file and source-stream identity;
- the original frame index when the decoder provides one;
- presentation timestamp and duration when meaningful;
- decoded width, height, pixel format, color-space information, and alpha;
- a bounded `RgbImage` containing the normalized pixels;
- warnings generated while decoding or normalizing the frame.

Video and animated-image frames use their source timing. Slideshow frames use
the manifest duration or a user-selected default. Numbered still sequences may
omit timing when they are intended only as ordered conversion jobs.

Frame enumeration must be stable. Given the same source, source options, and
decoder version, frame numbers and output names must not depend on thread
scheduling or completion order.

## Source range and sampling

Before conversion, the user can restrict what is enumerated by:

- first and last frame;
- start and end timestamp for timed media;
- every Nth source frame;
- a target sampling rate such as 10, 15, 24, or 30 frames per second;
- a maximum output-frame count;
- duplicate-frame suppression, which is off by default because it changes the
  relationship between frame numbers and source timing.

Sampling must use presentation timestamps rather than assuming that all video
is constant-frame-rate. Variable-frame-rate input retains an explicit timing
table in the batch manifest.

## Conversion-settings snapshot

Starting a batch freezes a complete copy of the active conversion state:

- conversion and dither modes;
- perceptual matching, error distribution, gamma, histogram, and color-shift
  settings;
- framing, scaling filter, offsets, and background fill;
- editable working palette and F18A palette-selection options;
- PowerPaint framing and export-format selection.

Edits made in the main window after the batch starts do not alter an in-flight
job. A future job editor may support per-frame overrides, but those are outside
the first Batch Mode release.

All frames are independently converted with this settings snapshot. This makes
the initial implementation parallelizable and keeps it aligned with one-shot
GUI and CLI conversions.

The snapshot is resolved in this order: batch/global recipe, clip-folder
recipe, then explicit per-clip override. The final typed snapshot and digest
are stored in the run manifest. Arbitrary JSON merge and untrusted raw FFmpeg
arguments are not recipe semantics.

The implemented first conversion slice accepts the global recipe only. Folder
and per-clip override resolution remain part of batch hardening.

### Palette stability

Adaptive F18A palettes can visibly change between adjacent frames even when
the source changes only slightly. Batch Mode therefore needs an explicit
palette policy:

- **Independent** — select the best palette for every frame. This is the
  simplest policy and the initial default, but it may shimmer during playback.
- **Lock to first frame** — select once from the first enumerated frame and use
  that palette throughout the batch.
- **Lock to reference frame** — select from a user-chosen frame.
- **Sequence palette** — select one palette from bounded samples across the
  sequence. This requires a separately specified and tested sampling
  algorithm and may be deferred.

The ordinary editable 9918A working palette is already stable because the
settings snapshot contains its colors.

## Output layout and naming

The default output is a new batch directory. It contains a machine-readable
batch manifest and one directory per enumerated frame:

```text
output-name/
  clip.json
  frames/
    frame_000001.png
    frame_000002.png
  audio/
    source.ogg
  recipes/
    extraction.json
    conversion.rvdp.json
  outputs/
    target-run/
      run.json
      previews/
        frame_000001.png
      native/
        frame_000001/
          frame_000001.TIAP
          frame_000001.TIAC
```

The actual files inside each frame directory follow the selected existing
export format and its applicability rules. A converted-preview PNG is optional
unless PNG itself is the chosen export. Padding width is based on the final
enumerated-frame count, with six digits as the minimum so lexical order equals
frame order.

The batch manifest records:

- application and manifest-schema versions;
- source identity and source options;
- a digest of the frozen conversion settings;
- palette policy;
- requested export format;
- every output frame's ordinal, source index, timestamp, duration, status,
  warnings, and generated files;
- enough timing information for a later playback or packaging tool to
  reconstruct the intended sequence.

No output filename may be derived directly from untrusted container metadata.
Names are sanitized using the same cross-platform policy as ordinary exports.

## Processing model

Enumeration and conversion form a bounded streaming pipeline:

```text
source reader → frame normalizer → bounded queue → conversion workers → writer
```

The decoder must not load an entire movie into memory. Queue depth, decoded
dimensions, encoded source size where knowable, frame count, and aggregate
output estimates all require configurable limits. The configured bounded
conversion workers run in parallel, while the writer and manifest commit
results in enumeration order.

Live preview is optional. When enabled, the Batch view shows the most recently
selected or most recently completed frame and may reuse the progressive-row
preview callback. Disabling it must avoid intermediate PNG encoding just as it
does for one-shot conversion.

Cancellation stops decoding new frames, requests cancellation from active
conversions, finishes or removes temporary writes safely, and records an
incomplete batch state. A resume operation may skip a frame only after its
manifest entry, settings digest, and output digests have been verified.

## Desktop workflow

The proposed entry point is **File → Batch Mode…**. A dedicated Batch view or
dialog should provide:

1. source type and source selection;
2. detected stream/frame/timing summary;
3. frame range and sampling controls;
4. output folder and naming preview;
5. conversion-settings summary with an explicit “Use current settings” action;
6. palette-stability policy where applicable;
7. estimated frame count, output size, and overwrite conflicts;
8. Start, Pause/Resume where supported, and Cancel actions;
9. progress for enumeration, conversion, and writing, including the current
   frame number and failures.

The user should be able to inspect several enumerated source frames before
starting. Batch Mode must not silently change the main window's active source
or conversion settings.

## Command-line direction

The headless interface should eventually expose the same feature without
inventing a second pipeline. A possible shape is:

```text
retrovdp-cli batch --input movie.mp4 --output converted-frames \
  --mode bitmap-9918a --format tifiles --start 00:00:05 --fps 15
```

The implemented single-file extraction and clip-map conversion syntax is
documented in [COMMAND_LINE.md](COMMAND_LINE.md). The broader batch syntax
remains deferred until mapping-driven extraction and recipe resolution are
implemented.
Machine-readable progress and the final batch manifest are required for
automation. The CLI must never prompt for overwrite or decoder choices.

## Errors and continuation policy

The default policy is **stop on the first failed frame**. An explicit
continue-on-error option may record failed frames and proceed, but it must
preserve their numbers so later frames do not shift. Decoder warnings,
conversion diagnostics, and write failures remain distinguishable in the
manifest.

Preflight checks occur before the first committed output where possible:

- source readability and stream selection;
- range and sampling validity;
- conversion-settings validation;
- export-format applicability;
- destination writability and overwrite conflicts;
- conservative disk-space estimate.

## Dependency and packaging decision

The accepted first backend is a separately installed FFmpeg subprocess pair:
`ffprobe` inspects sources and `ffmpeg` extracts or encodes media. Qt launches
each executable directly with an argument list; commands never pass through a
shell. Resolution order is an explicit preference, an executable beside the
application, then `PATH`.

The application initially does not redistribute FFmpeg. If packaging later
bundles a build, its exact license configuration, codec set, update policy,
binary size, and platform coverage require a release review.

Deterministic software decoding is preferred for golden tests. Hardware
decoding may be offered later only if pixel differences are documented and do
not affect reproducible batch output unexpectedly.

## Acceptance criteria

- The same frame source produces stable enumeration and names.
- Animated GIF timing, disposal, transparency, and frame composition are
  correct rather than treating frames as independent rectangles.
- Variable-frame-rate video sampling follows presentation timestamps.
- Generated audio has a manifest-recorded range, format, and synchronization
  relationship to the clip video.
- Extraction and conversion transforms can be enabled independently and their
  three geometries round-trip through the manifest.
- Daphne/Hypseus frame mappings retain exact logical frame numbers and
  rational timebases.
- Loose VOB sequences retain segment boundaries; DVD structures use authored
  title/program-chain order when supported.
- Slideshow natural ordering is defined and tested across platforms.
- Every frame uses the frozen conversion-settings snapshot and existing core.
- Sequential and parallel runs produce byte-identical ordered outputs.
- Cancellation cannot leave a file reported as complete when it is partial.
- Resume verifies settings and file digests before skipping work.
- Malformed, oversized, or extremely long media fails within configured
  limits.
- A batch containing one still image matches ordinary one-shot conversion.
- Windows, Linux, Intel Mac, and Apple Silicon packages expose equivalent
  decoding behavior for the formats claimed by that release.

## Deliberately deferred features

- subtitle rendering;
- timeline editing, transitions, titles, or effects;
- direct AVI/MP4/MOV output;
- automatic generation of target-machine playback programs;
- inter-frame compression or delta encoding for 9918A/F18A data;
- live capture from cameras or screens;
- distributed conversion across multiple machines.

Those may build on the ordered frame and timing manifest later without being
part of the initial Batch Mode contract.
