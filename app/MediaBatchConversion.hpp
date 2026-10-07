#pragma once

#include <QString>

#include <cstddef>

namespace retrovdp::appsupport {

struct MediaBatchConversionRequest {
    QString clipManifestPath;
    QString recipePath;
    QString destinationDirectory;
    // Zero selects the conservative automatic policy. Positive values are
    // clamped to the available logical processor count and frame count.
    int conversionWorkers{};
};

struct MediaBatchConversionLimits {
    std::size_t maximumFrames{100'000};
};

struct MediaBatchConversionResult {
    QString outputDirectory;
    QString runManifestPath;
    QString monitorManifestPath;
    std::size_t frameCount{};
    int workersUsed{1};
    QString errorCode;
    QString error;

    [[nodiscard]] explicit operator bool() const
    {
        return errorCode.isEmpty() && !runManifestPath.isEmpty();
    }
};

[[nodiscard]] int availableConversionWorkers();
[[nodiscard]] int automaticConversionWorkers();
[[nodiscard]] int resolvedConversionWorkers(int requestedWorkers,
                                            std::size_t frameCount);

[[nodiscard]] MediaBatchConversionResult convertMediaClip(
    const MediaBatchConversionRequest& request,
    const MediaBatchConversionLimits& limits = {});

} // namespace retrovdp::appsupport
