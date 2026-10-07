#pragma once

#include "retrovdp/media/MediaTimeline.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace retrovdp::media {

struct ClipMapEntry {
    std::string id;
    std::string label;
    std::int64_t startFrame{};
    std::optional<std::int64_t> endFrame;
    std::string sourceLocator;
    std::string audioLocator;
    std::string recipeLocator;
    std::string target;
};

struct ClipMapDocument {
    std::string name;
    std::string mediaRoot;
    std::optional<Rational> frameRate;
    std::string defaultRecipeLocator;
    std::string defaultTarget;
    std::vector<ClipMapEntry> entries;
};

struct ClipMapValidationIssue {
    std::string field;
    std::string message;
};

[[nodiscard]] std::vector<ClipMapValidationIssue>
validate(const ClipMapDocument& document);

} // namespace retrovdp::media
