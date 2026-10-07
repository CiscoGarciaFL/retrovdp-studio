#pragma once

#include "ConversionPipeline.hpp"

#include "retrovdp/formats/Export.hpp"

#include <QString>

#include <optional>

namespace retrovdp::appsupport {

struct ConversionRecipe {
    core::ConversionSettings settings;
    ConversionPipelineOptions pipeline;
    formats::ExportFormat exportFormat{formats::ExportFormat::TiFiles};
    QString sourcePath;
    QString absolutePath;
    QString sha256;
};

struct ConversionRecipeResult {
    std::optional<ConversionRecipe> recipe;
    QString errorCode;
    QString error;

    [[nodiscard]] explicit operator bool() const { return recipe.has_value(); }
};

[[nodiscard]] ConversionRecipeResult loadConversionRecipe(const QString& path);

} // namespace retrovdp::appsupport
