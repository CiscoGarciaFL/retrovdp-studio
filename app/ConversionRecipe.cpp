#include "ConversionRecipe.hpp"

#include "retrovdp/core/TargetProfile.hpp"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cstdint>

namespace retrovdp::appsupport {
namespace {

constexpr qint64 maximumRecipeBytes = 4LL * 1024LL * 1024LL;

constexpr std::array exportFormats{
    formats::ExportFormat::TiFiles,
    formats::ExportFormat::V9t9,
    formats::ExportFormat::Raw,
    formats::ExportFormat::Rle,
    formats::ExportFormat::MsxScreen2,
    formats::ExportFormat::ColecoCvPaint,
    formats::ExportFormat::AdamPowerPaint,
    formats::ExportFormat::AdamHgr,
    formats::ExportFormat::Png,
};

ConversionRecipeResult failure(QString code, QString error)
{
    ConversionRecipeResult result;
    result.errorCode = std::move(code);
    result.error = std::move(error);
    return result;
}

} // namespace

ConversionRecipeResult loadConversionRecipe(const QString& path)
{
    const QFileInfo recipeInfo(path);
    if (!recipeInfo.exists() || !recipeInfo.isFile()) {
        return failure(QStringLiteral("recipe-not-found"),
                       QStringLiteral("The conversion recipe does not exist: %1").arg(path));
    }
    if (recipeInfo.size() < 2 || recipeInfo.size() > maximumRecipeBytes) {
        return failure(QStringLiteral("recipe-size-limit"),
                       QStringLiteral("The conversion recipe exceeds the supported size limit."));
    }
    QFile file(recipeInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(QStringLiteral("recipe-open-failed"),
                       QStringLiteral("Could not open recipe: %1").arg(file.errorString()));
    }
    const QByteArray bytes = file.read(maximumRecipeBytes + 1);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failure(QStringLiteral("recipe-invalid-json"),
                       QStringLiteral("Invalid recipe JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = document.object();
    const QString recipeKind = root.value(QStringLiteral("kind")).toString();
    if ((recipeKind != QStringLiteral("retrovdp-studio-recipe")
         && recipeKind != QStringLiteral("newconvert9918-recipe"))
        || root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        return failure(QStringLiteral("recipe-unsupported-schema"),
                       QStringLiteral("Unsupported recipe type or schema version."));
    }
    if (root.value(QStringLiteral("workspace")).toString()
        != QStringLiteral("screen-image")) {
        return failure(
            QStringLiteral("recipe-workspace-unavailable"),
            QStringLiteral("Only Screen Image recipes can convert media clips."));
    }
    const QJsonObject conversion = root.value(QStringLiteral("conversion")).toObject();
    if (conversion.isEmpty()) {
        return failure(QStringLiteral("recipe-missing-conversion"),
                       QStringLiteral("Recipe does not contain conversion settings."));
    }

    ConversionRecipe recipe;
    recipe.absolutePath = recipeInfo.canonicalFilePath().isEmpty()
        ? recipeInfo.absoluteFilePath() : recipeInfo.canonicalFilePath();
    recipe.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    recipe.settings.mode = static_cast<core::ConversionMode>(std::clamp(
        conversion.value(QStringLiteral("mode")).toInt(0), 0,
        static_cast<int>(core::ConversionMode::Mode5GenesisH40Pal)));
    const auto savedTarget = core::targetProfileId(
        conversion.value(QStringLiteral("targetProfile")).toString().toStdString());
    recipe.settings.targetProfile = core::effectiveTargetProfile(
        savedTarget.value_or(core::primaryTargetProfile(recipe.settings.mode)),
        recipe.settings.mode);
    recipe.settings.dither = static_cast<core::DitherMode>(std::clamp(
        conversion.value(QStringLiteral("dither")).toInt(2), 0, 7));
    recipe.settings.perceptualColorMatching = conversion.value(
        QStringLiteral("perceptualColorMatching")).toBool(false);
    recipe.settings.perceptualRedWeight = std::clamp(
        conversion.value(QStringLiteral("perceptualRedWeight")).toInt(30) / 100.0,
        0.0, 1.0);
    recipe.settings.perceptualGreenWeight = std::clamp(
        conversion.value(QStringLiteral("perceptualGreenWeight")).toInt(52) / 100.0,
        0.0, 1.0);
    recipe.settings.perceptualBlueWeight = std::clamp(
        conversion.value(QStringLiteral("perceptualBlueWeight")).toInt(18) / 100.0,
        0.0, 1.0);
    recipe.settings.stretchHistogram = conversion.value(
        QStringLiteral("stretchHistogram")).toBool(false);
    recipe.settings.maximumColorShiftPercent = std::clamp(
        conversion.value(QStringLiteral("maximumColorShift")).toDouble(1.0),
        0.0, 100.0);
    recipe.settings.gamma = std::clamp(
        conversion.value(QStringLiteral("gamma")).toDouble(1.0), 0.1, 5.0);
    recipe.settings.lumaEmphasis = std::clamp(
        conversion.value(QStringLiteral("lumaEmphasis")).toDouble(1.2), 0.0, 10.0);
    recipe.settings.maximumMulticolorDifferencePercent = std::clamp(
        conversion.value(QStringLiteral("maximumMulticolorDifference")).toInt(95),
        0, 100);
    recipe.settings.orderedDitherBrightness = std::clamp(
        conversion.value(QStringLiteral("orderedBrightness")).toInt(0), 0, 16);
    recipe.settings.errorAccumulation = static_cast<core::ErrorAccumulationMode>(
        std::clamp(conversion.value(QStringLiteral("errorAccumulation")).toInt(1),
                   0, 1));
    recipe.settings.orderedDitherMapSize = conversion.value(
        QStringLiteral("orderedDitherMapSize")).toInt(2) == 4
        ? core::OrderedDitherMapSize::FourByFour
        : core::OrderedDitherMapSize::TwoByTwo;
    recipe.settings.errorDistribution = {
        static_cast<std::uint8_t>(std::clamp(conversion.value(
            QStringLiteral("errorDownLeft")).toInt(2), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(conversion.value(
            QStringLiteral("errorDown")).toInt(2), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(conversion.value(
            QStringLiteral("errorDownRight")).toInt(2), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(conversion.value(
            QStringLiteral("errorRight")).toInt(2), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(conversion.value(
            QStringLiteral("errorFarRight")).toInt(1), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(conversion.value(
            QStringLiteral("errorDownTwo")).toInt(1), 0, 16)),
    };
    recipe.settings.paletteSelection = static_cast<core::PaletteSelectionMode>(
        std::clamp(conversion.value(QStringLiteral("paletteSelection")).toInt(0),
                   0, 1));
    recipe.settings.scanlineStaticColorCount = std::clamp(
        conversion.value(QStringLiteral("scanlineStaticColorCount")).toInt(0),
        0, 14);
    recipe.settings.scanlineRegion1 = conversion.value(
        QStringLiteral("scanlineRegion1")).toBool(true);
    recipe.settings.scanlineRegion2 = conversion.value(
        QStringLiteral("scanlineRegion2")).toBool(true);
    recipe.settings.scanlineRegion3 = conversion.value(
        QStringLiteral("scanlineRegion3")).toBool(true);

    recipe.pipeline.scalingFilter = static_cast<core::ScalingFilter>(std::clamp(
        conversion.value(QStringLiteral("scalingFilter")).toInt(4), 0, 5));
    recipe.pipeline.fillMode = static_cast<core::ImageFillMode>(std::clamp(
        conversion.value(QStringLiteral("fillMode")).toInt(0), 0, 3));
    recipe.pipeline.horizontalOffset = std::clamp(
        conversion.value(QStringLiteral("horizontalOffset")).toInt(0), -256, 256);
    recipe.pipeline.verticalOffset = std::clamp(
        conversion.value(QStringLiteral("verticalOffset")).toInt(0), -192, 192);
    recipe.pipeline.powerPaintFraming = conversion.value(
        QStringLiteral("powerPaintFraming")).toBool(false);
    const QColor background(conversion.value(QStringLiteral("backgroundColor"))
                                .toString(QStringLiteral("#000000")));
    if (!background.isValid()) {
        return failure(QStringLiteral("recipe-invalid-background"),
                       QStringLiteral("Recipe contains an invalid background color."));
    }
    recipe.pipeline.backgroundColor = {
        static_cast<std::uint8_t>(background.red()),
        static_cast<std::uint8_t>(background.green()),
        static_cast<std::uint8_t>(background.blue()),
    };
    const QJsonArray palette = conversion.value(QStringLiteral("workingPalette")).toArray();
    if (!palette.isEmpty()) {
        if (palette.size() != 15) {
            return failure(QStringLiteral("recipe-invalid-palette"),
                           QStringLiteral("Recipe working palette must contain 15 colors."));
        }
        for (const QJsonValue& value : palette) {
            const QColor color(value.toString());
            if (!color.isValid()) {
                return failure(
                    QStringLiteral("recipe-invalid-palette"),
                    QStringLiteral("Recipe working palette contains an invalid color."));
            }
            recipe.pipeline.workingPalette.push_back({
                static_cast<std::uint8_t>(color.red()),
                static_cast<std::uint8_t>(color.green()),
                static_cast<std::uint8_t>(color.blue()),
            });
        }
    }
    const int exportIndex = conversion.value(QStringLiteral("exportFormat")).toInt(0);
    if (exportIndex < 0 || exportIndex >= static_cast<int>(exportFormats.size())) {
        return failure(QStringLiteral("recipe-invalid-export"),
                       QStringLiteral("Recipe contains an invalid export format."));
    }
    recipe.exportFormat = exportFormats[static_cast<std::size_t>(exportIndex)];
    recipe.sourcePath = root.value(QStringLiteral("source")).toObject()
                            .value(QStringLiteral("path")).toString();
    if (!recipe.sourcePath.isEmpty() && QFileInfo(recipe.sourcePath).isRelative()) {
        recipe.sourcePath = recipeInfo.dir().absoluteFilePath(recipe.sourcePath);
    }

    ConversionRecipeResult result;
    result.recipe = std::move(recipe);
    return result;
}

} // namespace retrovdp::appsupport
