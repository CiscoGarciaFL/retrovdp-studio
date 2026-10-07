#include "retrovdp/core/Bitmap9918Converter.hpp"
#include "retrovdp/core/CommodoreVdpConverter.hpp"
#include "retrovdp/core/ColorMath.hpp"
#include "retrovdp/core/ConversionTypes.hpp"
#include "retrovdp/core/ConversionJobController.hpp"
#include "retrovdp/core/ConversionPerformance.hpp"
#include "retrovdp/core/Dithering.hpp"
#include "retrovdp/core/F18AConverter.hpp"
#include "retrovdp/core/HuC6270Converter.hpp"
#include "retrovdp/core/ImageAdjustments.hpp"
#include "retrovdp/core/ImageTransform.hpp"
#include "retrovdp/core/Multicolor9918Converter.hpp"
#include "retrovdp/core/PaletteSelection.hpp"
#include "retrovdp/core/RgbImage.hpp"
#include "retrovdp/core/SegaGenesisVdpConverter.hpp"
#include "retrovdp/core/SegaSmsVdpConverter.hpp"
#include "retrovdp/core/TargetData.hpp"
#include "retrovdp/core/TargetProfile.hpp"
#include "retrovdp/core/Validation.hpp"
#include "retrovdp/core/YamahaVdpConverter.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>

using retrovdp::core::ConversionSettings;
using retrovdp::core::CancellationSource;
using retrovdp::core::ConversionDiagnostic;
using retrovdp::core::ConversionMode;
using retrovdp::core::ConversionRequest;
using retrovdp::core::ConversionResult;
using retrovdp::core::ConversionStatus;
using retrovdp::core::ConversionJobController;
using retrovdp::core::ConversionMemoryEstimate;
using retrovdp::core::DiagnosticSeverity;
using retrovdp::core::TargetCapability;
using retrovdp::core::TargetProfileId;
using retrovdp::core::TargetProfileStatus;
using retrovdp::core::DitherMode;
using retrovdp::core::ErrorAccumulationMode;
using retrovdp::core::ErrorDiffusionBuffer;
using retrovdp::core::ErrorDiffusionBufferError;
using retrovdp::core::ErrorDistributionKernel;
using retrovdp::core::ImageLayout;
using retrovdp::core::ImageLayoutError;
using retrovdp::core::ImageSizeLimits;
using retrovdp::core::ImageFillMode;
using retrovdp::core::ImageAdjustmentError;
using retrovdp::core::ImageTransformError;
using retrovdp::core::ImageTransformOptions;
using retrovdp::core::MedianCutColorDepth;
using retrovdp::core::PerceptualRgbWeights;
using retrovdp::core::Palette;
using retrovdp::core::PaletteError;
using retrovdp::core::PaletteSelectionError;
using retrovdp::core::PaletteSelectionMode;
using retrovdp::core::PixelFormat;
using retrovdp::core::OrderedDitherMapSize;
using retrovdp::core::PopularityWeighting;
using retrovdp::core::RgbColor;
using retrovdp::core::RgbSample;
using retrovdp::core::RgbImage;
using retrovdp::core::ScalingFilter;
using retrovdp::core::TargetMemoryImage;
using retrovdp::core::TargetMemoryTable;
using retrovdp::core::TargetTableError;
using retrovdp::core::TargetTableRole;
using retrovdp::core::bytesPerPixel;
using retrovdp::core::adjustImage;
using retrovdp::core::applyOrderedDither;
using retrovdp::core::colorDistanceSquared;
using retrovdp::core::convertBlackAndWhiteBitmap9918;
using retrovdp::core::convertBitmapColorOnly9918;
using retrovdp::core::convertBitmap9918;
using retrovdp::core::convertCommodoreDisplay;
using retrovdp::core::convertDualMulticolor9918;
using retrovdp::core::convertPalettedBitmapF18A;
using retrovdp::core::convertScanlinePaletteBitmapF18A;
using retrovdp::core::convertSegaSmsMode4;
using retrovdp::core::convertYamahaBitmap;
using retrovdp::core::estimateConversionMemory;
using retrovdp::core::convertGreyscaleBitmap9918;
using retrovdp::core::convertHalfMulticolor9918;
using retrovdp::core::convertHuC6270Background;
using retrovdp::core::convertMulticolor9918;
using retrovdp::core::defaultBitmap9918Palette;
using retrovdp::core::ditherConfiguration;
using retrovdp::core::expectedTargetTables;
using retrovdp::core::greyscaleBitmap9918Palette;
using retrovdp::core::orderedDitherThreshold;
using retrovdp::core::planImageTransform;
using retrovdp::core::perceptualRgbDistanceSquared;
using retrovdp::core::toYCrCb;
using retrovdp::core::selectMedianCutPalette;
using retrovdp::core::selectPopularPalette;
using retrovdp::core::transformImage;
using retrovdp::core::validate;
using retrovdp::core::validateImageLayout;
using retrovdp::core::validateTargetTables;
using retrovdp::core::yCrCbDistanceSquared;

namespace {

const QString corpusDirectory = QStringLiteral(RETROVDP_GOLDEN_DIR);

class TestContext final {
public:
    void expect(bool condition, std::string_view message)
    {
        if (!condition) {
            ++failures_;
            std::cerr << "FAIL: " << message << '\n';
        }
    }

    void expectNear(double actual, double expected, double tolerance, std::string_view message)
    {
        expect(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
    }

    [[nodiscard]] int result() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

QJsonObject loadCorpusManifest()
{
    QFile file(corpusDirectory + QStringLiteral("/corpus.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

QJsonObject loadCaptureManifest()
{
    QFile file(corpusDirectory + QStringLiteral("/reference/original-1_9_1/capture.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

QJsonObject loadModeCaptureManifest()
{
    QFile file(corpusDirectory
               + QStringLiteral("/reference/original-1_9_1/mode-captures.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

QJsonObject loadExportCaptureManifest()
{
    QFile file(corpusDirectory
               + QStringLiteral("/reference/original-1_9_1/export-captures.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

QString corpusPath(const QJsonObject &entry)
{
    const QString repositoryRelativePath = entry.value(QStringLiteral("path")).toString();
    const QString prefix = QStringLiteral("tests/golden/");
    if (!repositoryRelativePath.startsWith(prefix)) {
        return {};
    }
    return corpusDirectory + QStringLiteral("/") + repositoryRelativePath.mid(prefix.size());
}

QByteArray sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result().toHex();
}

QByteArray readPrefix(const QString &path, qint64 maximumSize)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(maximumSize) : QByteArray{};
}

void testSettingsValidation(TestContext &test)
{
    test.expect(validate(ConversionSettings{}).empty(), "default settings should be valid");

    ConversionSettings settings;
    settings.gamma = 0.0;
    auto issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "gamma",
                "zero gamma should produce one gamma issue");

    settings = ConversionSettings{};
    settings.maximumColorShiftPercent = 101.0;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "maximumColorShiftPercent",
                "color shift above 100 percent should produce one color-shift issue");

    settings = ConversionSettings{};
    settings.maximumMulticolorDifferencePercent = 0;
    issues = validate(settings);
    test.expect(issues.size() == 1
                    && issues.front().field == "maximumMulticolorDifferencePercent",
                "multicolor difference below one percent should be rejected");

    settings = ConversionSettings{};
    settings.perceptualRedWeight = -0.01;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "perceptualRedWeight",
                "negative perceptual weights should be rejected");

    settings = ConversionSettings{};
    settings.perceptualRedWeight = 0.0;
    settings.perceptualGreenWeight = 0.0;
    settings.perceptualBlueWeight = 0.0;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "perceptualColorWeights",
                "at least one perceptual weight should be required");

    settings = ConversionSettings{};
    settings.lumaEmphasis = -0.01;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "lumaEmphasis",
                "negative luma emphasis should be rejected");

    settings = ConversionSettings{};
    settings.gamma = std::numeric_limits<double>::quiet_NaN();
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "gamma",
                "non-finite settings should be rejected");

    settings = ConversionSettings{};
    settings.dither = static_cast<DitherMode>(255);
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "dither",
                "unknown dither modes should be rejected");

    settings = ConversionSettings{};
    settings.orderedDitherMapSize = static_cast<OrderedDitherMapSize>(3);
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "orderedDitherMapSize",
                "unsupported ordered-map sizes should be rejected");

    settings = ConversionSettings{};
    settings.orderedDitherBrightness = 17;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "orderedDitherBrightness",
                "ordered brightness above sixteen should be rejected");

    settings = ConversionSettings{};
    settings.errorAccumulation = static_cast<ErrorAccumulationMode>(255);
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "errorAccumulation",
                "unknown error accumulation modes should be rejected");

    settings = ConversionSettings{};
    settings.dither = DitherMode::Custom;
    settings.errorDistribution.farRight = 17;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "errorDistribution",
                "custom error-distribution weights above sixteen should be rejected");

    settings = ConversionSettings{};
    settings.paletteSelection = static_cast<PaletteSelectionMode>(255);
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "paletteSelection",
                "unknown palette-selection modes should be rejected");

    settings = ConversionSettings{};
    settings.scanlineStaticColorCount = 15;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "scanlineStaticColorCount",
                "more than fourteen shared scanline colors should be rejected");

    settings = ConversionSettings{};
    settings.scanlineStaticColorCount = 1;
    settings.scanlineRegion1 = false;
    settings.scanlineRegion2 = false;
    settings.scanlineRegion3 = false;
    issues = validate(settings);
    test.expect(issues.size() == 1 && issues.front().field == "scanlineRegions",
                "shared scanline colors should require at least one source region");
}

void testDithering(TestContext &test)
{
    const auto none = ditherConfiguration(DitherMode::None);
    test.expect(none && !none->ordered && !none->distributeError
                    && none->kernel.totalWeight() == 0,
                "none should disable both dithering paths");

    const auto floyd = ditherConfiguration(DitherMode::FloydSteinberg);
    test.expect(floyd && !floyd->ordered && floyd->distributeError
                    && floyd->kernel.downLeft == 3 && floyd->kernel.down == 5
                    && floyd->kernel.downRight == 1 && floyd->kernel.right == 7
                    && floyd->kernel.farRight == 0 && floyd->kernel.downTwo == 0
                    && floyd->kernel.totalWeight() == 16,
                "Floyd-Steinberg should retain the original six-cell kernel");

    const auto atkinson = ditherConfiguration(DitherMode::Atkinson);
    test.expect(atkinson && atkinson->kernel.totalWeight() == 10
                    && atkinson->kernel.farRight == 1
                    && atkinson->kernel.downTwo == 1,
                "Atkinson should intentionally distribute ten sixteenths of error");

    const auto pattern = ditherConfiguration(DitherMode::Pattern);
    test.expect(pattern && pattern->kernel.totalWeight() == 16
                    && pattern->kernel.down == 8 && pattern->kernel.right == 8,
                "pattern dithering should split error down and right");

    const auto diagonal = ditherConfiguration(DitherMode::Diagonal);
    test.expect(diagonal && diagonal->kernel.totalWeight() == 11,
                "diagonal dithering should retain its eleven-part kernel");

    const auto ordered = ditherConfiguration(DitherMode::Ordered);
    test.expect(ordered && ordered->ordered && !ordered->distributeError,
                "ordered mode should use only its threshold map");

    const auto orderedWithError = ditherConfiguration(DitherMode::OrderedWithError);
    test.expect(orderedWithError && orderedWithError->ordered
                    && orderedWithError->distributeError
                    && orderedWithError->kernel.totalWeight() == 7,
                "ordered-with-error should retain the original seven-part kernel");
    const ErrorDistributionKernel customKernel{4, 3, 2, 1, 0, 5};
    const auto custom = ditherConfiguration(DitherMode::Custom, customKernel);
    test.expect(custom && !custom->ordered && custom->distributeError
                    && custom->kernel.downLeft == 4 && custom->kernel.down == 3
                    && custom->kernel.downRight == 2 && custom->kernel.right == 1
                    && custom->kernel.farRight == 0 && custom->kernel.downTwo == 5
                    && custom->kernel.totalWeight() == 15,
                "custom dithering should use all six caller-provided weights");
    test.expect(!ditherConfiguration(static_cast<DitherMode>(255)),
                "unknown dither modes should not produce a configuration");

    test.expectNear(*orderedDitherThreshold(OrderedDitherMapSize::TwoByTwo, 0, 0, 0),
                    0.0, 1.0e-12, "2x2 threshold origin should be zero");
    test.expectNear(*orderedDitherThreshold(OrderedDitherMapSize::TwoByTwo, 1, 0, 0),
                    0.75, 1.0e-12, "2x2 threshold map should be indexed x first");
    test.expectNear(*orderedDitherThreshold(OrderedDitherMapSize::TwoByTwo, 0, 1, 0),
                    0.5, 1.0e-12, "2x2 threshold map should preserve its second row");
    test.expectNear(*orderedDitherThreshold(OrderedDitherMapSize::TwoByTwo, 3, 2, 8),
                    0.25, 1.0e-12,
                    "2x2 threshold coordinates should wrap and subtract brightness");
    test.expectNear(*orderedDitherThreshold(OrderedDitherMapSize::FourByFour, 1, 0, 0),
                    0.75, 1.0e-12, "4x4 threshold map should be indexed x first");
    test.expectNear(*orderedDitherThreshold(OrderedDitherMapSize::FourByFour, 5, 4, 0),
                    0.75, 1.0e-12, "4x4 threshold coordinates should wrap");
    test.expect(!orderedDitherThreshold(OrderedDitherMapSize::TwoByTwo, 0, 0, -1)
                    && !orderedDitherThreshold(
                        static_cast<OrderedDitherMapSize>(3), 0, 0, 0),
                "invalid ordered-dither arguments should be rejected");

    const auto adjusted = applyOrderedDither(
        {100.0, 80.0, 40.0}, 1, 0, OrderedDitherMapSize::TwoByTwo, 0);
    test.expect(adjusted.has_value(), "valid ordered dithering should produce a sample");
    test.expectNear(adjusted->red, 175.0, 1.0e-12,
                    "ordered dithering should scale the red channel");
    test.expectNear(adjusted->green, 140.0, 1.0e-12,
                    "ordered dithering should scale the green channel");
    test.expectNear(adjusted->blue, 70.0, 1.0e-12,
                    "ordered dithering should scale the blue channel");
    const auto darkened = applyOrderedDither(
        {100.0, 80.0, 40.0}, 1, 0, OrderedDitherMapSize::TwoByTwo, 16);
    test.expect(darkened && darkened->red < adjusted->red
                    && darkened->green < adjusted->green
                    && darkened->blue < adjusted->blue,
                "increasing the legacy ordered slider should darken non-extreme samples");

    const auto black = applyOrderedDither(
        {8.0, 4.0, 0.0}, 1, 0, OrderedDitherMapSize::TwoByTwo, 0);
    const auto white = applyOrderedDither(
        {248.0, 250.0, 255.0}, 1, 0, OrderedDitherMapSize::TwoByTwo, 0);
    test.expect(black && black->red == 8.0 && black->green == 4.0 && black->blue == 0.0,
                "near-black samples should bypass ordered adjustment");
    test.expect(white && white->red == 248.0 && white->green == 250.0
                    && white->blue == 255.0,
                "near-white samples should bypass ordered adjustment");

    ErrorDiffusionBufferError bufferError = ErrorDiffusionBufferError::None;
    auto buffer = ErrorDiffusionBuffer::create(4, 3, 12, &bufferError);
    test.expect(buffer.has_value() && bufferError == ErrorDiffusionBufferError::None,
                "a bounded error buffer should be created");
    buffer->distribute(1, 0, {16.0, 32.0, 48.0}, floyd->kernel);
    const auto rightError = buffer->errorAt(2, 0);
    const auto downLeftError = buffer->errorAt(0, 1);
    const auto downError = buffer->errorAt(1, 1);
    const auto downRightError = buffer->errorAt(2, 1);
    test.expectNear(rightError.red, 7.0, 1.0e-12,
                    "right neighbor should receive seven sixteenths of red error");
    test.expectNear(rightError.green, 14.0, 1.0e-12,
                    "right neighbor should receive seven sixteenths of green error");
    test.expectNear(rightError.blue, 21.0, 1.0e-12,
                    "right neighbor should receive seven sixteenths of blue error");
    test.expectNear(downLeftError.red, 3.0, 1.0e-12,
                    "down-left neighbor should receive three sixteenths of error");
    test.expectNear(downError.red, 5.0, 1.0e-12,
                    "down neighbor should receive five sixteenths of error");
    test.expectNear(downRightError.red, 1.0, 1.0e-12,
                    "down-right neighbor should receive one sixteenth of error");

    const ErrorDistributionKernel longKernel{0, 0, 0, 0, 8, 4};
    buffer->distribute(0, 0, {16.0, 16.0, 16.0}, longKernel);
    test.expectNear(buffer->errorAt(2, 0).red, 15.0, 1.0e-12,
                    "far-right distribution should reach two columns ahead");
    test.expectNear(buffer->errorAt(0, 2).red, 4.0, 1.0e-12,
                    "down-two distribution should reach two rows ahead");

    buffer->distribute(3, 2, {16.0, 16.0, 16.0}, floyd->kernel);
    const auto accumulated = buffer->adjustedSample(
        {10.0, 20.0, 30.0}, 1, 1, ErrorAccumulationMode::Accumulate);
    const auto averaged = buffer->adjustedSample(
        {10.0, 20.0, 30.0}, 1, 1, ErrorAccumulationMode::Average);
    const auto firstRowAverage = buffer->adjustedSample(
        {10.0, 20.0, 30.0}, 2, 0, ErrorAccumulationMode::Average);
    test.expectNear(accumulated.red, 15.0, 1.0e-12,
                    "accumulate mode should apply the complete stored error");
    test.expectNear(averaged.red, 10.0 + 5.0 / 3.0, 1.0e-12,
                    "average mode should divide later-row error by three");
    test.expectNear(firstRowAverage.red, 25.0, 1.0e-12,
                    "average mode should not divide first-row error");

    test.expect(!ErrorDiffusionBuffer::create(0, 1, 1, &bufferError)
                    && bufferError == ErrorDiffusionBufferError::ZeroDimension,
                "zero-sized error buffers should be rejected");
    test.expect(!ErrorDiffusionBuffer::create(2, 2, 3, &bufferError)
                    && bufferError == ErrorDiffusionBufferError::PixelLimitExceeded,
                "error buffers should enforce their pixel limit");
}

void testBitmap9918Conversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    test.expect(palette.size() == 15,
                "the Bitmap 9918A working palette should contain fifteen colors");
    test.expect(palette.at(0) == RgbColor{248, 248, 248}
                    && palette.at(1) == RgbColor{0, 0, 0}
                    && palette.at(2) == RgbColor{200, 200, 200}
                    && palette.at(14) == RgbColor{200, 88, 184},
                "the Bitmap 9918A palette should preserve original working indexes");

    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(), "the Bitmap 9918A test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const std::uint8_t value = (x & 1U) == 0 ? 0 : 248;
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = value;
            row[offset + 1] = value;
            row[offset + 2] = value;
        }
    }

    ConversionSettings settings;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertBitmap9918(*source, palette, settings);
    test.expect(result.succeeded() && result.preview.has_value()
                    && result.target.has_value(),
                "a valid target-sized image should convert to Bitmap 9918A");
    test.expect(result.target->mode == ConversionMode::Bitmap9918
                    && result.target->tables.size() == 2
                    && validateTargetTables(*result.target),
                "Bitmap 9918A conversion should return valid pattern and color tables");

    const auto &patterns = result.target->tables[0];
    const auto &colors = result.target->tables[1];
    test.expect(patterns.role == TargetTableRole::Pattern
                    && std::ranges::all_of(patterns.bytes, [](std::uint8_t value) {
                           return value == 0xaa;
                       }),
                "alternating black and white pixels should encode as pattern AA");
    test.expect(colors.role == TargetTableRole::Color
                    && std::ranges::all_of(colors.bytes, [](std::uint8_t value) {
                           return value == 0x1f;
                       }),
                "alternating black and white should encode as TI black on white");

    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 0 && previewRow[1] == 0 && previewRow[2] == 0
                    && previewRow[3] == 248 && previewRow[4] == 248
                    && previewRow[5] == 248,
                "the Bitmap 9918A preview should use the selected working colors");

    std::vector<RgbColor> editedColors(palette.colors().begin(), palette.colors().end());
    editedColors[1] = {248, 0, 248};
    const Palette editedPalette = std::move(*Palette::create(std::move(editedColors)));
    const ConversionResult editedPaletteResult = convertBitmap9918(
        *source, editedPalette, settings);
    test.expect(editedPaletteResult.succeeded()
                    && editedPaletteResult.target->tables[1].bytes != colors.bytes,
                "an edited working palette should affect deterministic bitmap output");

    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const std::uint8_t value = static_cast<std::uint8_t>((x * 7U + y * 3U) % 249U);
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = value;
            row[offset + 1] = value;
            row[offset + 2] = value;
        }
    }
    settings.dither = DitherMode::Custom;
    settings.errorDistribution = {};
    const ConversionResult zeroKernelResult = convertBitmap9918(*source, palette, settings);
    settings.errorDistribution = {0, 0, 0, 16, 0, 0};
    const ConversionResult rightKernelResult = convertBitmap9918(*source, palette, settings);
    test.expect(zeroKernelResult.succeeded() && rightKernelResult.succeeded()
                    && zeroKernelResult.target->tables[0].bytes
                        != rightKernelResult.target->tables[0].bytes,
                "Bitmap conversion should apply the editable custom error kernel");

    auto wrongSize = RgbImage::createTightlyPacked(8, 8, PixelFormat::Rgb888);
    const ConversionResult wrongSizeResult = convertBitmap9918(*wrongSize, palette, settings);
    test.expect(!wrongSizeResult.succeeded() && wrongSizeResult.hasErrors()
                    && wrongSizeResult.diagnostics.front().code
                        == "bitmap9918-invalid-dimensions",
                "Bitmap 9918A conversion should reject non-target-sized images");

    const Palette shortPalette = *Palette::create({{0, 0, 0}, {255, 255, 255}});
    const ConversionResult shortPaletteResult = convertBitmap9918(*source, shortPalette, settings);
    test.expect(!shortPaletteResult.succeeded()
                    && shortPaletteResult.diagnostics.front().code
                        == "bitmap9918-invalid-palette",
                "Bitmap 9918A conversion should reject nonstandard palette sizes");

    settings.mode = ConversionMode::Multicolor9918;
    const ConversionResult wrongModeResult = convertBitmap9918(*source, palette, settings);
    test.expect(!wrongModeResult.succeeded()
                    && wrongModeResult.diagnostics.front().code == "bitmap9918-wrong-mode",
                "Bitmap 9918A conversion should reject another conversion mode");
}

void testGreyscaleBitmap9918Conversion(TestContext &test)
{
    const Palette colorPalette = defaultBitmap9918Palette();
    const Palette greyPalette = greyscaleBitmap9918Palette(colorPalette);
    test.expect(greyPalette.size() == 15
                    && greyPalette.at(0) == RgbColor{248, 248, 248}
                    && greyPalette.at(1) == RgbColor{0, 0, 0}
                    && greyPalette.at(3) == RgbColor{154, 154, 154}
                    && greyPalette.at(5) == RgbColor{90, 90, 90},
                "greyscale mode should use the original Rec.709 palette conversion");

    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(), "the greyscale Bitmap test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = 0;
            row[offset + 1] = 154;
            row[offset + 2] = 0;
        }
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::GreyscaleBitmap9918;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertGreyscaleBitmap9918(
        *source, colorPalette, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Greyscale Bitmap 9918A");
    test.expect(result.target->mode == ConversionMode::GreyscaleBitmap9918
                    && validateTargetTables(*result.target),
                "greyscale conversion should return valid Graphics II tables");
    test.expect(std::ranges::all_of(
                    result.target->tables[0].bytes,
                    [](std::uint8_t value) { return value == 0x00; })
                    && std::ranges::all_of(
                        result.target->tables[1].bytes,
                        [](std::uint8_t value) { return value == 0x14; }),
                "a uniform 90-luma source should encode as solid dark-blue luminance");

    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 90 && previewRow[1] == 90 && previewRow[2] == 90,
                "greyscale conversion should emit equal preview channels");

    settings.mode = ConversionMode::Bitmap9918;
    const ConversionResult wrongMode = convertGreyscaleBitmap9918(
        *source, colorPalette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "greyscale-bitmap9918-wrong-mode",
                "greyscale conversion should reject another selected mode");
}

void testBlackAndWhiteBitmap9918Conversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(),
                "the black-and-white Bitmap test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const std::uint8_t value = (x & 1U) == 0 ? 0 : 248;
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = value;
            row[offset + 1] = value;
            row[offset + 2] = value;
        }
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::BlackAndWhiteBitmap9918;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertBlackAndWhiteBitmap9918(
        *source, palette, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Black-and-White Bitmap 9918A");
    test.expect(result.target->mode == ConversionMode::BlackAndWhiteBitmap9918
                    && result.target->tables.size() == 1
                    && validateTargetTables(*result.target),
                "black-and-white conversion should emit only a valid pattern table");
    test.expect(result.target->tables[0].role == TargetTableRole::Pattern
                    && std::ranges::all_of(
                        result.target->tables[0].bytes,
                        [](std::uint8_t value) { return value == 0xaa; }),
                "black pixels should be forced to the set pattern bits");

    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 0 && previewRow[1] == 0 && previewRow[2] == 0
                    && previewRow[3] == 248 && previewRow[4] == 248
                    && previewRow[5] == 248,
                "the black-and-white preview should preserve alternating pixels");

    settings.mode = ConversionMode::Bitmap9918;
    const ConversionResult wrongMode = convertBlackAndWhiteBitmap9918(
        *source, palette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "black-white-bitmap9918-wrong-mode",
                "black-and-white conversion should reject another selected mode");
}

void testBitmapColorOnly9918Conversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(),
                "the Bitmap Color Only test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const bool firstHalf = (x & 7U) < 4;
            const std::uint8_t value = firstHalf ? 248 : 0;
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = value;
            row[offset + 1] = value;
            row[offset + 2] = value;
        }
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::BitmapColorOnly9918;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertBitmapColorOnly9918(
        *source, palette, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Bitmap Color Only 9918A");
    test.expect(result.target->mode == ConversionMode::BitmapColorOnly9918
                    && result.target->tables.size() == 2
                    && validateTargetTables(*result.target),
                "Bitmap Color Only should emit valid fixed-pattern and color tables");
    test.expect(result.target->tables[0].role == TargetTableRole::FixedPattern
                    && std::ranges::all_of(
                        result.target->tables[0].bytes,
                        [](std::uint8_t value) { return value == 0xf0; }),
                "Bitmap Color Only should force every pattern byte to F0");
    test.expect(result.target->tables[1].role == TargetTableRole::Color
                    && std::ranges::all_of(
                        result.target->tables[1].bytes,
                        [](std::uint8_t value) { return value == 0x1f; }),
                "Bitmap Color Only should preserve the original color-byte ordering");

    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 248 && previewRow[1] == 248
                    && previewRow[2] == 248 && previewRow[12] == 0
                    && previewRow[13] == 0 && previewRow[14] == 0,
                "Bitmap Color Only preview should retain its searched half-block colors");

    settings.mode = ConversionMode::Bitmap9918;
    const ConversionResult wrongMode = convertBitmapColorOnly9918(
        *source, palette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "bitmap-color-only9918-wrong-mode",
                "Bitmap Color Only should reject another selected mode");
}

void testMulticolor9918Conversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(), "the Multicolor 9918 test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const RgbColor color = palette.at(((x / 4) & 1U) == 0 ? 1 : 0);
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = color.red;
            row[offset + 1] = color.green;
            row[offset + 2] = color.blue;
        }
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::Multicolor9918;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    int progressFrames = 0;
    std::uint32_t firstProgressRow = 0;
    std::uint32_t lastProgressRow = 0;
    const ConversionResult result = convertMulticolor9918(
        *source,
        palette,
        settings,
        {},
        [&](const RgbImage& preview,
            std::uint32_t completedRows,
            std::uint32_t totalRows) {
            ++progressFrames;
            if (firstProgressRow == 0) firstProgressRow = completedRows;
            lastProgressRow = completedRows;
            test.expect(preview.width() == 256 && preview.height() == 192
                            && totalRows == 192,
                        "live conversion frames should retain target geometry");
        });
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Multicolor 9918");
    test.expect(result.target->mode == ConversionMode::Multicolor9918
                    && result.target->tables.size() == 1
                    && validateTargetTables(*result.target),
                "Multicolor 9918 should emit one valid 1536-byte table");
    test.expect(result.target->tables[0].role == TargetTableRole::Multicolor
                    && std::ranges::all_of(
                        result.target->tables[0].bytes,
                        [](std::uint8_t value) { return value == 0x1f; }),
                "alternating black and white 4x4 cells should pack as 1F bytes");

    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 0 && previewRow[1] == 0 && previewRow[2] == 0
                    && previewRow[12] == 248 && previewRow[13] == 248
                    && previewRow[14] == 248,
                "Multicolor 9918 preview should expand each logical pixel to 4x4");
    test.expect(progressFrames == 24 && firstProgressRow == 8 && lastProgressRow == 192,
                "live conversion should publish throttled completed-row frames");

    settings.mode = ConversionMode::Bitmap9918;
    const ConversionResult wrongMode = convertMulticolor9918(*source, palette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code == "multicolor9918-wrong-mode",
                "Multicolor 9918 should reject another selected mode");
}

void testDualMulticolor9918Conversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(),
                "the Dual Multicolor 9918 test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        for (std::uint32_t x = 0; x < source->width(); ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * 3;
            row[offset] = 56;
            row[offset + 1] = 140;
            row[offset + 2] = 148;
        }
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::DualMulticolor9918;
    settings.dither = DitherMode::None;
    const ConversionResult result = convertDualMulticolor9918(
        *source, palette, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Dual Multicolor 9918");
    test.expect(result.target->mode == ConversionMode::DualMulticolor9918
                    && result.target->tables.size() == 2
                    && validateTargetTables(*result.target),
                "Dual Multicolor should emit two valid 1536-byte frame tables");
    test.expect(result.target->tables[0].role == TargetTableRole::MulticolorFrame1
                    && std::ranges::all_of(
                        result.target->tables[0].bytes,
                        [](std::uint8_t value) { return value == 0x44; })
                    && result.target->tables[1].role
                        == TargetTableRole::MulticolorFrame2
                    && std::ranges::all_of(
                        result.target->tables[1].bytes,
                        [](std::uint8_t value) { return value == 0x22; }),
                "the two legacy frame colors should retain low- then high-nibble order");

    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 56 && previewRow[1] == 140
                    && previewRow[2] == 148,
                "Dual Multicolor preview should show the temporal frame average");

    settings.mode = ConversionMode::Multicolor9918;
    const ConversionResult wrongMode = convertDualMulticolor9918(
        *source, palette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "dual-multicolor9918-wrong-mode",
                "Dual Multicolor should reject another selected mode");
}

void testHalfMulticolor9918Conversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(),
                "the Half Multicolor 9918A test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::span<std::uint8_t> row = source->row(y);
        std::ranges::fill(row, 0);
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::HalfMulticolor9918;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertHalfMulticolor9918(
        *source, palette, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Half Multicolor 9918A");
    test.expect(result.target->mode == ConversionMode::HalfMulticolor9918
                    && result.target->tables.size() == 3
                    && validateTargetTables(*result.target),
                "Half Multicolor should emit valid bitmap and multicolor tables");
    test.expect(result.target->tables[0].role == TargetTableRole::Pattern
                    && std::ranges::all_of(
                        result.target->tables[0].bytes,
                        [](std::uint8_t value) { return value == 0x00; })
                    && result.target->tables[1].role == TargetTableRole::Color
                    && std::ranges::all_of(
                        result.target->tables[1].bytes,
                        [](std::uint8_t value) { return value == 0x11; }),
                "uniform black should retain black bitmap colors through table rotation");
    const auto &multicolor = result.target->tables[2];
    test.expect(multicolor.role == TargetTableRole::Multicolor
                    && std::ranges::count(multicolor.bytes, std::uint8_t{0x11}) == 1536
                    && std::ranges::count(multicolor.bytes, std::uint8_t{0x00}) == 512,
                "Half Multicolor should populate the legacy 1536 used bytes in its 2 KiB table");
    const std::span<const std::uint8_t> previewRow = result.preview->row(0);
    test.expect(previewRow[0] == 0 && previewRow[1] == 0 && previewRow[2] == 0,
                "Half Multicolor preview should show the temporal mixed color");

    settings.mode = ConversionMode::DualMulticolor9918;
    const ConversionResult wrongMode = convertHalfMulticolor9918(
        *source, palette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "half-multicolor9918-wrong-mode",
                "Half Multicolor should reject another selected mode");
}

void testPalettedBitmapF18AConversion(TestContext &test)
{
    const Palette palette = defaultBitmap9918Palette();
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(),
                "the Paletted Bitmap F18A test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::ranges::fill(source->row(y), std::uint8_t{0});
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::PalettedBitmapF18A;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertPalettedBitmapF18A(
        *source, palette, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image and selected palette should convert to Paletted Bitmap F18A");
    test.expect(result.target->mode == ConversionMode::PalettedBitmapF18A
                    && result.target->palette
                    && result.target->tables.size() == 3
                    && validateTargetTables(*result.target),
                "Paletted Bitmap F18A should emit bitmap and 32-byte palette tables");
    test.expect(result.target->palette->at(0) == RgbColor{255, 255, 255}
                    && result.target->palette->at(3) == RgbColor{34, 204, 68},
                "the selected palette should be rounded to duplicated RGB444 nibbles");
    const auto &paletteTable = result.target->tables[2];
    test.expect(paletteTable.role == TargetTableRole::Palette
                    && paletteTable.bytes[0] == 0x00
                    && paletteTable.bytes[1] == 0x00
                    && paletteTable.bytes[28] == 0x0c
                    && paletteTable.bytes[29] == 0xcc
                    && paletteTable.bytes[30] == 0x0f
                    && paletteTable.bytes[31] == 0xff,
                "the F18A palette should use hardware color remapping and 0000RRRR GGGGBBBB packing");
    test.expect(std::ranges::all_of(
                    result.target->tables[0].bytes,
                    [](std::uint8_t value) { return value == 0x00; })
                    && std::ranges::all_of(
                        result.target->tables[1].bytes,
                        [](std::uint8_t value) { return value == 0x11; }),
                "uniform black should encode as solid black with the rounded palette");

    settings.mode = ConversionMode::Bitmap9918;
    const ConversionResult wrongMode = convertPalettedBitmapF18A(
        *source, palette, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "paletted-bitmap-f18a-wrong-mode",
                "Paletted Bitmap F18A should reject another selected mode");
}

void testScanlinePaletteBitmapF18AConversion(TestContext &test)
{
    auto source = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(source.has_value(),
                "the Scanline Palette Bitmap F18A test source should be allocated");
    for (std::uint32_t y = 0; y < source->height(); ++y) {
        std::ranges::fill(source->row(y), std::uint8_t{0});
    }

    ConversionSettings settings;
    settings.mode = ConversionMode::ScanlinePaletteBitmapF18A;
    settings.dither = DitherMode::None;
    settings.maximumColorShiftPercent = 0.0;
    const ConversionResult result = convertScanlinePaletteBitmapF18A(
        *source, settings);
    test.expect(result.succeeded() && result.preview && result.target,
                "a valid image should convert to Scanline Palette Bitmap F18A");
    test.expect(result.target->mode == ConversionMode::ScanlinePaletteBitmapF18A
                    && !result.target->palette
                    && result.target->tables.size() == 3
                    && validateTargetTables(*result.target),
                "Scanline Palette F18A should emit bitmap and 6 KiB palette tables");
    test.expect(std::ranges::all_of(
                    result.target->tables[0].bytes,
                    [](std::uint8_t value) { return value == 0x00; })
                    && std::ranges::all_of(
                        result.target->tables[1].bytes,
                        [](std::uint8_t value) { return value == 0x11; })
                    && result.target->tables[2].role
                        == TargetTableRole::ScanlinePalettes
                    && std::ranges::all_of(
                        result.target->tables[2].bytes,
                        [](std::uint8_t value) { return value == 0x00; }),
                "uniform black should produce black bitmap and per-line palette data");
    test.expect(result.diagnostics.size() == 1
                    && result.diagnostics.front().code
                        == "scanline-palette-deterministic-selection",
                "scanline conversion should disclose its deterministic selection difference");

    settings.scanlineStaticColorCount = 2;
    settings.paletteSelection = PaletteSelectionMode::Popularity;
    const ConversionResult staticResult = convertScanlinePaletteBitmapF18A(
        *source, settings);
    bool sharedSlotsStable = staticResult.succeeded() && staticResult.target
        && staticResult.target->tables[2].bytes.size() == 6144U;
    if (sharedSlotsStable) {
        const auto& bytes = staticResult.target->tables[2].bytes;
        for (std::size_t row = 1; row < 192U; ++row) {
            sharedSlotsStable &= std::equal(
                bytes.begin(), bytes.begin() + 4,
                bytes.begin() + static_cast<std::ptrdiff_t>(row * 32U));
        }
    }
    test.expect(sharedSlotsStable,
                "shared scanline colors should occupy stable palette slots on every row");

    settings.mode = ConversionMode::PalettedBitmapF18A;
    const ConversionResult wrongMode = convertScanlinePaletteBitmapF18A(
        *source, settings);
    test.expect(!wrongMode.succeeded()
                    && wrongMode.diagnostics.front().code
                        == "scanline-palette-f18a-wrong-mode",
                "Scanline Palette F18A should reject another selected mode");
}

void testColorMath(TestContext &test)
{
    const RgbSample black{0.0, 0.0, 0.0};
    const RgbSample white{255.0, 255.0, 255.0};
    const RgbSample red{RgbColor{255, 0, 0}};

    const auto whiteYCrCb = toYCrCb(white);
    test.expectNear(whiteYCrCb.luminance, 255.0, 1.0e-12,
                    "white should have full legacy luminance");
    test.expectNear(whiteYCrCb.redChroma, 0.0, 1.0e-12,
                    "white should have zero red chroma");
    test.expectNear(whiteYCrCb.blueChroma, 0.0, 1.0e-12,
                    "white should have zero blue chroma");

    const auto redYCrCb = toYCrCb(red);
    test.expectNear(redYCrCb.luminance, 76.245, 1.0e-12,
                    "red luminance should match the original matrix");
    test.expectNear(redYCrCb.redChroma, 127.5, 1.0e-12,
                    "red chroma should match the original matrix");
    test.expectNear(redYCrCb.blueChroma, -43.095, 1.0e-12,
                    "blue chroma should match the original matrix");

    test.expectNear(yCrCbDistanceSquared(red, black), 26484.581061, 1.0e-9,
                    "default YCrCb distance should preserve legacy luma emphasis");
    test.expectNear(yCrCbDistanceSquared(white, black), 93636.0, 1.0e-9,
                    "neutral YCrCb distance should apply luma emphasis before squaring");
    test.expectNear(perceptualRgbDistanceSquared(red, black), 19507.5, 1.0e-9,
                    "perceptual RGB distance should weight squared channel differences");

    const RgbSample diffused{-12.5, 260.0, 40.25};
    test.expectNear(yCrCbDistanceSquared(diffused, diffused), 0.0, 1.0e-12,
                    "color math should accept identical out-of-range diffusion values");
    test.expectNear(yCrCbDistanceSquared(red, white), yCrCbDistanceSquared(white, red),
                    1.0e-12, "YCrCb distance should be symmetric");

    ConversionSettings settings;
    test.expectNear(colorDistanceSquared(red, black, settings), 26484.581061, 1.0e-9,
                    "conversion settings should select YCrCb matching by default");
    settings.perceptualColorMatching = true;
    test.expectNear(colorDistanceSquared(red, black, settings), 19507.5, 1.0e-9,
                    "conversion settings should select perceptual RGB matching");
    settings.perceptualRedWeight = 1.0;
    settings.perceptualGreenWeight = 0.0;
    settings.perceptualBlueWeight = 0.0;
    test.expectNear(colorDistanceSquared(red, black, settings), 65025.0, 1.0e-9,
                    "custom perceptual weights should flow into color matching");

    test.expectNear(perceptualRgbDistanceSquared(
                        RgbSample{1.0, 2.0, 3.0},
                        RgbSample{4.0, 6.0, 8.0},
                        PerceptualRgbWeights{1.0, 2.0, 3.0}),
                    116.0,
                    1.0e-12,
                    "perceptual weights should scale squared differences, not channels");
}

void testImageAdjustments(TestContext &test)
{
    auto rgba = RgbImage::create(
        {.width = 2, .height = 1, .pixelFormat = PixelFormat::Rgba8888, .rowStride = 10},
        {0, 16, 64, 7, 128, 255, 32, 9, 201, 202});
    test.expect(rgba.has_value(), "image-adjustment fixture should be created");

    auto adjusted = adjustImage(*rgba, ConversionSettings{});
    test.expect(static_cast<bool>(adjusted) && adjusted.image->bytes() == rgba->bytes(),
                "default adjustments should copy every pixel and padding byte unchanged");

    ConversionSettings settings;
    settings.gamma = 2.0;
    adjusted = adjustImage(*rgba, settings);
    test.expect(static_cast<bool>(adjusted), "positive gamma should produce an image");
    test.expect(adjusted.image->bytes()
                    == std::vector<std::uint8_t>(
                        {0, 63, 127, 7, 180, 255, 90, 9, 201, 202}),
                "gamma should use the legacy inverse exponent and preserve alpha and padding");

    settings.gamma = 0.5;
    adjusted = adjustImage(*rgba, settings);
    test.expect(static_cast<bool>(adjusted)
                    && adjusted.image->bytes()[4] == 64
                    && adjusted.image->bytes()[5] == 255
                    && adjusted.image->bytes()[7] == 9,
                "gamma below one should darken RGB without changing alpha");

    auto greys = RgbImage::create(
        {.width = 4, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 12},
        {10, 10, 10, 20, 20, 20, 30, 30, 30, 40, 40, 40});
    settings = ConversionSettings{};
    settings.stretchHistogram = true;
    adjusted = adjustImage(*greys, settings);
    test.expect(static_cast<bool>(adjusted)
                    && adjusted.image->bytes()
                        == std::vector<std::uint8_t>(
                            {32, 32, 32, 96, 96, 96, 160, 160, 160, 224, 224, 224}),
                "histogram stretching should equalize brightness into the legacy 32-224 range");

    auto colors = RgbImage::create(
        {.width = 4, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 12},
        {10, 20, 30, 60, 70, 80, 110, 120, 130, 160, 170, 180});
    adjusted = adjustImage(*colors, settings);
    test.expect(static_cast<bool>(adjusted)
                    && adjusted.image->bytes()
                        == std::vector<std::uint8_t>(
                            {24, 34, 44, 88, 98, 108, 152, 162, 172, 216, 226, 236}),
                "brightness stretching should preserve RGB channel differences when unclipped");

    settings.gamma = 2.0;
    adjusted = adjustImage(*greys, settings);
    test.expect(static_cast<bool>(adjusted)
                    && adjusted.image->bytes()
                        == std::vector<std::uint8_t>(
                            {90, 90, 90, 156, 156, 156, 201, 201, 201, 238, 238, 238}),
                "histogram stretching should run before gamma correction");

    auto flat = RgbImage::create(
        {.width = 2, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 6},
        {40, 50, 60, 40, 50, 60});
    settings.gamma = 1.0;
    adjusted = adjustImage(*flat, settings);
    test.expect(static_cast<bool>(adjusted) && adjusted.image->bytes() == flat->bytes(),
                "a single-valued brightness histogram should remain unchanged");

    settings.gamma = 0.0;
    adjusted = adjustImage(*flat, settings);
    test.expect(!adjusted && adjusted.error == ImageAdjustmentError::InvalidGamma,
                "image adjustment should reject invalid gamma without relying on prior validation");

    settings = ConversionSettings{};
    const ImageSizeLimits smallLimits{
        .maximumWidth = 1,
        .maximumHeight = 1,
        .maximumPixels = 1,
        .maximumBytes = 4,
    };
    adjusted = adjustImage(*flat, settings, smallLimits);
    test.expect(!adjusted && adjusted.error == ImageAdjustmentError::OutputImageRejected,
                "image adjustment should enforce output allocation limits");
}

void testPaletteSelection(TestContext &test)
{
    auto ramp = RgbImage::create(
        {.width = 8, .height = 1, .pixelFormat = PixelFormat::Rgba8888, .rowStride = 34},
        {
            0, 0, 0, 1,
            16, 0, 0, 2,
            32, 0, 0, 3,
            48, 0, 0, 4,
            192, 0, 0, 5,
            208, 0, 0, 6,
            224, 0, 0, 7,
            240, 0, 0, 8,
            201, 202,
        });
    test.expect(ramp.has_value(), "palette-selection fixture should be created");

    auto selected = selectMedianCutPalette(*ramp, 2);
    test.expect(static_cast<bool>(selected) && selected.palette->size() == 2,
                "median cut should produce the requested palette size");
    test.expect(selected.palette->at(0) == RgbColor{17, 0, 0}
                    && selected.palette->at(1) == RgbColor{221, 0, 0},
                "RGB444 median cut should split the longest range and truncate block averages");

    selected = selectMedianCutPalette(*ramp, 2, MedianCutColorDepth::Rgb888);
    test.expect(static_cast<bool>(selected)
                    && selected.palette->at(0) == RgbColor{24, 0, 0}
                    && selected.palette->at(1) == RgbColor{216, 0, 0},
                "RGB888 median cut should retain full channel precision");

    const auto repeated = selectMedianCutPalette(*ramp, 4);
    const auto repeatedAgain = selectMedianCutPalette(*ramp, 4);
    test.expect(static_cast<bool>(repeated) && static_cast<bool>(repeatedAgain)
                    && repeated.palette->colors().size() == 4
                    && std::equal(repeated.palette->colors().begin(),
                                  repeated.palette->colors().end(),
                                  repeatedAgain.palette->colors().begin()),
                "median-cut tie handling should be deterministic");

    auto twoPixels = RgbImage::create(
        {.width = 2, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 6},
        {10, 20, 30, 20, 40, 60});
    selected = selectMedianCutPalette(*twoPixels, 1, MedianCutColorDepth::Rgb888);
    test.expect(static_cast<bool>(selected)
                    && selected.palette->at(0) == RgbColor{15, 30, 45},
                "a one-color median palette should be the truncated RGB average");

    auto tiedRanges = RgbImage::create(
        {.width = 4, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 12},
        {0, 0, 0, 0, 255, 0, 255, 0, 0, 255, 255, 0});
    selected = selectMedianCutPalette(*tiedRanges, 2, MedianCutColorDepth::Rgb888);
    test.expect(static_cast<bool>(selected)
                    && selected.palette->at(0) == RgbColor{0, 127, 0}
                    && selected.palette->at(1) == RgbColor{255, 127, 0},
                "equal channel ranges should retain the original red-green-blue precedence");

    std::vector<std::uint8_t> popularityPixels;
    const auto append = [&popularityPixels](RgbColor color, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            popularityPixels.push_back(color.red);
            popularityPixels.push_back(color.green);
            popularityPixels.push_back(color.blue);
        }
    };
    append({0x10, 0x10, 0x10}, 10);
    append({0x20, 0x20, 0x20}, 9);
    append({0xf0, 0x00, 0x00}, 8);
    auto popularitySource = RgbImage::create(
        {.width = 27, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 81},
        std::move(popularityPixels));
    selected = selectPopularPalette(*popularitySource, 2, PopularityWeighting::Uniform);
    test.expect(static_cast<bool>(selected) && selected.palette->size() == 2,
                "popularity selection should produce the requested number of colors");
    test.expect(selected.palette->at(0) == RgbColor{17, 17, 17}
                    && selected.palette->at(1) == RgbColor{255, 0, 0},
                "nearby popular RGB444 colors should merge before final ranking");

    auto weightedSource = RgbImage::create(
        {.width = 8, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 24},
        {
            0xf0, 0, 0,
            0xf0, 0, 0,
            0, 0xf0, 0,
            0, 0, 0xf0,
            0, 0, 0xf0,
            0xf0, 0xf0, 0,
            0, 0xf0, 0xf0,
            0xf0, 0, 0,
        });
    const auto uniform = selectPopularPalette(
        *weightedSource, 1, PopularityWeighting::Uniform);
    const auto centerWeighted = selectPopularPalette(
        *weightedSource, 1, PopularityWeighting::HorizontalCenter);
    test.expect(static_cast<bool>(uniform)
                    && uniform.palette->at(0) == RgbColor{255, 0, 0}
                    && static_cast<bool>(centerWeighted)
                    && centerWeighted.palette->at(0) == RgbColor{0, 0, 255},
                "horizontal center weighting should reproduce the original 1-2-2-3-3-2-2-1 emphasis");

    auto ties = RgbImage::create(
        {.width = 2, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 6},
        {0xf0, 0, 0, 0, 0, 0xf0});
    selected = selectPopularPalette(*ties, 2, PopularityWeighting::Uniform);
    test.expect(static_cast<bool>(selected)
                    && selected.palette->at(0) == RgbColor{0, 0, 255}
                    && selected.palette->at(1) == RgbColor{255, 0, 0},
                "popularity ties should use ascending RGB444 value order");

    selected = selectMedianCutPalette(*ramp, 0);
    test.expect(!selected && selected.error == PaletteSelectionError::InvalidColorCount,
                "palette selection should reject a zero color request");
    selected = selectPopularPalette(
        *ramp, Palette::maximumColorCount + 1, PopularityWeighting::Uniform);
    test.expect(!selected && selected.error == PaletteSelectionError::InvalidColorCount,
                "palette selection should reject more than sixteen colors");
    selected = selectMedianCutPalette(
        *ramp, 2, static_cast<MedianCutColorDepth>(255));
    test.expect(!selected && selected.error == PaletteSelectionError::UnsupportedColorDepth,
                "median cut should reject unknown color-depth options");
    selected = selectPopularPalette(
        *ramp, 2, static_cast<PopularityWeighting>(255));
    test.expect(!selected && selected.error == PaletteSelectionError::UnsupportedWeighting,
                "popularity selection should reject unknown weighting options");
}

void testRgbImage(TestContext &test)
{
    test.expect(bytesPerPixel(PixelFormat::Rgb888) == 3,
                "RGB888 should contain three bytes per pixel");
    test.expect(bytesPerPixel(PixelFormat::Rgba8888) == 4,
                "RGBA8888 should contain four bytes per pixel");
    test.expect(validateImageLayout({0, 1, PixelFormat::Rgb888, 0}).error
                    == ImageLayoutError::ZeroWidth,
                "image width should be positive");
    test.expect(validateImageLayout({1, 0, PixelFormat::Rgb888, 3}).error
                    == ImageLayoutError::ZeroHeight,
                "image height should be positive");
    test.expect(validateImageLayout({1, 1, static_cast<PixelFormat>(255), 4}).error
                    == ImageLayoutError::UnsupportedPixelFormat,
                "unknown pixel formats should be rejected");

    ImageLayoutError error = ImageLayoutError::DataSizeMismatch;
    auto image = RgbImage::createTightlyPacked(2, 3, PixelFormat::Rgb888, {}, &error);
    test.expect(image.has_value() && error == ImageLayoutError::None,
                "a small tightly packed RGB image should be created");
    test.expect(image->rowStride() == 6 && image->bytes().size() == 18,
                "a tightly packed image should derive stride and byte size");
    test.expect(image->row(2).size() == 6,
                "an image row should expose the complete stored stride");

    const ImageLayout paddedLayout{
        .width = 2,
        .height = 2,
        .pixelFormat = PixelFormat::Rgb888,
        .rowStride = 8,
    };
    image = RgbImage::create(paddedLayout, std::vector<std::uint8_t>(16), {}, &error);
    test.expect(image.has_value() && image->minimumRowBytes() == 6
                    && image->rowStride() == 8,
                "RgbImage should preserve explicit row padding");

    const ImageLayout shortStride{
        .width = 2,
        .height = 1,
        .pixelFormat = PixelFormat::Rgba8888,
        .rowStride = 7,
    };
    test.expect(validateImageLayout(shortStride).error == ImageLayoutError::RowStrideTooSmall,
                "row stride must contain every pixel in a row");

    const ImageSizeLimits smallLimits{
        .maximumWidth = 4,
        .maximumHeight = 4,
        .maximumPixels = 8,
        .maximumBytes = 32,
    };
    test.expect(validateImageLayout({5, 1, PixelFormat::Rgb888, 15}, smallLimits).error
                    == ImageLayoutError::DimensionLimitExceeded,
                "image dimensions should respect configured limits");
    test.expect(validateImageLayout({4, 3, PixelFormat::Rgb888, 12}, smallLimits).error
                    == ImageLayoutError::PixelLimitExceeded,
                "image pixel count should respect configured limits");
    test.expect(validateImageLayout({4, 2, PixelFormat::Rgba8888, 20}, smallLimits).error
                    == ImageLayoutError::ByteLimitExceeded,
                "image allocation size should respect configured limits");

    image = RgbImage::create({2, 2, PixelFormat::Rgb888, 6},
                             std::vector<std::uint8_t>(11),
                             {},
                             &error);
    test.expect(!image.has_value() && error == ImageLayoutError::DataSizeMismatch,
                "RgbImage should reject a buffer whose size does not match its layout");

    const ImageSizeLimits overflowLimits{
        .maximumWidth = std::numeric_limits<std::uint32_t>::max(),
        .maximumHeight = std::numeric_limits<std::uint32_t>::max(),
        .maximumPixels = std::numeric_limits<std::size_t>::max(),
        .maximumBytes = std::numeric_limits<std::size_t>::max(),
    };
    const ImageLayout overflowingLayout{
        .width = 1,
        .height = 2,
        .pixelFormat = PixelFormat::Rgb888,
        .rowStride = std::numeric_limits<std::size_t>::max(),
    };
    test.expect(validateImageLayout(overflowingLayout, overflowLimits).error
                    == ImageLayoutError::ByteSizeOverflow,
                "row-stride multiplication should reject integer overflow");
}

void testConversionTypes(TestContext &test)
{
    auto source = RgbImage::createTightlyPacked(2, 2, PixelFormat::Rgba8888);
    test.expect(source.has_value(), "conversion-type fixture should be created");

    ConversionRequest request{
        .source = std::make_shared<const RgbImage>(std::move(*source)),
        .settings = {},
    };
    test.expect(validate(request).empty(), "a request with a source and valid settings should pass");

    request.settings.gamma = 0.0;
    const auto invalidSettings = validate(request);
    test.expect(invalidSettings.size() == 1 && invalidSettings.front().field == "gamma",
                "request validation should include settings issues");

    request.source.reset();
    const auto invalidRequest = validate(request);
    test.expect(invalidRequest.size() == 2 && invalidRequest.front().field == "source",
                "request validation should report a missing source before settings issues");

    ConversionResult result{
        .status = ConversionStatus::Succeeded,
        .preview = std::nullopt,
        .diagnostics = {{DiagnosticSeverity::Warning, "palette-reduced", "Palette reduced."}},
    };
    test.expect(result.succeeded() && !result.hasErrors(),
                "warnings should not turn a successful conversion into a failure");

    result.diagnostics.push_back(
        ConversionDiagnostic{DiagnosticSeverity::Error, "invalid-source", "Invalid source."});
    test.expect(result.hasErrors() && !result.succeeded(),
                "error diagnostics should make a result unsuccessful");

    result.status = ConversionStatus::Cancelled;
    result.diagnostics.clear();
    test.expect(!result.succeeded() && !result.hasErrors(),
                "cancellation should remain distinct from an error diagnostic");
}

void testConversionJobController(TestContext &test)
{
    auto image = RgbImage::createTightlyPacked(2, 2, PixelFormat::Rgb888);
    test.expect(image.has_value(), "conversion-job fixture should be created");
    auto source = std::make_shared<const RgbImage>(std::move(*image));

    ConversionJobController controller;
    const ConversionRequest first = controller.begin(source, ConversionSettings{});
    test.expect(first.generation == 1
                    && !first.cancellation.isCancellationRequested()
                    && controller.isCurrent(first.generation),
                "the first preview request should receive generation one");

    const ConversionRequest second = controller.begin(source, ConversionSettings{});
    test.expect(second.generation == 2
                    && first.cancellation.isCancellationRequested()
                    && !second.cancellation.isCancellationRequested()
                    && !controller.isCurrent(first.generation)
                    && controller.isCurrent(second.generation),
                "a newer preview request should cancel and supersede its predecessor");

    ConversionResult completed{
        .status = ConversionStatus::Succeeded,
        .preview = *source,
        .diagnostics = {},
        .target = std::nullopt,
    };
    const ConversionResult stale = controller.finalize(first, completed);
    test.expect(stale.status == ConversionStatus::Cancelled
                    && stale.generation == first.generation
                    && !stale.preview && !stale.target
                    && stale.diagnostics.size() == 1
                    && stale.diagnostics.front().code == "conversion-cancelled"
                    && !controller.accepts(stale),
                "finalization should strip cancelled stale output before publication");

    const ConversionResult current = controller.finalize(second, completed);
    test.expect(current.succeeded() && current.generation == second.generation
                    && current.preview && controller.accepts(current),
                "the newest completed generation should be accepted");

    controller.cancelCurrent();
    const ConversionResult cancelled = controller.finalize(second, completed);
    test.expect(cancelled.status == ConversionStatus::Cancelled
                    && !controller.accepts(cancelled),
                "explicit cancellation should prevent publication of the current job");
}

void testConverterCancellation(TestContext &test)
{
    auto image = RgbImage::createTightlyPacked(256, 192, PixelFormat::Rgb888);
    test.expect(image.has_value(), "converter-cancellation fixture should be created");
    if (!image) {
        return;
    }

    const Palette palette = defaultBitmap9918Palette();
    CancellationSource cancellationSource;
    cancellationSource.requestCancellation();
    const auto expectCancelled = [&test](const ConversionResult& result) {
        test.expect(result.status == ConversionStatus::Cancelled
                        && !result.preview && !result.target
                        && result.diagnostics.empty(),
                    "a pre-cancelled converter should stop without publishing output");
    };

    ConversionSettings settings;
    expectCancelled(convertBitmap9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::GreyscaleBitmap9918;
    expectCancelled(convertGreyscaleBitmap9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::BlackAndWhiteBitmap9918;
    expectCancelled(convertBlackAndWhiteBitmap9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::Multicolor9918;
    expectCancelled(convertMulticolor9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::DualMulticolor9918;
    expectCancelled(convertDualMulticolor9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::HalfMulticolor9918;
    expectCancelled(convertHalfMulticolor9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::BitmapColorOnly9918;
    expectCancelled(convertBitmapColorOnly9918(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::PalettedBitmapF18A;
    expectCancelled(convertPalettedBitmapF18A(
        *image, palette, settings, cancellationSource.token()));

    settings.mode = ConversionMode::ScanlinePaletteBitmapF18A;
    expectCancelled(convertScanlinePaletteBitmapF18A(
        *image, settings, cancellationSource.token()));

    settings.targetProfile = TargetProfileId::SegaMasterSystem;
    settings.mode = ConversionMode::Mode4Sms192;
    expectCancelled(convertSegaSmsMode4(
        *image, settings, cancellationSource.token()));

    settings.targetProfile = TargetProfileId::SegaGenesis;
    settings.mode = ConversionMode::Mode5GenesisH32;
    settings.targetWidth = 256;
    settings.targetHeight = 224;
    auto genesisImage = RgbImage::createTightlyPacked(256, 224, PixelFormat::Rgb888);
    test.expect(genesisImage.has_value(),
                "the Genesis cancellation source should be allocated");
    if (genesisImage) {
        expectCancelled(convertSegaGenesisMode5(
            *genesisImage, settings, cancellationSource.token()));
    }

    settings.targetProfile = TargetProfileId::HuC6270;
    settings.mode = ConversionMode::HuC6270Background256;
    settings.targetWidth = 256;
    settings.targetHeight = 224;
    if (genesisImage) {
        expectCancelled(convertHuC6270Background(
            *genesisImage, settings, cancellationSource.token()));
    }

    settings.targetProfile = TargetProfileId::VicII;
    settings.mode = ConversionMode::VicIIHiresCharacter;
    settings.targetWidth = 320;
    settings.targetHeight = 200;
    auto vicIIImage = RgbImage::createTightlyPacked(320, 200, PixelFormat::Rgb888);
    test.expect(vicIIImage.has_value(),
                "the VIC-II cancellation source should be allocated");
    if (vicIIImage) {
        expectCancelled(convertCommodoreDisplay(
            *vicIIImage, settings, cancellationSource.token()));
    }
}

void testConversionMemoryEstimate(TestContext &test)
{
    ConversionSettings settings;
    const ConversionMemoryEstimate bitmap = estimateConversionMemory(settings);
    test.expect(bitmap.sourceImageBytes == 147456
                    && bitmap.previewImageBytes == 147456
                    && bitmap.indexedImageBytes == 49152
                    && bitmap.errorDiffusionBytes == 1179648
                    && bitmap.targetTableBytes == 12288
                    && bitmap.paletteBytes == 0
                    && bitmap.totalBytes() == 1536000,
                "default Bitmap memory accounting should cover its large owned buffers");

    settings.mode = ConversionMode::Multicolor9918;
    const ConversionMemoryEstimate multicolor = estimateConversionMemory(settings);
    test.expect(multicolor.errorDiffusionBytes == 0
                    && multicolor.targetTableBytes == 1536
                    && multicolor.totalBytes() == 345600,
                "Multicolor memory accounting should omit its unused error buffer");

    settings.mode = ConversionMode::ScanlinePaletteBitmapF18A;
    const ConversionMemoryEstimate scanline = estimateConversionMemory(settings);
    test.expect(scanline.paletteBytes == 8640
                    && scanline.targetTableBytes == 18432
                    && scanline.totalBytes() == 1550784,
                "scanline F18A memory accounting should include all row palettes");
}

void testTargetProfiles(TestContext &test)
{
    const auto profiles = retrovdp::core::targetProfiles();
    test.expect(profiles.size() == 9,
                "the registry should publish the implemented VDP profiles");

    const auto& tms9918 = retrovdp::core::targetProfile(TargetProfileId::Tms9918A);
    const auto& f18a = retrovdp::core::targetProfile(TargetProfileId::F18A);
    const auto& v9938 = retrovdp::core::targetProfile(TargetProfileId::V9938);
    const auto& v9958 = retrovdp::core::targetProfile(TargetProfileId::V9958);
    const auto& sms = retrovdp::core::targetProfile(
        TargetProfileId::SegaMasterSystem);
    const auto& genesis = retrovdp::core::targetProfile(
        TargetProfileId::SegaGenesis);
    const auto& huc6270 = retrovdp::core::targetProfile(TargetProfileId::HuC6270);
    const auto& vicII = retrovdp::core::targetProfile(TargetProfileId::VicII);
    const auto& vic = retrovdp::core::targetProfile(TargetProfileId::Vic);
    test.expect(tms9918.stableId == "tms9918a"
                    && tms9918.status == TargetProfileStatus::Implemented
                    && retrovdp::core::hasCapability(
                        tms9918.capabilities, TargetCapability::CharacterPatterns)
                    && tms9918.characterPatterns.pixelWidth == 8
                    && tms9918.characterPatterns.pixelHeight == 8
                    && tms9918.characterPatterns.patternsPerSet == 256
                    && tms9918.characterPatterns.setCount == 3
                    && tms9918.characterPatterns.mapColumns == 32
                    && tms9918.characterPatterns.mapRows == 24
                    && tms9918.sprites.minimumPixelSize == 8
                    && tms9918.sprites.maximumPixelSize == 16
                    && tms9918.sprites.patternsPerSet == 32
                    && tms9918.sprites.maximumVisibleSprites == 32
                    && tms9918.sprites.maximumColorDepth == 1
                    && tms9918.sprites.usesGlobalSize
                    && !tms9918.sprites.supportsPerSpriteSize,
                "the TMS9918A profile should expose its stable identity and capabilities");
    test.expect(f18a.stableId == "f18a"
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::F18A, ConversionMode::Bitmap9918)
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::F18A, ConversionMode::PalettedBitmapF18A)
                    && f18a.sprites.maximumColorDepth == 3
                    && !f18a.sprites.usesGlobalSize
                    && f18a.sprites.supportsPerSpriteSize,
                "the F18A profile should support compatible base and enhanced modes");
    test.expect(v9938.stableId == "v9938"
                    && v9938.status == TargetProfileStatus::Implemented
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::V9938, ConversionMode::Screen5V9938)
                    && v9938.sprites.maximumColorDepth == 4
                    && v9958.stableId == "v9958"
                    && v9958.status == TargetProfileStatus::Implemented
                    && retrovdp::core::supportsConversionMode(
                    TargetProfileId::V9958, ConversionMode::Screen12V9958),
                "the Yamaha VDP profiles should publish their native bitmap modes");
    test.expect(sms.stableId == "sega-sms-vdp"
                    && sms.status == TargetProfileStatus::Implemented
                    && sms.nominalVramBytes == 16U * 1024U
                    && sms.characterPatterns.pixelWidth == 8
                    && sms.characterPatterns.pixelHeight == 8
                    && sms.characterPatterns.patternsPerSet == 448
                    && sms.characterPatterns.mapColumns == 32
                    && sms.characterPatterns.mapRows == 28
                    && sms.sprites.patternsPerSet == 64
                    && sms.sprites.maximumVisibleSprites == 64
                    && sms.sprites.maximumColorDepth == 4
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::SegaMasterSystem,
                        ConversionMode::Mode4Sms192)
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::SegaMasterSystem,
                        ConversionMode::Bitmap9918),
                "the Master System profile should expose Mode 4 and documented legacy compatibility");
    test.expect(genesis.stableId == "sega-genesis-vdp"
                    && genesis.status == TargetProfileStatus::Implemented
                    && genesis.nominalVramBytes == 64U * 1024U
                    && genesis.characterPatterns.pixelWidth == 8
                    && genesis.characterPatterns.pixelHeight == 8
                    && genesis.characterPatterns.patternsPerSet == 2048
                    && genesis.characterPatterns.mapColumns == 40
                    && genesis.characterPatterns.mapRows == 28
                    && genesis.sprites.minimumPixelSize == 8
                    && genesis.sprites.maximumPixelSize == 32
                    && genesis.sprites.patternsPerSet == 80
                    && genesis.sprites.maximumVisibleSprites == 80
                    && genesis.sprites.maximumColorDepth == 4
                    && !genesis.sprites.usesGlobalSize
                    && genesis.sprites.supportsPerSpriteSize
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::SegaGenesis,
                        ConversionMode::Mode5GenesisH32)
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::SegaGenesis,
                        ConversionMode::Mode5GenesisH40Pal)
                    && !retrovdp::core::supportsConversionMode(
                        TargetProfileId::SegaGenesis,
                        ConversionMode::Bitmap9918),
                "the Genesis profile should expose native Mode V character, map, and sprite limits");
    test.expect(huc6270.stableId == "huc6270"
                    && huc6270.status == TargetProfileStatus::Implemented
                    && huc6270.nominalVramBytes == 64U * 1024U
                    && huc6270.characterPatterns.patternsPerSet == 1920
                    && huc6270.sprites.patternsPerSet == 64
                    && huc6270.sprites.maximumColorDepth == 4
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::HuC6270,
                        ConversionMode::HuC6270Background320),
                "the HuC6270 profile should expose its native tile, BAT, palette, and sprite limits");
    test.expect(vicII.stableId == "vic-ii"
                    && vicII.status == TargetProfileStatus::Implemented
                    && vicII.characterPatterns.mapColumns == 40
                    && vicII.characterPatterns.mapRows == 25
                    && vicII.sprites.patternsPerSet == 8
                    && vicII.sprites.maximumColorDepth == 2
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::VicII,
                        ConversionMode::VicIIMulticolorBitmap)
                    && vic.stableId == "vic"
                    && vic.status == TargetProfileStatus::Implemented
                    && vic.characterPatterns.mapColumns == 22
                    && vic.characterPatterns.mapRows == 23
                    && !retrovdp::core::hasCapability(
                        vic.capabilities, TargetCapability::Sprites)
                    && retrovdp::core::supportsConversionMode(
                        TargetProfileId::Vic,
                        ConversionMode::VicMulticolorCharacter),
                "the VIC-II and VIC profiles should expose native character, bitmap, and sprite capabilities");
    test.expect(!retrovdp::core::supportsConversionMode(
                    TargetProfileId::Tms9918A, ConversionMode::PalettedBitmapF18A),
                "target profiles should reject modes outside their capabilities");
    test.expect(retrovdp::core::targetProfileId("f18a") == TargetProfileId::F18A
                    && !retrovdp::core::targetProfileId("unknown").has_value(),
                "stable target identifiers should round-trip and reject unknown values");
    test.expect(retrovdp::core::effectiveTargetProfile(
                    TargetProfileId::V9938, ConversionMode::Bitmap9918)
                    == TargetProfileId::V9938,
                "implemented Yamaha profiles should retain compatible TMS modes");

    retrovdp::core::StableIdError idError = retrovdp::core::StableIdError::None;
    const auto validId = retrovdp::core::ModeId::create("bitmap-9918a", &idError);
    test.expect(validId.has_value() && idError == retrovdp::core::StableIdError::None,
                "stable IDs should accept lowercase, versionable identifiers");
    test.expect(!retrovdp::core::ModeId::create("Bitmap 9918A", &idError).has_value()
                    && idError == retrovdp::core::StableIdError::InvalidFirstCharacter,
                "stable IDs should reject display labels and uppercase identifiers");
    test.expect(!retrovdp::core::ModeId::create("bitmap--9918a", &idError).has_value()
                    && idError == retrovdp::core::StableIdError::ConsecutiveSeparator,
                "stable IDs should reject ambiguous separators");

    const auto modes = retrovdp::core::displayModes();
    test.expect(modes.size() == 31 && retrovdp::core::validateRegistry(),
                "the target and display-mode registries should validate as one contract");
    const auto& f18aMode = retrovdp::core::displayMode(
        ConversionMode::PalettedBitmapF18A);
    test.expect(f18aMode.stableId == "paletted-bitmap-f18a"
                    && f18aMode.primaryTarget == TargetProfileId::F18A
                    && f18aMode.geometry.width == 256
                    && f18aMode.geometry.height == 192
                    && f18aMode.palette.model == retrovdp::core::PaletteModel::ProgrammableRgb
                    && f18aMode.palette.channelBits == 4
                    && retrovdp::core::hasOption(
                        f18aMode.options, retrovdp::core::ModeOption::PaletteSelection)
                    && !retrovdp::core::hasOption(
                        f18aMode.options, retrovdp::core::ModeOption::WorkingPalette),
                "display-mode descriptors should own stable identity and hardware constraints");
    test.expect(retrovdp::core::hasOption(
                    retrovdp::core::displayMode(ConversionMode::Bitmap9918).options,
                    retrovdp::core::ModeOption::WorkingPalette)
                    && retrovdp::core::hasOption(
                        retrovdp::core::displayMode(
                            ConversionMode::ScanlinePaletteBitmapF18A).options,
                        retrovdp::core::ModeOption::ScanlinePalette),
                "display modes should describe their applicable settings without UI mode-number checks");
    const auto& screen7 = retrovdp::core::displayMode(ConversionMode::Screen7V9938);
    const auto& screen10 = retrovdp::core::displayMode(ConversionMode::Screen10V9958);
    const auto& screen12 = retrovdp::core::displayMode(ConversionMode::Screen12V9958);
    test.expect(screen7.geometry.width == 512 && screen7.geometry.height == 212
                    && screen7.geometry.pixelAspectRatio.numerator == 1
                    && screen7.geometry.pixelAspectRatio.denominator == 2
                    && screen7.palette.entryCount == 16
                    && screen7.palette.channelBits == 3
                    && screen10.palette.model
                        == retrovdp::core::PaletteModel::YjkWithPalette
                    && screen10.palette.entryCount == 16
                    && screen10.palette.channelBits == 3
                    && screen12.palette.model == retrovdp::core::PaletteModel::Yjk
                    && screen12.palette.channelBits == 0,
                "Yamaha mode descriptors should retain native geometry and color depth");
    const auto& sms192 = retrovdp::core::displayMode(ConversionMode::Mode4Sms192);
    const auto& sms224 = retrovdp::core::displayMode(ConversionMode::Mode4Sms224);
    const auto& sms240 = retrovdp::core::displayMode(ConversionMode::Mode4Sms240);
    test.expect(sms192.stableId == "mode-4-sms-192"
                    && sms192.geometry.width == 256 && sms192.geometry.height == 192
                    && sms224.geometry.height == 224 && sms240.geometry.height == 240
                    && sms192.palette.entryCount == 32
                    && sms192.palette.workingColorCount == 32
                    && sms192.palette.channelBits == 2
                    && retrovdp::core::hasOption(
                        sms192.options, retrovdp::core::ModeOption::PaletteSelection),
                "Master System Mode 4 descriptors should cover every supported display height");
    const auto& genesisH32 = retrovdp::core::displayMode(
        ConversionMode::Mode5GenesisH32);
    const auto& genesisH40 = retrovdp::core::displayMode(
        ConversionMode::Mode5GenesisH40);
    const auto& genesisH32Pal = retrovdp::core::displayMode(
        ConversionMode::Mode5GenesisH32Pal);
    const auto& genesisH40Pal = retrovdp::core::displayMode(
        ConversionMode::Mode5GenesisH40Pal);
    test.expect(genesisH32.stableId == "mode-5-genesis-h32"
                    && genesisH32.geometry.width == 256
                    && genesisH32.geometry.height == 224
                    && genesisH40.geometry.width == 320
                    && genesisH40.geometry.height == 224
                    && genesisH32Pal.geometry.width == 256
                    && genesisH32Pal.geometry.height == 240
                    && genesisH40Pal.geometry.width == 320
                    && genesisH40Pal.geometry.height == 240
                    && genesisH40.palette.entryCount == 64
                    && genesisH40.palette.workingColorCount == 61
                    && genesisH40.palette.channelBits == 3
                    && retrovdp::core::hasOption(
                        genesisH40.options,
                        retrovdp::core::ModeOption::PaletteSelection),
                "Genesis Mode V descriptors should cover H32/H40 and 224/240-line displays");
    test.expect(retrovdp::core::conversionMode("bitmap-9918a")
                    == ConversionMode::Bitmap9918
                    && !retrovdp::core::conversionMode("unknown-mode").has_value(),
                "display-mode identifiers should round-trip and reject unknown values");

    std::array duplicateProfiles{profiles[0], profiles[0]};
    test.expect(retrovdp::core::validateRegistry(duplicateProfiles, modes).error
                    == retrovdp::core::RegistryError::DuplicateTargetProfile,
                "registry validation should reject duplicate legacy target identities");
    std::vector<retrovdp::core::TargetProfile> invalidCharacterProfiles(
        profiles.begin(), profiles.end());
    invalidCharacterProfiles[0].characterPatterns.pixelWidth = 0;
    test.expect(retrovdp::core::validateRegistry(
                    invalidCharacterProfiles, modes).error
                    == retrovdp::core::RegistryError::InvalidCharacterPatterns,
                "registry validation should reject invalid target character geometry");
    std::vector<retrovdp::core::TargetProfile> invalidSpriteProfiles(
        profiles.begin(), profiles.end());
    invalidSpriteProfiles[0].sprites.maximumColorDepth = 0;
    test.expect(retrovdp::core::validateRegistry(
                    invalidSpriteProfiles, modes).error
                    == retrovdp::core::RegistryError::InvalidSprites,
                "registry validation should reject invalid target sprite constraints");
    std::array invalidModes{modes[0]};
    invalidModes[0].geometry.width = 0;
    test.expect(retrovdp::core::validateRegistry(profiles, invalidModes).error
                    == retrovdp::core::RegistryError::InvalidGeometry,
                "registry validation should reject invalid display geometry");
    std::vector<retrovdp::core::DisplayModeDescriptor> invalidPrimaryModes(
        modes.begin(), modes.end());
    invalidPrimaryModes[7].primaryTarget = TargetProfileId::V9938;
    test.expect(retrovdp::core::validateRegistry(profiles, invalidPrimaryModes).error
                    == retrovdp::core::RegistryError::PrimaryTargetDoesNotSupportMode,
                "registry validation should reject a primary target that cannot compile its mode");
}

void testYamahaBitmapConversion(TestContext &test)
{
    struct YamahaModeCase {
        ConversionMode mode;
        TargetProfileId profile;
        std::uint32_t width;
        std::size_t framebufferBytes;
        bool hasPalette;
    };
    constexpr std::array cases{
        YamahaModeCase{ConversionMode::Screen5V9938, TargetProfileId::V9938,
                       256, 27136, true},
        YamahaModeCase{ConversionMode::Screen6V9938, TargetProfileId::V9938,
                       512, 27136, true},
        YamahaModeCase{ConversionMode::Screen7V9938, TargetProfileId::V9938,
                       512, 54272, true},
        YamahaModeCase{ConversionMode::Screen8V9938, TargetProfileId::V9938,
                       256, 54272, false},
        YamahaModeCase{ConversionMode::Screen10V9958, TargetProfileId::V9958,
                       256, 54272, true},
        YamahaModeCase{ConversionMode::Screen11V9958, TargetProfileId::V9958,
                       256, 54272, true},
        YamahaModeCase{ConversionMode::Screen12V9958, TargetProfileId::V9958,
                       256, 54272, false},
    };

    for (const auto& modeCase : cases) {
        auto source = RgbImage::createTightlyPacked(
            modeCase.width, 212, PixelFormat::Rgb888);
        test.expect(source.has_value(),
                    "the Yamaha bitmap test source should be allocated");
        if (!source) continue;
        for (std::uint32_t y = 0; y < source->height(); ++y)
            std::ranges::fill(source->row(y), std::uint8_t{0});

        ConversionSettings settings;
        settings.targetProfile = modeCase.profile;
        settings.mode = modeCase.mode;
        settings.dither = DitherMode::None;
        std::size_t progressFrames = 0;
        std::uint32_t lastCompletedRows = 0;
        std::uint32_t reportedTotalRows = 0;
        bool progressGeometryValid = true;
        const ConversionResult result = convertYamahaBitmap(
            *source,
            settings,
            {},
            [&progressFrames, &lastCompletedRows, &reportedTotalRows,
             &progressGeometryValid](
                const RgbImage& preview,
                std::uint32_t completedRows,
                std::uint32_t totalRows) {
                ++progressFrames;
                lastCompletedRows = completedRows;
                reportedTotalRows = totalRows;
                progressGeometryValid = progressGeometryValid
                    && preview.width() != 0 && preview.height() != 0;
            });
        const bool tablesValid = result.target
            && static_cast<bool>(validateTargetTables(*result.target));
        test.expect(result.succeeded() && result.preview && tablesValid,
                    "every native Yamaha bitmap mode should compile valid target data");
        test.expect(progressFrames > 0 && progressGeometryValid
                        && lastCompletedRows == source->height()
                        && reportedTotalRows == source->height(),
                    "every native Yamaha bitmap mode should publish live progress through its final row");
        if (!result.target) continue;
        test.expect(result.target->tables.front().role == TargetTableRole::Framebuffer
                        && result.target->tables.front().bytes.size()
                            == modeCase.framebufferBytes,
                    "Yamaha modes should emit their native VRAM framebuffer size");
        test.expect(result.target->tables.size() == (modeCase.hasPalette ? 2U : 1U)
                        && (!modeCase.hasPalette
                            || result.target->tables[1].role == TargetTableRole::Palette),
                    "Yamaha modes should emit palette data only when the mode uses it");
    }
}

void testSegaSmsMode4Conversion(TestContext& test)
{
    struct ModeCase {
        ConversionMode mode;
        std::uint32_t height;
        std::size_t patternBytes;
        std::uint8_t register0;
        std::uint8_t register1;
    };
    constexpr std::array cases{
        ModeCase{ConversionMode::Mode4Sms192, 192, 0x3800, 0x04, 0xc0},
        ModeCase{ConversionMode::Mode4Sms224, 224, 0x3700, 0x06, 0xc0},
        ModeCase{ConversionMode::Mode4Sms240, 240, 0x3700, 0x04, 0xc8},
    };

    for (const auto& modeCase : cases) {
        auto source = RgbImage::createTightlyPacked(
            256, modeCase.height, PixelFormat::Rgb888);
        test.expect(source.has_value(),
                    "the Master System test source should be allocated");
        if (!source) continue;
        constexpr std::array colors{
            RgbColor{0, 0, 0}, RgbColor{255, 0, 0},
            RgbColor{0, 255, 0}, RgbColor{0, 0, 255},
        };
        for (std::uint32_t y = 0; y < source->height(); ++y) {
            auto row = source->row(y);
            for (std::uint32_t x = 0; x < source->width(); ++x) {
                const RgbColor color = colors[((x / 8U) + (y / 8U)) % colors.size()];
                const std::size_t offset = static_cast<std::size_t>(x) * 3U;
                row[offset] = color.red;
                row[offset + 1U] = color.green;
                row[offset + 2U] = color.blue;
            }
        }

        ConversionSettings settings;
        settings.targetProfile = TargetProfileId::SegaMasterSystem;
        settings.mode = modeCase.mode;
        settings.dither = DitherMode::None;
        settings.paletteSelection = retrovdp::core::PaletteSelectionMode::Popularity;
        std::uint32_t lastProgressRow = 0;
        const ConversionResult result = convertSegaSmsMode4(
            *source, settings, {},
            [&lastProgressRow](const RgbImage&, std::uint32_t completed,
                               std::uint32_t) { lastProgressRow = completed; });
        test.expect(result.succeeded() && result.preview && result.target
                        && validateTargetTables(*result.target),
                    "every Master System Mode 4 height should compile valid hardware tables");
        if (!result.preview || !result.target) continue;
        test.expect(lastProgressRow == modeCase.height
                        && result.preview->width() == 256
                        && result.preview->height() == modeCase.height,
                    "Master System conversion should publish native-size live progress");
        test.expect(result.target->profile == TargetProfileId::SegaMasterSystem
                        && result.target->tables.size() == 4
                        && result.target->tables[0].role == TargetTableRole::Pattern
                        && result.target->tables[0].bytes.size() == modeCase.patternBytes
                        && result.target->tables[1].role == TargetTableRole::TileMap
                        && result.target->tables[1].bytes.size() == 2048
                        && result.target->tables[2].role == TargetTableRole::Palette
                        && result.target->tables[2].bytes.size() == 32
                        && result.target->tables[3].role
                            == TargetTableRole::DisplayRegisters
                        && result.target->tables[3].bytes.size() == 11,
                    "Master System output should contain planar tiles, name table, CRAM, and registers");
        test.expect(std::ranges::all_of(
                        result.target->tables[2].bytes,
                        [](std::uint8_t value) { return value <= 0x3fU; })
                        && result.target->tables[3].bytes[0] == modeCase.register0
                        && result.target->tables[3].bytes[1] == modeCase.register1,
                    "Master System CRAM and display-mode register data should use hardware encodings");

        const auto& patterns = result.target->tables[0].bytes;
        const auto& nameTable = result.target->tables[1].bytes;
        const auto& cram = result.target->tables[2].bytes;
        const std::uint16_t word = static_cast<std::uint16_t>(nameTable[0])
            | (static_cast<std::uint16_t>(nameTable[1]) << 8U);
        const std::size_t pattern = word & 0x01ffU;
        const bool flipX = (word & 0x0200U) != 0U;
        const bool flipY = (word & 0x0400U) != 0U;
        const std::size_t bank = (word & 0x0800U) != 0U ? 1U : 0U;
        const std::size_t patternX = flipX ? 7U : 0U;
        const std::size_t patternY = flipY ? 7U : 0U;
        std::uint8_t colorIndex = 0;
        for (std::size_t plane = 0; plane < 4; ++plane) {
            const std::uint8_t planeByte =
                patterns[pattern * 32U + patternY * 4U + plane];
            if ((planeByte & (0x80U >> patternX)) != 0U)
                colorIndex = static_cast<std::uint8_t>(colorIndex | (1U << plane));
        }
        const std::uint8_t cramColor = cram[bank * 16U + colorIndex];
        const auto previewPixel = result.preview->row(0);
        test.expect(previewPixel[0] == static_cast<std::uint8_t>((cramColor & 3U) * 85U)
                        && previewPixel[1]
                            == static_cast<std::uint8_t>(((cramColor >> 2U) & 3U) * 85U)
                        && previewPixel[2]
                            == static_cast<std::uint8_t>(((cramColor >> 4U) & 3U) * 85U),
                    "the Mode 4 preview should decode the emitted tile, map, and CRAM data");
    }

    auto wrongSize = RgbImage::createTightlyPacked(255, 192, PixelFormat::Rgb888);
    ConversionSettings settings;
    settings.targetProfile = TargetProfileId::SegaMasterSystem;
    settings.mode = ConversionMode::Mode4Sms192;
    const ConversionResult invalid = convertSegaSmsMode4(*wrongSize, settings);
    test.expect(!invalid.succeeded() && !invalid.diagnostics.empty()
                    && invalid.diagnostics.front().code == "sms-mode4-invalid-dimensions",
                "the Master System converter should reject non-native source geometry");
}

void testSegaGenesisMode5Conversion(TestContext& test)
{
    struct ModeCase {
        ConversionMode mode;
        std::uint32_t width;
        std::uint32_t height;
        std::size_t patternBytes;
        std::size_t mapBytes;
        std::uint8_t register1;
        std::uint8_t register5;
        std::uint8_t register12;
    };
    constexpr std::array cases{
        ModeCase{ConversionMode::Mode5GenesisH32, 256, 224,
                 0xb000, 2048, 0x74, 0x5f, 0x00},
        ModeCase{ConversionMode::Mode5GenesisH40, 320, 224,
                 0xa800, 4096, 0x74, 0x54, 0x81},
        ModeCase{ConversionMode::Mode5GenesisH32Pal, 256, 240,
                 0xb000, 2048, 0x7c, 0x5f, 0x00},
        ModeCase{ConversionMode::Mode5GenesisH40Pal, 320, 240,
                 0xa800, 4096, 0x7c, 0x54, 0x81},
    };

    for (const auto& modeCase : cases) {
        auto source = RgbImage::createTightlyPacked(
            modeCase.width, modeCase.height, PixelFormat::Rgb888);
        test.expect(source.has_value(),
                    "the Genesis test source should be allocated");
        if (!source) continue;
        constexpr std::array colors{
            RgbColor{0, 0, 0}, RgbColor{255, 0, 0},
            RgbColor{0, 255, 0}, RgbColor{0, 0, 255},
        };
        for (std::uint32_t y = 0; y < source->height(); ++y) {
            auto row = source->row(y);
            for (std::uint32_t x = 0; x < source->width(); ++x) {
                const RgbColor color = colors[((x / 8U) + (y / 8U)) % colors.size()];
                const std::size_t offset = static_cast<std::size_t>(x) * 3U;
                row[offset] = color.red;
                row[offset + 1U] = color.green;
                row[offset + 2U] = color.blue;
            }
        }

        ConversionSettings settings;
        settings.targetProfile = TargetProfileId::SegaGenesis;
        settings.mode = modeCase.mode;
        settings.targetWidth = modeCase.width;
        settings.targetHeight = modeCase.height;
        settings.dither = DitherMode::None;
        settings.paletteSelection = retrovdp::core::PaletteSelectionMode::Popularity;
        std::uint32_t lastProgressRow = 0;
        const ConversionResult result = convertSegaGenesisMode5(
            *source, settings, {},
            [&lastProgressRow](const RgbImage&, std::uint32_t completed,
                               std::uint32_t) { lastProgressRow = completed; });
        test.expect(result.succeeded() && result.preview && result.target
                        && validateTargetTables(*result.target),
                    "every standard Genesis Mode V geometry should compile valid hardware tables");
        if (!result.preview || !result.target) continue;
        test.expect(lastProgressRow == modeCase.height
                        && result.preview->width() == modeCase.width
                        && result.preview->height() == modeCase.height,
                    "Genesis conversion should publish native-size live progress");
        test.expect(result.target->profile == TargetProfileId::SegaGenesis
                        && result.target->tables.size() == 4
                        && result.target->tables[0].role == TargetTableRole::Pattern
                        && result.target->tables[0].bytes.size() == modeCase.patternBytes
                        && result.target->tables[1].role == TargetTableRole::TileMap
                        && result.target->tables[1].bytes.size() == modeCase.mapBytes
                        && result.target->tables[2].role == TargetTableRole::Palette
                        && result.target->tables[2].bytes.size() == 128
                        && result.target->tables[3].role
                            == TargetTableRole::DisplayRegisters
                        && result.target->tables[3].bytes.size() == 24,
                    "Genesis output should contain packed tiles, Plane A map, CRAM, and registers");
        const auto& registers = result.target->tables[3].bytes;
        bool validCram = true;
        const auto& cram = result.target->tables[2].bytes;
        for (std::size_t offset = 0; offset < cram.size(); offset += 2U) {
            const std::uint16_t word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(cram[offset]) << 8U)
                | cram[offset + 1U]);
            validCram &= (word & static_cast<std::uint16_t>(~0x0eeeU)) == 0U;
        }
        test.expect(validCram
                        && registers[1] == modeCase.register1
                        && registers[5] == modeCase.register5
                        && registers[12] == modeCase.register12,
                    "Genesis CRAM and display registers should use native big-endian RGB333 encodings");

        const auto& patterns = result.target->tables[0].bytes;
        const auto& nameTable = result.target->tables[1].bytes;
        const std::uint16_t mapWord = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(nameTable[0]) << 8U) | nameTable[1]);
        const std::size_t pattern = mapWord & 0x07ffU;
        const bool flipX = (mapWord & 0x0800U) != 0U;
        const bool flipY = (mapWord & 0x1000U) != 0U;
        const std::size_t bank = (mapWord >> 13U) & 0x03U;
        const std::size_t patternX = flipX ? 7U : 0U;
        const std::size_t patternY = flipY ? 7U : 0U;
        const std::uint8_t packed = patterns[
            pattern * 32U + patternY * 4U + patternX / 2U];
        const std::uint8_t colorIndex = patternX % 2U == 0U
            ? static_cast<std::uint8_t>(packed >> 4U)
            : static_cast<std::uint8_t>(packed & 0x0fU);
        const std::size_t cramOffset = (bank * 16U + colorIndex) * 2U;
        const std::uint16_t cramWord = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(cram[cramOffset]) << 8U)
            | cram[cramOffset + 1U]);
        const auto previewPixel = result.preview->row(0);
        test.expect(previewPixel[0]
                            == static_cast<std::uint8_t>(((cramWord >> 1U) & 7U) * 255U / 7U)
                        && previewPixel[1]
                            == static_cast<std::uint8_t>(((cramWord >> 5U) & 7U) * 255U / 7U)
                        && previewPixel[2]
                            == static_cast<std::uint8_t>(((cramWord >> 9U) & 7U) * 255U / 7U),
                    "the Genesis preview should decode the emitted tile, map, and CRAM data");
    }

    auto wrongSize = RgbImage::createTightlyPacked(255, 224, PixelFormat::Rgb888);
    ConversionSettings settings;
    settings.targetProfile = TargetProfileId::SegaGenesis;
    settings.mode = ConversionMode::Mode5GenesisH32;
    settings.targetWidth = 256;
    settings.targetHeight = 224;
    const ConversionResult invalid = convertSegaGenesisMode5(*wrongSize, settings);
    test.expect(!invalid.succeeded() && !invalid.diagnostics.empty()
                    && invalid.diagnostics.front().code
                        == "genesis-mode5-invalid-dimensions",
                "the Genesis converter should reject non-native source geometry");
}

void testHuC6270Conversion(TestContext& test)
{
    struct ModeCase {
        ConversionMode mode;
        std::uint32_t width;
        std::size_t patternBytes;
        std::size_t mapBytes;
    };
    constexpr std::array cases{
        ModeCase{ConversionMode::HuC6270Background256, 256, 0xf800, 0x0800},
        ModeCase{ConversionMode::HuC6270Background320, 320, 0xf000, 0x1000},
    };
    for (const auto& modeCase : cases) {
        auto source = RgbImage::createTightlyPacked(
            modeCase.width, 224, PixelFormat::Rgb888);
        test.expect(source.has_value(), "the HuC6270 test source should be allocated");
        if (!source) continue;
        for (std::uint32_t y = 0; y < source->height(); ++y) {
            auto row = source->row(y);
            for (std::uint32_t x = 0; x < source->width(); ++x) {
                const bool light = ((x / 8U) + (y / 8U)) % 2U != 0U;
                const std::size_t offset = static_cast<std::size_t>(x) * 3U;
                row[offset] = light ? 255U : 0U;
                row[offset + 1U] = light ? 128U : 0U;
                row[offset + 2U] = light ? 64U : 0U;
            }
        }
        ConversionSettings settings;
        settings.targetProfile = TargetProfileId::HuC6270;
        settings.mode = modeCase.mode;
        settings.targetWidth = modeCase.width;
        settings.targetHeight = 224;
        settings.dither = DitherMode::None;
        const ConversionResult result = convertHuC6270Background(*source, settings);
        test.expect(result.succeeded() && result.preview && result.target
                        && validateTargetTables(*result.target),
                    "HuC6270 background conversion should compile valid native tables");
        if (!result.target) continue;
        const auto& tables = result.target->tables;
        test.expect(result.target->profile == TargetProfileId::HuC6270
                        && tables.size() == 4
                        && tables[0].role == TargetTableRole::Pattern
                        && tables[0].bytes.size() == modeCase.patternBytes
                        && tables[1].role == TargetTableRole::TileMap
                        && tables[1].bytes.size() == modeCase.mapBytes
                        && tables[2].role == TargetTableRole::Palette
                        && tables[2].bytes.size() == 1024
                        && tables[3].role == TargetTableRole::DisplayRegisters
                        && tables[3].bytes.size() == 42,
                    "HuC6270 output should contain planar tiles, BAT, VCE colors, and VDC/VCE state");
        const std::uint16_t batWord = static_cast<std::uint16_t>(tables[1].bytes[0])
            | (static_cast<std::uint16_t>(tables[1].bytes[1]) << 8U);
        test.expect((batWord & 0x0fffU) == modeCase.mapBytes / 32U,
                    "HuC6270 BAT entries should address tiles after the in-VRAM BAT");
    }

    auto wrongSize = RgbImage::createTightlyPacked(255, 224, PixelFormat::Rgb888);
    ConversionSettings settings;
    settings.targetProfile = TargetProfileId::HuC6270;
    settings.mode = ConversionMode::HuC6270Background256;
    settings.targetWidth = 256;
    settings.targetHeight = 224;
    const ConversionResult invalid = convertHuC6270Background(*wrongSize, settings);
    test.expect(!invalid.succeeded() && !invalid.diagnostics.empty()
                    && invalid.diagnostics.front().code == "huc6270-invalid-dimensions",
                "the HuC6270 converter should reject non-native source geometry");
}

void testCommodoreVdpConversion(TestContext& test)
{
    struct ModeCase {
        ConversionMode mode;
        TargetProfileId profile;
        std::uint32_t width;
        std::uint32_t height;
        std::size_t tableCount;
    };
    constexpr std::array cases{
        ModeCase{ConversionMode::VicIIHiresCharacter, TargetProfileId::VicII,
                 320, 200, 4},
        ModeCase{ConversionMode::VicIIMulticolorCharacter, TargetProfileId::VicII,
                 160, 200, 4},
        ModeCase{ConversionMode::VicIIHiresBitmap, TargetProfileId::VicII,
                 320, 200, 3},
        ModeCase{ConversionMode::VicIIMulticolorBitmap, TargetProfileId::VicII,
                 160, 200, 4},
        ModeCase{ConversionMode::VicHiresCharacter, TargetProfileId::Vic,
                 176, 184, 4},
        ModeCase{ConversionMode::VicMulticolorCharacter, TargetProfileId::Vic,
                 88, 184, 4},
    };
    for (const auto& modeCase : cases) {
        auto source = RgbImage::createTightlyPacked(
            modeCase.width, modeCase.height, PixelFormat::Rgb888);
        test.expect(source.has_value(), "the Commodore test source should be allocated");
        if (!source) continue;
        for (std::uint32_t y = 0; y < source->height(); ++y) {
            auto row = source->row(y);
            for (std::uint32_t x = 0; x < source->width(); ++x) {
                const bool light = ((x / 4U) + (y / 4U)) % 2U != 0U;
                const std::size_t offset = static_cast<std::size_t>(x) * 3U;
                row[offset] = light ? 255U : 0U;
                row[offset + 1U] = light ? 255U : 0U;
                row[offset + 2U] = light ? 255U : 0U;
            }
        }
        ConversionSettings settings;
        settings.targetProfile = modeCase.profile;
        settings.mode = modeCase.mode;
        settings.targetWidth = modeCase.width;
        settings.targetHeight = modeCase.height;
        settings.dither = DitherMode::None;
        const ConversionResult result = convertCommodoreDisplay(*source, settings);
        test.expect(result.succeeded() && result.preview && result.target
                        && validateTargetTables(*result.target),
                    "VIC and VIC-II conversions should compile valid native tables");
        if (!result.target) continue;
        const auto& tables = result.target->tables;
        test.expect(result.target->profile == modeCase.profile
                        && result.target->palette
                        && result.target->palette->size() == 16
                        && tables.size() == modeCase.tableCount
                        && tables.back().role == TargetTableRole::DisplayRegisters
                        && tables.back().bytes.size()
                            == (modeCase.profile == TargetProfileId::VicII ? 47U : 16U),
                    "Commodore output should include the native register block");
        if (modeCase.mode == ConversionMode::VicIIHiresBitmap
            || modeCase.mode == ConversionMode::VicIIMulticolorBitmap) {
            test.expect(tables[0].role == TargetTableRole::Framebuffer
                            && tables[0].bytes.size() == 8000
                            && tables[1].role == TargetTableRole::TileMap
                            && tables[1].bytes.size() == 1000,
                        "VIC-II bitmap modes should emit 8 KiB bitmap and 1 KiB screen memory");
        } else {
            const std::size_t cells = modeCase.profile == TargetProfileId::VicII
                ? 1000U : 506U;
            test.expect(tables[0].role == TargetTableRole::Pattern
                            && tables[0].bytes.size() == 2048
                            && tables[1].role == TargetTableRole::TileMap
                            && tables[1].bytes.size() == cells
                            && tables[2].role == TargetTableRole::Color
                            && tables[2].bytes.size() == cells,
                        "Commodore character modes should emit character, screen, and color memory");
        }
    }
}

void testTargetData(TestContext &test)
{
    PaletteError paletteError = PaletteError::TooManyColors;
    auto palette = Palette::create({RgbColor{0, 0, 0}, RgbColor{255, 255, 255}},
                                   &paletteError);
    test.expect(palette.has_value() && paletteError == PaletteError::None,
                "a palette containing one to sixteen colors should be created");
    test.expect(palette->size() == 2 && palette->at(1) == RgbColor{255, 255, 255},
                "palette entries should preserve their RGB channel values");

    palette = Palette::create({}, &paletteError);
    test.expect(!palette.has_value() && paletteError == PaletteError::Empty,
                "an empty palette should be rejected");
    palette = Palette::create(std::vector<RgbColor>(Palette::maximumColorCount + 1),
                              &paletteError);
    test.expect(!palette.has_value() && paletteError == PaletteError::TooManyColors,
                "a palette with more than sixteen colors should be rejected");

    const std::array modes{
        ConversionMode::Bitmap9918,
        ConversionMode::GreyscaleBitmap9918,
        ConversionMode::BlackAndWhiteBitmap9918,
        ConversionMode::Multicolor9918,
        ConversionMode::DualMulticolor9918,
        ConversionMode::HalfMulticolor9918,
        ConversionMode::BitmapColorOnly9918,
        ConversionMode::PalettedBitmapF18A,
        ConversionMode::ScanlinePaletteBitmapF18A,
        ConversionMode::Screen5V9938,
        ConversionMode::Screen6V9938,
        ConversionMode::Screen7V9938,
        ConversionMode::Screen8V9938,
        ConversionMode::Screen10V9958,
        ConversionMode::Screen11V9958,
        ConversionMode::Screen12V9958,
        ConversionMode::Mode4Sms192,
        ConversionMode::Mode4Sms224,
        ConversionMode::Mode4Sms240,
        ConversionMode::Mode5GenesisH32,
        ConversionMode::Mode5GenesisH40,
        ConversionMode::Mode5GenesisH32Pal,
        ConversionMode::Mode5GenesisH40Pal,
        ConversionMode::HuC6270Background256,
        ConversionMode::HuC6270Background320,
        ConversionMode::VicIIHiresCharacter,
        ConversionMode::VicIIMulticolorCharacter,
        ConversionMode::VicIIHiresBitmap,
        ConversionMode::VicIIMulticolorBitmap,
        ConversionMode::VicHiresCharacter,
        ConversionMode::VicMulticolorCharacter,
    };

    constexpr std::array roles{
        TargetTableRole::Pattern, TargetTableRole::Color, TargetTableRole::Multicolor,
        TargetTableRole::MulticolorFrame1, TargetTableRole::MulticolorFrame2,
        TargetTableRole::FixedPattern, TargetTableRole::Palette,
        TargetTableRole::ScanlinePalettes, TargetTableRole::Framebuffer,
        TargetTableRole::TileMap, TargetTableRole::DisplayRegisters,
    };
    for (const auto role : roles) {
        const auto id = retrovdp::core::targetTableRoleId(role);
        test.expect(retrovdp::core::targetTableRole(id) == role,
                    "target table role IDs should round-trip through the registry boundary");
    }
    test.expect(!retrovdp::core::targetTableRole("unknown-role").has_value(),
                "unknown target table role IDs should be rejected");

    for (const auto mode : modes) {
        const auto expected = expectedTargetTables(mode);
        test.expect(!expected.empty(), "every conversion mode should define target tables");

        std::vector<TargetMemoryTable> tables;
        for (const auto layout : expected) {
            tables.push_back({layout.role, std::vector<std::uint8_t>(layout.byteSize)});
        }
        test.expect(static_cast<bool>(validateTargetTables(mode, tables)),
                    "tables matching their mode layout should be valid");
    }

    const auto bitmapLayout = expectedTargetTables(ConversionMode::Bitmap9918);
    test.expect(bitmapLayout.size() == 2
                    && bitmapLayout[0].role == TargetTableRole::Pattern
                    && bitmapLayout[0].byteSize == 6144
                    && bitmapLayout[1].role == TargetTableRole::Color
                    && bitmapLayout[1].byteSize == 6144,
                "Bitmap 9918A should define 6144-byte pattern and color tables");
    const auto blackAndWhiteLayout =
        expectedTargetTables(ConversionMode::BlackAndWhiteBitmap9918);
    test.expect(blackAndWhiteLayout.size() == 1
                    && blackAndWhiteLayout[0].role == TargetTableRole::Pattern
                    && blackAndWhiteLayout[0].byteSize == 6144,
                "black-and-white mode should define only a 6144-byte pattern table");
    const auto multicolorLayout = expectedTargetTables(ConversionMode::Multicolor9918);
    test.expect(multicolorLayout.size() == 1
                    && multicolorLayout[0].role == TargetTableRole::Multicolor
                    && multicolorLayout[0].byteSize == 1536,
                "Multicolor 9918 should define one 1536-byte table");
    const auto dualLayout = expectedTargetTables(ConversionMode::DualMulticolor9918);
    test.expect(dualLayout.size() == 2
                    && dualLayout[0].role == TargetTableRole::MulticolorFrame1
                    && dualLayout[1].role == TargetTableRole::MulticolorFrame2
                    && dualLayout[0].byteSize == 1536 && dualLayout[1].byteSize == 1536,
                "dual multicolor should define two 1536-byte frame tables");
    const auto halfLayout = expectedTargetTables(ConversionMode::HalfMulticolor9918);
    test.expect(halfLayout.size() == 3 && halfLayout[2].role == TargetTableRole::Multicolor
                    && halfLayout[2].byteSize == 2048,
                "half multicolor should include its 2048-byte multicolor table");
    const auto f18aLayout = expectedTargetTables(ConversionMode::PalettedBitmapF18A);
    test.expect(f18aLayout.size() == 3 && f18aLayout[2].role == TargetTableRole::Palette
                    && f18aLayout[2].byteSize == 32,
                "paletted F18A mode should include a 32-byte palette table");
    const auto scanlineLayout = expectedTargetTables(ConversionMode::ScanlinePaletteBitmapF18A);
    test.expect(scanlineLayout.size() == 3
                    && scanlineLayout[2].role == TargetTableRole::ScanlinePalettes
                    && scanlineLayout[2].byteSize == 6144,
                "scanline F18A mode should include a 6144-byte palette table");

    test.expect(validateTargetTables(static_cast<ConversionMode>(255), {}).error
                    == TargetTableError::UnsupportedConversionMode,
                "unknown conversion modes should not validate as empty target layouts");

    std::vector<TargetMemoryTable> bitmapTables{
        {bitmapLayout[0].role, std::vector<std::uint8_t>(bitmapLayout[0].byteSize)},
        {bitmapLayout[1].role, std::vector<std::uint8_t>(bitmapLayout[1].byteSize)},
    };
    bitmapTables.pop_back();
    auto invalidTables = validateTargetTables(ConversionMode::Bitmap9918, bitmapTables);
    test.expect(invalidTables.error == TargetTableError::TableCountMismatch,
                "a mode should reject a missing target table");

    bitmapTables.push_back(
        {TargetTableRole::Palette, std::vector<std::uint8_t>(bitmapLayout[1].byteSize)});
    invalidTables = validateTargetTables(ConversionMode::Bitmap9918, bitmapTables);
    test.expect(invalidTables.error == TargetTableError::TableRoleMismatch,
                "a mode should reject a target table in the wrong role");

    bitmapTables[1].role = bitmapLayout[1].role;
    bitmapTables[1].bytes.pop_back();
    const auto invalidSize = validateTargetTables(ConversionMode::Bitmap9918, bitmapTables);
    test.expect(invalidSize.error == TargetTableError::TableSizeMismatch
                    && invalidSize.tableIndex == 1
                    && invalidSize.expected.byteSize == bitmapLayout[1].byteSize,
                "target-table size errors should identify the table and expected layout");

    const TargetMemoryImage target{
        .mode = ConversionMode::Bitmap9918,
        .palette = std::nullopt,
        .tables = {
            {bitmapLayout[0].role, std::vector<std::uint8_t>(bitmapLayout[0].byteSize)},
            {bitmapLayout[1].role, std::vector<std::uint8_t>(bitmapLayout[1].byteSize)},
        },
    };
    test.expect(static_cast<bool>(validateTargetTables(target)),
                "a target-memory image should validate against its recorded mode");
}

void testImageTransform(TestContext &test)
{
    auto source = RgbImage::create(
        {.width = 4, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 12},
        {
            10, 0, 0,
            20, 0, 0,
            30, 0, 0,
            40, 0, 0,
        });
    test.expect(source.has_value(), "image-transform fixture should be created");

    ImageTransformOptions options{
        .targetWidth = 8,
        .targetHeight = 6,
        .filter = ScalingFilter::Bilinear,
        .fillMode = ImageFillMode::Fit,
    };
    auto plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->scaledWidth == 8 && plan->scaledHeight == 2
                    && plan->destinationX == 0 && plan->destinationY == 2,
                "fit mode should preserve aspect ratio and center letterboxing");

    options.targetWidth = 2;
    options.targetHeight = 2;
    options.fillMode = ImageFillMode::CropCenter;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->scaledWidth == 8 && plan->scaledHeight == 2
                    && plan->cropX == 3 && plan->cropY == 0,
                "center fill should scale to cover and crop the middle");

    options.filter = ScalingFilter::None;
    options.targetWidth = 2;
    options.targetHeight = 1;
    options.fillMode = ImageFillMode::CropStart;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->scaledWidth == 4 && plan->cropX == 0,
                "no-scale start crop should retain the first source pixels");
    options.fillMode = ImageFillMode::CropCenter;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->cropX == 1,
                "no-scale center crop should retain the middle source pixels");
    options.fillMode = ImageFillMode::CropEnd;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->cropX == 2,
                "no-scale end crop should retain the final source pixels");

    options.fillMode = ImageFillMode::CropCenter;
    options.horizontalOffset = 10;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->cropX == 0,
                "positive position offsets should move the source toward the target end");
    options.horizontalOffset = -10;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->cropX == 2,
                "negative position offsets should move the source toward the target start");

    options.horizontalOffset = 0;
    auto transformed = transformImage(*source, options);
    test.expect(static_cast<bool>(transformed),
                "a valid no-scale crop should produce an image");
    test.expect(transformed.image->bytes()
                    == std::vector<std::uint8_t>({20, 0, 0, 30, 0, 0}),
                "center cropping should copy the expected source pixels");

    options.targetWidth = 6;
    options.targetHeight = 3;
    options.fillMode = ImageFillMode::Fit;
    options.backgroundRed = 1;
    options.backgroundGreen = 2;
    options.backgroundBlue = 3;
    transformed = transformImage(*source, options);
    test.expect(static_cast<bool>(transformed) && transformed.plan.destinationX == 1
                    && transformed.plan.destinationY == 1,
                "an unscaled source should be centered in a larger target");
    test.expect(transformed.image->bytes()[0] == 1 && transformed.image->bytes()[1] == 2
                    && transformed.image->bytes()[2] == 3,
                "letterbox pixels should use the configured background color");

    options.horizontalOffset = 10;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->destinationX == 2,
                "positive fit offsets should move a smaller source right within the target");
    options.horizontalOffset = -10;
    plan = planImageTransform(*source, options);
    test.expect(plan.has_value() && plan->destinationX == 0,
                "negative fit offsets should move a smaller source left within the target");

    auto verticalSource = RgbImage::create(
        {.width = 1, .height = 4, .pixelFormat = PixelFormat::Rgb888, .rowStride = 3},
        {10, 0, 0, 20, 0, 0, 30, 0, 0, 40, 0, 0});
    options = {
        .targetWidth = 1,
        .targetHeight = 2,
        .filter = ScalingFilter::None,
        .fillMode = ImageFillMode::CropEnd,
    };
    transformed = transformImage(*verticalSource, options);
    test.expect(static_cast<bool>(transformed) && transformed.plan.cropY == 2
                    && transformed.image->bytes()[0] == 30
                    && transformed.image->bytes()[3] == 40,
                "end cropping should retain the final source rows");

    auto rgbaSource = RgbImage::create(
        {.width = 1, .height = 1, .pixelFormat = PixelFormat::Rgba8888, .rowStride = 4},
        {9, 8, 7, 6});
    options = {
        .targetWidth = 3,
        .targetHeight = 3,
        .filter = ScalingFilter::None,
        .fillMode = ImageFillMode::Fit,
        .backgroundRed = 1,
        .backgroundGreen = 2,
        .backgroundBlue = 3,
        .backgroundAlpha = 4,
    };
    transformed = transformImage(*rgbaSource, options);
    test.expect(static_cast<bool>(transformed) && transformed.image->pixelFormat()
                    == PixelFormat::Rgba8888,
                "image transforms should preserve the source pixel format");
    test.expect(transformed.image->bytes()[0] == 1 && transformed.image->bytes()[3] == 4
                    && transformed.image->bytes()[16] == 9
                    && transformed.image->bytes()[19] == 6,
                "RGBA composition should preserve source and background alpha");

    auto gradient = RgbImage::create(
        {.width = 2, .height = 1, .pixelFormat = PixelFormat::Rgb888, .rowStride = 6},
        {255, 0, 0, 0, 0, 255});
    options = {
        .targetWidth = 3,
        .targetHeight = 1,
        .filter = ScalingFilter::Bilinear,
        .fillMode = ImageFillMode::CropCenter,
    };
    transformed = transformImage(*gradient, options);
    test.expect(static_cast<bool>(transformed) && transformed.image->bytes()[3] == 85
                    && transformed.image->bytes()[5] == 170,
                "bilinear scaling should deterministically blend neighboring pixels");

    for (const ScalingFilter filter : {ScalingFilter::Box,
                                       ScalingFilter::Gaussian,
                                       ScalingFilter::Hamming,
                                       ScalingFilter::Blackman,
                                       ScalingFilter::Bilinear}) {
        options.filter = filter;
        test.expect(static_cast<bool>(transformImage(*gradient, options)),
                    "every supported resampling filter should produce an image");
    }

    options.targetWidth = 0;
    test.expect(transformImage(*gradient, options).error
                    == ImageTransformError::ZeroTargetDimension,
                "zero target dimensions should be rejected");
    options.targetWidth = 3;
    options.filter = static_cast<ScalingFilter>(255);
    test.expect(transformImage(*gradient, options).error
                    == ImageTransformError::UnsupportedFilter,
                "unknown scaling filters should be rejected");
    options.filter = ScalingFilter::Bilinear;
    options.fillMode = static_cast<ImageFillMode>(255);
    test.expect(transformImage(*gradient, options).error
                    == ImageTransformError::UnsupportedFillMode,
                "unknown fill modes should be rejected");

    options = {
        .targetWidth = 1,
        .targetHeight = 4,
        .filter = ScalingFilter::Bilinear,
        .fillMode = ImageFillMode::CropCenter,
    };
    const ImageSizeLimits transformLimits{
        .maximumWidth = 8,
        .maximumHeight = 8,
        .maximumPixels = 64,
        .maximumBytes = 256,
    };
    test.expect(transformImage(*source, options, transformLimits).error
                    == ImageTransformError::ScaledImageLimitExceeded,
                "an oversized resampling intermediate should be rejected before allocation");
}

void testCorpusManifest(TestContext &test, const QJsonObject &manifest)
{
    test.expect(manifest.value(QStringLiteral("schema_version")).toInt() == 1,
                "corpus schema version should be 1");

    const QJsonArray sourceImages = manifest.value(QStringLiteral("source_images")).toArray();
    const QJsonArray malformedInputs = manifest.value(QStringLiteral("malformed_inputs")).toArray();
    test.expect(sourceImages.size() == 8, "corpus should contain eight source images");
    test.expect(malformedInputs.size() == 3, "corpus should contain three malformed inputs");

    for (const QJsonArray &entries : {sourceImages, malformedInputs}) {
        for (const QJsonValue &value : entries) {
            const QJsonObject entry = value.toObject();
            const QString path = corpusPath(entry);
            test.expect(!path.isEmpty(), "corpus paths must remain under tests/golden");
            test.expect(sha256(path) == entry.value(QStringLiteral("sha256")).toString().toLatin1(),
                        "corpus SHA-256 digest should match the manifest");
        }
    }
}

void testCorpusImages(TestContext &test, const QJsonObject &manifest)
{
    const QJsonArray sourceImages = manifest.value(QStringLiteral("source_images")).toArray();
    for (const QJsonValue &value : sourceImages) {
        const QJsonObject entry = value.toObject();
        QImageReader reader(corpusPath(entry));
        const QImage image = reader.read();
        test.expect(!image.isNull(), "each corpus source should decode as an image");
        test.expect(image.size()
                        == QSize(entry.value(QStringLiteral("width")).toInt(),
                                 entry.value(QStringLiteral("height")).toInt()),
                    "decoded dimensions should match the manifest");
    }

    for (const QString &filename : {QStringLiteral("tiny-rgba.png"),
                                    QStringLiteral("transparency-rgba.png")}) {
        const QImage image(corpusDirectory + QStringLiteral("/source/") + filename);
        test.expect(!image.isNull() && image.hasAlphaChannel(),
                    "alpha fixtures should decode with an alpha channel");
    }

    const QImage gradient(corpusDirectory + QStringLiteral("/source/transparency-rgba.png"));
    test.expect(gradient.pixelColor(0, 0).alpha() == 0,
                "the transparency gradient should include fully transparent pixels");
    test.expect(gradient.pixelColor(gradient.width() / 2, gradient.height() / 2).alpha() >= 250,
                "the transparency gradient should include nearly opaque pixels");
}

void testMalformedInputs(TestContext &test, const QJsonObject &manifest)
{
    const QJsonArray malformedInputs = manifest.value(QStringLiteral("malformed_inputs")).toArray();
    for (const QJsonValue &value : malformedInputs) {
        QImageReader reader(corpusPath(value.toObject()));
        test.expect(reader.read().isNull(), "malformed corpus input should be rejected");
    }
}

void testOriginalCapture(TestContext &test)
{
    const QJsonObject capture = loadCaptureManifest();
    test.expect(capture.value(QStringLiteral("schema_version")).toInt() == 1,
                "original capture schema version should be 1");
    const QJsonObject original = capture.value(QStringLiteral("original")).toObject();
    test.expect(original.value(QStringLiteral("version")).toString() == QStringLiteral("1.9.1.0"),
                "original capture should identify Convert9918 1.9.1.0");

    const QJsonArray captures = capture.value(QStringLiteral("captures")).toArray();
    test.expect(captures.size() == 8, "original capture should contain all eight valid sources");
    for (const QJsonValue &captureValue : captures) {
        const QJsonObject captureEntry = captureValue.toObject();
        const QJsonArray outputs = captureEntry.value(QStringLiteral("outputs")).toArray();
        test.expect(outputs.size() == 3, "each original capture should contain three outputs");
        for (const QJsonValue &outputValue : outputs) {
            const QJsonObject output = outputValue.toObject();
            const QString path = corpusPath(output);
            const QFileInfo file(path);
            test.expect(file.exists(), "each recorded original output should exist");
            test.expect(file.size() == output.value(QStringLiteral("size")).toInteger(),
                        "original output size should match its capture manifest");
            test.expect(sha256(path) == output.value(QStringLiteral("sha256")).toString().toLatin1(),
                        "original output SHA-256 should match its capture manifest");

            const QString suffix = file.suffix().toUpper();
            if (suffix == QStringLiteral("BMP")) {
                const QImage preview(path);
                test.expect(preview.size() == QSize(256, 192),
                            "original BMP preview should be 256 by 192 pixels");
            } else {
                test.expect(suffix == QStringLiteral("TIAP") || suffix == QStringLiteral("TIAC"),
                            "original binary output should be a TIAP or TIAC table");
                test.expect(file.size() == 6272,
                            "TIFILES table should contain a 128-byte header and 6144-byte payload");
                QFile input(path);
                test.expect(input.open(QIODevice::ReadOnly), "TIFILES output should be readable");
                test.expect(input.read(8) == QByteArray("\x07TIFILES", 8),
                            "TIFILES output should contain the expected signature");
            }
        }
    }
}

void testOriginalModeCaptures(TestContext &test)
{
    const QJsonObject manifest = loadModeCaptureManifest();
    test.expect(manifest.value(QStringLiteral("schema_version")).toInt() == 1,
                "original mode-capture schema version should be 1");

    const QJsonArray modes = manifest.value(QStringLiteral("modes")).toArray();
    test.expect(modes.size() == 8, "mode captures should contain the eight remaining modes");
    QSet<int> capturedModeIndexes;

    for (const QJsonValue &modeValue : modes) {
        const QJsonObject mode = modeValue.toObject();
        const int modeIndex = mode.value(QStringLiteral("conversion_mode_index")).toInt(-1);
        test.expect(modeIndex >= 1 && modeIndex <= 8,
                    "captured conversion-mode index should be between 1 and 8");
        test.expect(!capturedModeIndexes.contains(modeIndex),
                    "captured conversion-mode indexes should be unique");
        capturedModeIndexes.insert(modeIndex);

        QHash<QString, qint64> expectedPayloadSizes;
        const QJsonArray tablePayloads = mode.value(QStringLiteral("table_payloads")).toArray();
        for (const QJsonValue &tableValue : tablePayloads) {
            const QJsonObject table = tableValue.toObject();
            expectedPayloadSizes.insert(table.value(QStringLiteral("extension")).toString(),
                                        table.value(QStringLiteral("payload_size")).toInteger());
        }

        const bool previewAvailable = mode.value(QStringLiteral("preview_available")).toBool();
        const int expectedOutputCount = tablePayloads.size() + (previewAvailable ? 1 : 0);
        const QJsonArray captures = mode.value(QStringLiteral("captures")).toArray();
        test.expect(captures.size() == 8,
                    "each remaining conversion mode should contain all eight valid sources");

        for (const QJsonValue &captureValue : captures) {
            const QJsonArray outputs =
                captureValue.toObject().value(QStringLiteral("outputs")).toArray();
            test.expect(outputs.size() == expectedOutputCount,
                        "each mode capture should contain its applicable outputs");

            for (const QJsonValue &outputValue : outputs) {
                const QJsonObject output = outputValue.toObject();
                const QString path = corpusPath(output);
                const QFileInfo file(path);
                test.expect(file.exists(), "each recorded mode output should exist");
                test.expect(file.size() == output.value(QStringLiteral("size")).toInteger(),
                            "mode-output size should match its capture manifest");
                test.expect(sha256(path)
                                == output.value(QStringLiteral("sha256")).toString().toLatin1(),
                            "mode-output SHA-256 should match its capture manifest");

                const QString extension = output.value(QStringLiteral("extension")).toString();
                if (output.value(QStringLiteral("kind")).toString()
                    == QStringLiteral("preview")) {
                    const QImage preview(path);
                    test.expect(extension == QStringLiteral("BMP")
                                    && preview.size() == QSize(256, 192),
                                "captured preview should be a 256 by 192 BMP");
                } else {
                    test.expect(expectedPayloadSizes.contains(extension),
                                "captured table extension should be expected for its mode");
                    test.expect(file.size() == expectedPayloadSizes.value(extension) + 128,
                                "captured table should contain its payload and TIFILES header");
                    QFile input(path);
                    test.expect(input.open(QIODevice::ReadOnly),
                                "captured TIFILES table should be readable");
                    test.expect(input.read(8) == QByteArray("\x07TIFILES", 8),
                                "captured table should contain the TIFILES signature");
                }
            }
        }
    }

    for (int modeIndex = 1; modeIndex <= 8; ++modeIndex) {
        test.expect(capturedModeIndexes.contains(modeIndex),
                    "every remaining original conversion-mode index should be captured");
    }
}

void testOriginalExportCaptures(TestContext &test)
{
    const QJsonObject manifest = loadExportCaptureManifest();
    test.expect(manifest.value(QStringLiteral("schema_version")).toInt() == 1,
                "original export-capture schema version should be 1");
    test.expect(manifest.value(QStringLiteral("original"))
                        .toObject()
                        .value(QStringLiteral("version"))
                        .toString()
                    == QStringLiteral("1.9.1.0"),
                "original export capture should identify Convert9918 1.9.1.0");

    const QJsonObject workflow = manifest.value(QStringLiteral("workflow")).toObject();
    test.expect(workflow.value(QStringLiteral("method")).toString()
                    == QStringLiteral("controlled-ui"),
                "export baseline should record the controlled UI workflow");
    test.expect(workflow.value(QStringLiteral("conversion_mode_index")).toInt(-1) == 0,
                "export baseline should use default Bitmap 9918A mode");
    const QJsonObject source = workflow.value(QStringLiteral("source")).toObject();
    test.expect(sha256(corpusPath(source))
                    == source.value(QStringLiteral("sha256")).toString().toLatin1(),
                "export baseline source digest should match its manifest");

    const QSet<QString> expectedFormatIds = {
        QStringLiteral("v9t9"),       QStringLiteral("raw"),
        QStringLiteral("rle"),        QStringLiteral("ti-xb"),
        QStringLiteral("ti-xb-rle"),  QStringLiteral("msx-sc2"),
        QStringLiteral("cvpaint"),    QStringLiteral("powerpaint"),
        QStringLiteral("hgr"),        QStringLiteral("coleco-rom"),
        QStringLiteral("png"),
    };
    QSet<QString> capturedFormatIds;
    int outputCount = 0;

    const QJsonArray formats = manifest.value(QStringLiteral("formats")).toArray();
    test.expect(formats.size() == expectedFormatIds.size(),
                "export baseline should contain all eleven applicable choices");
    for (const QJsonValue &formatValue : formats) {
        const QJsonObject format = formatValue.toObject();
        const QString id = format.value(QStringLiteral("id")).toString();
        test.expect(expectedFormatIds.contains(id),
                    "export capture should use a recognized format id");
        test.expect(!capturedFormatIds.contains(id),
                    "export capture format ids should be unique");
        capturedFormatIds.insert(id);

        const QJsonArray outputs = format.value(QStringLiteral("outputs")).toArray();
        test.expect(!outputs.isEmpty(), "each captured export format should emit a file");
        outputCount += outputs.size();
        for (const QJsonValue &outputValue : outputs) {
            const QJsonObject output = outputValue.toObject();
            const QString path = corpusPath(output);
            const QFileInfo file(path);
            test.expect(file.exists(), "each recorded export output should exist");
            test.expect(file.size() == output.value(QStringLiteral("size")).toInteger(),
                        "export size should match its capture manifest");
            test.expect(sha256(path)
                            == output.value(QStringLiteral("sha256")).toString().toLatin1(),
                        "export SHA-256 should match its capture manifest");

            const QByteArray prefix = readPrefix(path, 16);
            if (id == QStringLiteral("v9t9")) {
                test.expect(file.size() == 6272 && prefix.startsWith("tinyv9_"),
                            "V9T9 tables should have a 128-byte filename header");
                test.expect(!prefix.startsWith(QByteArray("\x07TIFILES", 8)),
                            "V9T9 tables should not contain a TIFILES signature");
            } else if (id == QStringLiteral("raw")) {
                test.expect(file.size() == 6144,
                            "raw pattern and color tables should be exactly 6144 bytes");
            } else if (id == QStringLiteral("rle")) {
                test.expect(file.size() > 0 && file.size() < 6144,
                            "RLE tables should be nonempty and smaller than raw tables");
            } else if (id == QStringLiteral("ti-xb")
                       || id == QStringLiteral("ti-xb-rle")) {
                test.expect(prefix.startsWith(QByteArray("\x07TIFILES", 8)),
                            "TI XB programs should use the original TIFILES wrapper");
            } else if (id == QStringLiteral("msx-sc2")) {
                test.expect(prefix.startsWith(QByteArray("\xFE\x00\x00\x00\x38\x00\x00", 7)),
                            "MSX SC2 should contain the captured binary header");
            } else if (id == QStringLiteral("cvpaint")) {
                test.expect(file.size() == 12288,
                            "CVPaint output should contain two 6144-byte tables");
            } else if (id == QStringLiteral("powerpaint")) {
                test.expect(file.size() == 10240,
                            "PowerPaint output should retain its 10 KiB layout");
            } else if (id == QStringLiteral("hgr")) {
                test.expect(file.size() == 10261
                                && prefix.startsWith(QByteArray("\x01\x00\x02", 3)),
                            "Adam HGR should retain its captured header and size");
            } else if (id == QStringLiteral("coleco-rom")) {
                test.expect(prefix.startsWith(QByteArray("\x55\xAA", 2)),
                            "ColecoVision cartridge should start with its ROM signature");
            } else if (id == QStringLiteral("png")) {
                const QImage image(path);
                test.expect(image.size() == QSize(256, 192),
                            "captured PNG export should be 256 by 192 pixels");
            }
        }
    }

    test.expect(capturedFormatIds == expectedFormatIds,
                "every applicable non-TIFILES export choice should be captured");
    test.expect(outputCount == 14, "the eleven export choices should emit fourteen files");

    const QJsonArray excluded = manifest.value(QStringLiteral("excluded_formats")).toArray();
    test.expect(excluded.size() == 1
                    && excluded.first()
                           .toObject()
                           .value(QStringLiteral("id"))
                           .toString()
                        == QStringLiteral("coleco-rle-rom")
                    && excluded.first()
                           .toObject()
                           .value(QStringLiteral("menu_label"))
                           .toString()
                           .contains(QStringLiteral("Broken")),
                "the original's broken RLE cartridge writer should be explicitly excluded");
}

} // namespace

int main(int argc, char *argv[])
{
    const QCoreApplication application(argc, argv);
    TestContext test;
    testSettingsValidation(test);
    testColorMath(test);
    testDithering(test);
    testBitmap9918Conversion(test);
    testGreyscaleBitmap9918Conversion(test);
    testBlackAndWhiteBitmap9918Conversion(test);
    testBitmapColorOnly9918Conversion(test);
    testMulticolor9918Conversion(test);
    testDualMulticolor9918Conversion(test);
    testHalfMulticolor9918Conversion(test);
    testPalettedBitmapF18AConversion(test);
    testScanlinePaletteBitmapF18AConversion(test);
    testYamahaBitmapConversion(test);
    testSegaSmsMode4Conversion(test);
    testSegaGenesisMode5Conversion(test);
    testHuC6270Conversion(test);
    testCommodoreVdpConversion(test);
    testImageAdjustments(test);
    testPaletteSelection(test);
    testRgbImage(test);
    testConversionTypes(test);
    testConversionJobController(test);
    testConverterCancellation(test);
    testConversionMemoryEstimate(test);
    testTargetProfiles(test);
    testTargetData(test);
    testImageTransform(test);
    const QJsonObject manifest = loadCorpusManifest();
    testCorpusManifest(test, manifest);
    testCorpusImages(test, manifest);
    testMalformedInputs(test, manifest);
    testOriginalCapture(test);
    testOriginalModeCaptures(test);
    testOriginalExportCaptures(test);
    return test.result();
}
