#pragma once

#include <QString>

namespace retrovdp::appsupport {

struct MediaExecutableInfo {
    QString name;
    QString requestedPath;
    QString resolvedPath;
    QString version;
    QString error;
    bool available{};
};

struct MediaToolStatus {
    MediaExecutableInfo ffmpeg;
    MediaExecutableInfo ffprobe;

    [[nodiscard]] bool ready() const
    {
        return ffmpeg.available && ffprobe.available;
    }
};

// Probe one supported media executable. Name must be "ffmpeg" or "ffprobe".
[[nodiscard]] MediaExecutableInfo probeMediaExecutable(
    const QString& name,
    const QString& executableOverride = {},
    int timeoutMilliseconds = 3000);

// Resolve an explicit executable override, an executable beside the running
// application, or an executable available through PATH, in that order. Each
// tool is launched directly with QProcess and must identify itself through
// its version output before it is considered usable.
[[nodiscard]] MediaToolStatus probeMediaTools(
    const QString& ffmpegOverride = {},
    const QString& ffprobeOverride = {},
    int timeoutMilliseconds = 3000);

} // namespace retrovdp::appsupport
