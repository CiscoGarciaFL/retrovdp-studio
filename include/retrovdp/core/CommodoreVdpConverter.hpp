#pragma once

#include "retrovdp/core/ConversionTypes.hpp"

namespace retrovdp::core {

// Compiles documented static VIC-II character/bitmap modes and VIC-20
// character modes. Output uses the chips' native character/bitmap, screen,
// color-RAM, and register byte layouts.
[[nodiscard]] ConversionResult
convertCommodoreDisplay(const RgbImage& source,
                        const ConversionSettings& settings,
                        CancellationToken cancellation = {},
                        ConversionProgressCallback progress = {});

} // namespace retrovdp::core
