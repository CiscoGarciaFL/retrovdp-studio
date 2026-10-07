#pragma once

#include "retrovdp/media/MediaTimeline.hpp"

#include <QString>

#include <cstddef>
#include <optional>

namespace retrovdp::appsupport {

struct MediaProbeLimits {
    std::size_t maximumJsonBytes{16U * 1024U * 1024U};
    int maximumStreams{64};
    int maximumChapters{10'000};
    int timeoutMilliseconds{15'000};
};

struct MediaProbeResult {
    std::optional<media::MediaTimeline> timeline;
    QString errorCode;
    QString error;

    [[nodiscard]] explicit operator bool() const { return timeline.has_value(); }
};

// Inspect one local media file with an already validated FFprobe executable.
// The process output, stream count, chapter count, and wall time are bounded.
[[nodiscard]] MediaProbeResult probeMediaFile(
    const QString& sourcePath,
    const QString& ffprobeExecutable,
    const MediaProbeLimits& limits = {});

} // namespace retrovdp::appsupport
