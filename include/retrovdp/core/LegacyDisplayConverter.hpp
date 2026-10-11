#pragma once

#include "retrovdp/core/ConversionTypes.hpp"

namespace retrovdp::core {

[[nodiscard]] ConversionResult convertLegacyDisplay(
    const RgbImage& source,
    const ConversionSettings& settings,
    CancellationToken cancellation = {},
    ConversionProgressCallback progress = {});

} // namespace retrovdp::core
