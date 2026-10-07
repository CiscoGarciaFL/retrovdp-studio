#include "retrovdp/media/MediaTimeline.hpp"

#include <iostream>

namespace {

struct TestContext {
    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

} // namespace

int main()
{
    using namespace retrovdp::media;
    TestContext test;

    test.expect(normalizedRational({30'000, 1001}) == Rational{30'000, 1001}
                    && normalizedRational({60, 2}) == Rational{30, 1}
                    && normalizedRational({-60, -2}) == Rational{30, 1},
                "media rational values should normalize without floating-point conversion");

    const auto timelineId = MediaTimelineId::create("sample-timeline");
    const auto clipId = ClipId::create("intro");
    if (!timelineId || !clipId) return 1;
    MediaTimeline timeline{
        .id = *timelineId,
        .formatName = "mov,mp4",
        .durationMicroseconds = 5'055'000,
        .segments = {{
            .sourceLocator = "sample.mp4",
            .durationMicroseconds = 5'055'000,
        }},
        .videoStreams = {{
            .index = 0,
            .codecName = "h264",
            .width = 960,
            .height = 540,
            .averageFrameRate = {30'000, 1001},
            .nativeFrameRate = {30'000, 1001},
            .timeBase = {1, 30'000},
            .durationMicroseconds = 5'005'000,
            .frameCount = 150,
            .defaultStream = true,
        }},
        .audioStreams = {{
            .index = 1,
            .codecName = "aac",
            .sampleRate = 48'000,
            .channels = 2,
            .channelLayout = "stereo",
            .timeBase = {1, 48'000},
            .durationMicroseconds = 5'055'000,
            .defaultStream = true,
        }},
    };
    test.expect(validate(timeline).empty(),
                "a synchronized video/audio timeline should validate");

    ClipDefinition clip{
        .id = *clipId,
        .timelineId = *timelineId,
        .startMicroseconds = 500'000,
        .durationMicroseconds = 2'000'000,
        .sourceStartFrame = 15,
        .sourceEndFrame = 74,
        .sourceFrameRate = {30'000, 1001},
        .videoStreamIndex = 0,
        .audioStreamIndex = 1,
    };
    test.expect(validate(clip, timeline).empty(),
                "a bounded clip with exact source frame mapping should validate");

    clip.durationMicroseconds = 10'000'000;
    clip.audioStreamIndex = 7;
    const auto invalidClip = validate(clip, timeline);
    test.expect(invalidClip.size() == 2,
                "clip validation should reject an out-of-range duration and missing audio stream");

    timeline.audioStreams.front().index = 0;
    test.expect(!validate(timeline).empty(),
                "timeline validation should reject duplicate global stream indexes");

    if (test.failures == 0) {
        std::cout << "Media timeline validation passed\n";
    }
    return test.failures == 0 ? 0 : 1;
}
