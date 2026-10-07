# FFmpeg setup for media features

RetroVDP Studio uses the separately installed `ffmpeg` and `ffprobe`
executables for video and audio inspection and processing. The application
does not currently distribute either executable.

After installation, verify both tools from a terminal:

```shell
ffmpeg -version
ffprobe -version
```

RetroVDP Studio searches first for tools beside the application and then on
the process `PATH`. Explicit executable paths can be saved under
**Preferences → Media Tools**. The same check is available without opening the
desktop interface:

```shell
retrovdp-cli --check-media-tools
```

Add `--json` for a machine-readable result. Use `--ffmpeg-path` and
`--ffprobe-path` to validate explicit executables.

Once both tools pass, `retrovdp-cli --probe-media <file>` inspects a source and
`retrovdp-cli --extract-media <file> --output <directory>` creates the first
supported frame/audio clip-package form. See [COMMAND_LINE.md](COMMAND_LINE.md)
for extraction ranges, FPS, scaling/framing, streams, and audio formats.

## Windows

FFmpeg publishes source code and links to current Windows executable builds on
its [official download page](https://ffmpeg.org/download.html). Install one of
the linked builds, keep `ffmpeg.exe` and `ffprobe.exe` from the same build
together, and either add their `bin` directory to `PATH` or enter both paths
in RetroVDP Studio preferences.

Package-manager installations are also suitable. Confirm the package contains
both executables and restart RetroVDP Studio after changing `PATH`.

## macOS

The Homebrew package installs both tools:

```shell
brew install ffmpeg
```

The formula and supported package variants are documented by
[Homebrew](https://formulae.brew.sh/formula/ffmpeg).

## Ubuntu and Debian

Install the distribution package:

```shell
sudo apt update
sudo apt install ffmpeg
```

Ubuntu publishes `ffmpeg` in its official package archive. Distribution
releases may ship different FFmpeg versions and optional capabilities.

## Fedora and related distributions

Use the FFmpeg package supplied by the configured distribution repositories.
Package names and codec coverage vary between the base distribution and
third-party multimedia repositories. After installation, run both version
checks above rather than assuming the package includes `ffprobe`.

## Arch Linux

Install the official package:

```shell
sudo pacman -S ffmpeg
```

The Arch package includes both `/usr/bin/ffmpeg` and `/usr/bin/ffprobe`.

## Capability notes

Finding the executables proves that RetroVDP Studio can launch them, but
individual builds differ in enabled codecs, filters, and external libraries.
Later media operations perform their own capability checks and report a
specific missing demuxer, decoder, encoder, or filter.

In particular, direct DVD `VIDEO_TS` navigation requires FFmpeg's `dvdvideo`
demuxer and a build configured with `libdvdnav` and `libdvdread`. A single VOB
file or an explicitly ordered loose-VOB set does not imply that the installed
build can interpret DVD titles, chapters, program chains, or angles.
