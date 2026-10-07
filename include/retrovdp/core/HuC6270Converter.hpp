#pragma once

#include "retrovdp/core/ConversionTypes.hpp"

namespace retrovdp::core {

// Compiles a visible PC Engine/TurboGrafx-16 background into HuC6270 4-plane
// character data, a native little-endian BAT, the complete HuC6260 color
// table, and initial VDC/VCE state.
[[nodiscard]] ConversionResult
convertHuC6270Background(const RgbImage& source,
                         const ConversionSettings& settings,
                         CancellationToken cancellation = {},
                         ConversionProgressCallback progress = {});

} // namespace retrovdp::core
