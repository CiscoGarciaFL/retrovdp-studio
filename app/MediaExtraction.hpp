#pragma once

#include "retrovdp/media/MediaTimeline.hpp"

#include <QString>

#include <cstddef>
#include <optional>

namespace retrovdp::appsupport {

enum class ExtractionSizingMode {
    Native,
    Fit,
    Fill,
    Stretch,
};

enum class AudioExtractionFormat {
    None,
    Wav,
    Flac,
    OggVorbis,
};

struct MediaExtractionRequest {
    QString sourcePath;
    QString destinationDirectory;
    QString ffmpegExecutable;
    const media::MediaTimeline* timeline{};
    std::int64_t startMicroseconds{};
    std::optional<std::int64_t> durationMicroseconds;
    media::Rational framesPerSecond{};
    int videoStreamIndex{-1};
    std::optional<int> audioStreamIndex;
    ExtractionSizingMode sizingMode{ExtractionSizingMode::Native};
    std::uint32_t outputWidth{};
    std::uint32_t outputHeight{};
    AudioExtractionFormat audioFormat{AudioExtractionFormat::None};
};

struct MediaExtractionLimits {
    std::size_t maximumFrames{100'000};
    std::size_t maximumProcessOutputBytes{1024U * 1024U};
    int timeoutMilliseconds{10 * 60 * 1000};
};

struct MediaExtractionResult {
    QString outputDirectory;
    QString manifestPath;
    QString audioPath;
    std::size_t frameCount{};
    QString errorCode;
    QString error;

    [[nodiscard]] explicit operator bool() const
    {
        return errorCode.isEmpty() && !manifestPath.isEmpty();
    }
};

[[nodiscard]] MediaExtractionResult extractMediaClip(
    const MediaExtractionRequest& request,
    const MediaExtractionLimits& limits = {});

} // namespace retrovdp::appsupport
