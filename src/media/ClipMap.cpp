#include "retrovdp/media/ClipMap.hpp"

#include <unordered_set>

namespace retrovdp::media {

std::vector<ClipMapValidationIssue> validate(const ClipMapDocument& document)
{
    std::vector<ClipMapValidationIssue> issues;
    if (document.frameRate && !isValidPositiveRational(*document.frameRate)) {
        issues.push_back({"frameRate", "Frame rate must be a positive rational value."});
    }
    if (document.entries.empty()) {
        issues.push_back({"entries", "A clip map requires at least one entry."});
    }

    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < document.entries.size(); ++index) {
        const auto& entry = document.entries[index];
        const std::string prefix = "entries[" + std::to_string(index) + "]";
        if (entry.id.empty()) {
            issues.push_back({prefix + ".id", "Entry ID cannot be empty."});
        } else if (!ids.insert(entry.id).second) {
            issues.push_back({prefix + ".id", "Entry IDs must be unique."});
        }
        if (entry.sourceLocator.empty()) {
            issues.push_back({prefix + ".source", "Source locator cannot be empty."});
        }
        if (entry.endFrame && *entry.endFrame < entry.startFrame) {
            issues.push_back({prefix + ".endFrame",
                              "End frame cannot precede the start frame."});
        }
    }
    return issues;
}

} // namespace retrovdp::media
