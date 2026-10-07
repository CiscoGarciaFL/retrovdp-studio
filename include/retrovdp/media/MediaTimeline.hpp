#pragma once

#include "retrovdp/core/StableId.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace retrovdp::media {

struct MediaTimelineIdTag;
struct ClipIdTag;

using MediaTimelineId = core::BasicStableId<MediaTimelineIdTag>;
using ClipId = core::BasicStableId<ClipIdTag>;

struct Rational {
    std::int64_t numerator{};
    std::int64_t denominator{1};

    [[nodiscard]] friend bool operator==(const Rational&, const Rational&) = default;
};

[[nodiscard]] bool isValidPositiveRational(Rational value);
[[nodiscard]] Rational normalizedRational(Rational value);

enum class MediaSourceKind : std::uint8_t {
    SingleFile,
    OrderedSegments,
    DvdTitle,
    ImageSequence,
};

struct VideoStreamDescriptor {
    int index{-1};
    std::string codecName;
    std::uint32_t width{};
    std::uint32_t height{};
    std::string pixelFormat;
    std::string fieldOrder;
    std::string colorRange;
    std::string colorSpace;
    std::string colorTransfer;
    std::string colorPrimaries;
    Rational sampleAspectRatio{1, 1};
    Rational averageFrameRate{};
    Rational nativeFrameRate{};
    Rational timeBase{};
    std::int64_t startMicroseconds{};
    std::int64_t durationMicroseconds{};
    std::optional<std::int64_t> frameCount;
    bool defaultStream{};
};

struct AudioStreamDescriptor {
    int index{-1};
    std::string codecName;
    std::uint32_t sampleRate{};
    std::uint32_t channels{};
    std::string channelLayout;
    std::string sampleFormat;
    std::string language;
    Rational timeBase{};
    std::int64_t startMicroseconds{};
    std::int64_t durationMicroseconds{};
    bool defaultStream{};
};

struct MediaChapter {
    int id{-1};
    std::int64_t startMicroseconds{};
    std::int64_t endMicroseconds{};
    std::string title;
};

struct MediaSegment {
    std::string sourceLocator;
    std::int64_t timelineStartMicroseconds{};
    std::int64_t sourceStartMicroseconds{};
    std::int64_t durationMicroseconds{};
};

struct MediaTimeline {
    MediaTimelineId id;
    MediaSourceKind sourceKind{MediaSourceKind::SingleFile};
    std::string formatName;
    std::int64_t durationMicroseconds{};
    std::vector<MediaSegment> segments;
    std::vector<VideoStreamDescriptor> videoStreams;
    std::vector<AudioStreamDescriptor> audioStreams;
    std::vector<MediaChapter> chapters;
};

struct ClipDefinition {
    ClipId id;
    MediaTimelineId timelineId;
    std::int64_t startMicroseconds{};
    std::int64_t durationMicroseconds{};
    std::optional<std::int64_t> sourceStartFrame;
    std::optional<std::int64_t> sourceEndFrame;
    Rational sourceFrameRate{};
    int videoStreamIndex{-1};
    std::optional<int> audioStreamIndex;
};

struct MediaValidationIssue {
    std::string field;
    std::string message;
};

[[nodiscard]] std::vector<MediaValidationIssue>
validate(const MediaTimeline& timeline);

[[nodiscard]] std::vector<MediaValidationIssue>
validate(const ClipDefinition& clip, const MediaTimeline& timeline);

} // namespace retrovdp::media
