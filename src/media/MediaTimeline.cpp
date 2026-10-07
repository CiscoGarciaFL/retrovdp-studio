#include "retrovdp/media/MediaTimeline.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace retrovdp::media {
namespace {

constexpr std::uint32_t maximumDimension = 65'535;

void addIssue(std::vector<MediaValidationIssue>& issues,
              std::string field,
              std::string message)
{
    issues.push_back({std::move(field), std::move(message)});
}

bool containsVideoStream(const MediaTimeline& timeline, int index)
{
    return std::ranges::any_of(timeline.videoStreams,
                               [index](const auto& stream) {
                                   return stream.index == index;
                               });
}

bool containsAudioStream(const MediaTimeline& timeline, int index)
{
    return std::ranges::any_of(timeline.audioStreams,
                               [index](const auto& stream) {
                                   return stream.index == index;
                               });
}

} // namespace

bool isValidPositiveRational(Rational value)
{
    return value.numerator > 0 && value.denominator > 0;
}

Rational normalizedRational(Rational value)
{
    if (value.denominator == 0) return value;
    if (value.numerator == std::numeric_limits<std::int64_t>::min()
        || value.denominator == std::numeric_limits<std::int64_t>::min()) {
        return value;
    }
    if (value.denominator < 0) {
        value.numerator = -value.numerator;
        value.denominator = -value.denominator;
    }
    const auto divisor = std::gcd(std::abs(value.numerator), value.denominator);
    if (divisor > 1) {
        value.numerator /= divisor;
        value.denominator /= divisor;
    }
    return value;
}

std::vector<MediaValidationIssue> validate(const MediaTimeline& timeline)
{
    std::vector<MediaValidationIssue> issues;
    if (timeline.durationMicroseconds < 0) {
        addIssue(issues, "duration", "Timeline duration cannot be negative.");
    }
    if (timeline.segments.empty()) {
        addIssue(issues, "segments", "A media timeline requires at least one source segment.");
    }
    if (timeline.videoStreams.empty() && timeline.audioStreams.empty()) {
        addIssue(issues, "streams", "A media timeline requires a video or audio stream.");
    }

    std::int64_t previousEnd{};
    for (std::size_t index = 0; index < timeline.segments.size(); ++index) {
        const auto& segment = timeline.segments[index];
        const std::string prefix = "segments[" + std::to_string(index) + "]";
        if (segment.sourceLocator.empty()) {
            addIssue(issues, prefix + ".source", "Source locator cannot be empty.");
        }
        if (segment.timelineStartMicroseconds < 0
            || segment.sourceStartMicroseconds < 0) {
            addIssue(issues, prefix + ".start", "Segment starts cannot be negative.");
        }
        if (segment.durationMicroseconds <= 0) {
            addIssue(issues, prefix + ".duration", "Segment duration must be positive.");
        }
        if (index > 0 && segment.timelineStartMicroseconds < previousEnd) {
            addIssue(issues, prefix + ".start", "Timeline segments cannot overlap.");
        }
        if (segment.durationMicroseconds > 0
            && segment.timelineStartMicroseconds
                <= std::numeric_limits<std::int64_t>::max()
                    - segment.durationMicroseconds) {
            previousEnd = segment.timelineStartMicroseconds
                + segment.durationMicroseconds;
        }
    }

    std::unordered_set<int> streamIndexes;
    for (std::size_t index = 0; index < timeline.videoStreams.size(); ++index) {
        const auto& stream = timeline.videoStreams[index];
        const std::string prefix = "videoStreams[" + std::to_string(index) + "]";
        if (stream.index < 0 || !streamIndexes.insert(stream.index).second) {
            addIssue(issues, prefix + ".index", "Stream index must be unique and non-negative.");
        }
        if (stream.width == 0 || stream.height == 0
            || stream.width > maximumDimension || stream.height > maximumDimension) {
            addIssue(issues, prefix + ".geometry", "Video geometry is invalid or unsupported.");
        }
        if (!isValidPositiveRational(stream.timeBase)) {
            addIssue(issues, prefix + ".timeBase", "Video timebase must be positive.");
        }
        if (stream.durationMicroseconds < 0) {
            addIssue(issues, prefix + ".duration", "Video duration cannot be negative.");
        }
        if (stream.frameCount && *stream.frameCount < 0) {
            addIssue(issues, prefix + ".frameCount", "Frame count cannot be negative.");
        }
    }
    for (std::size_t index = 0; index < timeline.audioStreams.size(); ++index) {
        const auto& stream = timeline.audioStreams[index];
        const std::string prefix = "audioStreams[" + std::to_string(index) + "]";
        if (stream.index < 0 || !streamIndexes.insert(stream.index).second) {
            addIssue(issues, prefix + ".index", "Stream index must be unique and non-negative.");
        }
        if (stream.sampleRate == 0 || stream.channels == 0) {
            addIssue(issues, prefix + ".format", "Audio sample rate and channel count must be positive.");
        }
        if (!isValidPositiveRational(stream.timeBase)) {
            addIssue(issues, prefix + ".timeBase", "Audio timebase must be positive.");
        }
        if (stream.durationMicroseconds < 0) {
            addIssue(issues, prefix + ".duration", "Audio duration cannot be negative.");
        }
    }
    for (std::size_t index = 0; index < timeline.chapters.size(); ++index) {
        const auto& chapter = timeline.chapters[index];
        if (chapter.startMicroseconds < 0
            || chapter.endMicroseconds <= chapter.startMicroseconds
            || (timeline.durationMicroseconds > 0
                && chapter.endMicroseconds > timeline.durationMicroseconds)) {
            addIssue(issues, "chapters[" + std::to_string(index) + "]",
                     "Chapter range is outside the timeline.");
        }
    }
    return issues;
}

std::vector<MediaValidationIssue> validate(const ClipDefinition& clip,
                                           const MediaTimeline& timeline)
{
    std::vector<MediaValidationIssue> issues;
    if (!(clip.timelineId == timeline.id)) {
        addIssue(issues, "timelineId", "Clip refers to a different media timeline.");
    }
    if (clip.startMicroseconds < 0 || clip.durationMicroseconds <= 0
        || (timeline.durationMicroseconds > 0
            && (clip.startMicroseconds > timeline.durationMicroseconds
                || clip.durationMicroseconds
                    > timeline.durationMicroseconds - clip.startMicroseconds))) {
        addIssue(issues, "range", "Clip range is outside the media timeline.");
    }
    if (!containsVideoStream(timeline, clip.videoStreamIndex)) {
        addIssue(issues, "videoStreamIndex", "Clip video stream is unavailable.");
    }
    if (clip.audioStreamIndex
        && !containsAudioStream(timeline, *clip.audioStreamIndex)) {
        addIssue(issues, "audioStreamIndex", "Clip audio stream is unavailable.");
    }
    if (clip.sourceStartFrame || clip.sourceEndFrame) {
        if (!clip.sourceStartFrame || !clip.sourceEndFrame
            || *clip.sourceStartFrame < 0
            || *clip.sourceEndFrame < *clip.sourceStartFrame
            || !isValidPositiveRational(clip.sourceFrameRate)) {
            addIssue(issues, "sourceFrames",
                     "Frame mapping requires an ordered non-negative range and positive rate.");
        }
    }
    return issues;
}

} // namespace retrovdp::media
