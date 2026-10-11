#include "retrovdp/core/ObjectProcessorConverter.hpp"

#include "retrovdp/core/ColorMath.hpp"
#include "retrovdp/core/Dithering.hpp"
#include "retrovdp/core/PaletteSelection.hpp"
#include "retrovdp/core/TargetProfile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace retrovdp::core {
namespace {

constexpr std::uint32_t lynxWidth = 160;
constexpr std::uint32_t lynxHeight = 102;
constexpr std::uint32_t neoGeoWidth = 320;
constexpr std::uint32_t neoGeoHeight = 224;
constexpr std::size_t neoGeoPaletteBanks = 16;
constexpr std::size_t neoGeoColorsPerBank = 16;
constexpr std::size_t neoGeoPaletteBytes = 8192;

struct IndexedSurface {
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> tilePalettes;
    std::vector<RgbColor> colors;
    RgbImage preview;
};

ConversionResult failure(std::string code, std::string message)
{
    return {.status = ConversionStatus::Failed,
            .diagnostics = {{DiagnosticSeverity::Error, std::move(code),
                             std::move(message)}}};
}

ConversionResult cancelled()
{
    return {.status = ConversionStatus::Cancelled};
}

RgbColor sourcePixel(const RgbImage& image, std::uint32_t x, std::uint32_t y)
{
    const auto row = image.row(y);
    const std::size_t offset = static_cast<std::size_t>(x)
        * bytesPerPixel(image.pixelFormat());
    return {row[offset], row[offset + 1U], row[offset + 2U]};
}

void setPixel(RgbImage& image, std::uint32_t x, std::uint32_t y, RgbColor color)
{
    auto row = image.row(y);
    const std::size_t offset = static_cast<std::size_t>(x)
        * bytesPerPixel(image.pixelFormat());
    row[offset] = color.red;
    row[offset + 1U] = color.green;
    row[offset + 2U] = color.blue;
    if (image.pixelFormat() == PixelFormat::Rgba8888) row[offset + 3U] = 255U;
}

std::uint8_t to4(std::uint8_t value)
{
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(value) * 15U + 127U) / 255U);
}

std::uint8_t to5(std::uint8_t value)
{
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(value) * 31U + 127U) / 255U);
}

RgbColor lynxColor(RgbColor color)
{
    return {static_cast<std::uint8_t>(to4(color.red) * 17U),
            static_cast<std::uint8_t>(to4(color.green) * 17U),
            static_cast<std::uint8_t>(to4(color.blue) * 17U)};
}

RgbColor neoGeoColor(RgbColor color)
{
    return {static_cast<std::uint8_t>(to5(color.red) * 255U / 31U),
            static_cast<std::uint8_t>(to5(color.green) * 255U / 31U),
            static_cast<std::uint8_t>(to5(color.blue) * 255U / 31U)};
}

std::vector<RgbColor> selectedColors(const RgbImage& source,
                                     std::size_t count,
                                     bool neoGeo)
{
    PaletteSelectionResult selected = selectMedianCutPalette(
        source, count, MedianCutColorDepth::Rgb888);
    std::vector<RgbColor> colors;
    if (selected.palette) {
        colors.assign(selected.palette->colors().begin(),
                      selected.palette->colors().end());
    }
    if (colors.empty()) colors.push_back({0, 0, 0});
    for (RgbColor& color : colors)
        color = neoGeo ? neoGeoColor(color) : lynxColor(color);
    while (colors.size() < count) colors.push_back(colors.back());
    colors.resize(count);
    return colors;
}

std::optional<IndexedSurface> quantizeLynx(
    const RgbImage& source,
    const ConversionSettings& settings,
    CancellationToken cancellation,
    const ConversionProgressCallback& progress)
{
    auto preview = RgbImage::createTightlyPacked(
        source.width(), source.height(), source.pixelFormat());
    if (!preview) return std::nullopt;
    std::vector<RgbColor> colors = selectedColors(source, 16, false);
    const auto dithering = ditherConfiguration(settings.dither,
                                                settings.errorDistribution);
    if (!dithering) return std::nullopt;
    std::optional<ErrorDiffusionBuffer> errors;
    if (dithering->distributeError)
        errors = ErrorDiffusionBuffer::create(source.width(), source.height());
    if (dithering->distributeError && !errors) return std::nullopt;
    const ColorDistanceEvaluator evaluator(settings);
    std::vector<PreparedColorSample> prepared;
    for (const RgbColor color : colors)
        prepared.push_back(evaluator.prepare(RgbSample(color)));
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(source.width()) * source.height());
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (cancellation.isCancellationRequested()) return std::nullopt;
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            RgbSample sample(sourcePixel(source, x, y));
            if (dithering->ordered) {
                sample = *applyOrderedDither(sample, x, y,
                                             settings.orderedDitherMapSize,
                                             settings.orderedDitherBrightness);
            }
            if (errors)
                sample = errors->adjustedSample(sample, x, y,
                                                settings.errorAccumulation);
            std::size_t nearest = 0;
            double best = std::numeric_limits<double>::max();
            for (std::size_t entry = 0; entry < prepared.size(); ++entry) {
                const double candidate = evaluator.distanceSquared(sample,
                                                                    prepared[entry]);
                if (candidate < best) {
                    best = candidate;
                    nearest = entry;
                }
            }
            pixels[static_cast<std::size_t>(y) * source.width() + x]
                = static_cast<std::uint8_t>(nearest);
            setPixel(*preview, x, y, colors[nearest]);
            if (errors) {
                const RgbColor output = colors[nearest];
                errors->distribute(x, y,
                    {sample.red - output.red, sample.green - output.green,
                     sample.blue - output.blue}, dithering->kernel);
            }
        }
        if (progress) progress(*preview, y + 1U, source.height());
    }
    return IndexedSurface{std::move(pixels), {}, std::move(colors),
                          std::move(*preview)};
}

std::optional<IndexedSurface> quantizeNeoGeo(
    const RgbImage& source,
    const ConversionSettings& settings,
    std::uint32_t tileSize,
    CancellationToken cancellation,
    const ConversionProgressCallback& progress)
{
    auto preview = RgbImage::createTightlyPacked(
        source.width(), source.height(), source.pixelFormat());
    if (!preview) return std::nullopt;
    const std::vector<RgbColor> opaque = selectedColors(source, 240, true);
    std::vector<RgbColor> colors;
    colors.reserve(256);
    for (std::size_t bank = 0; bank < neoGeoPaletteBanks; ++bank) {
        colors.push_back({0, 0, 0});
        colors.insert(colors.end(), opaque.begin() + static_cast<std::ptrdiff_t>(bank * 15U),
                      opaque.begin() + static_cast<std::ptrdiff_t>((bank + 1U) * 15U));
    }
    const ColorDistanceEvaluator evaluator(settings);
    std::vector<PreparedColorSample> prepared;
    for (const RgbColor color : colors)
        prepared.push_back(evaluator.prepare(RgbSample(color)));

    const std::size_t columns = source.width() / tileSize;
    const std::size_t rows = source.height() / tileSize;
    std::vector<std::uint8_t> tilePalettes(columns * rows);
    for (std::size_t tileY = 0; tileY < rows; ++tileY) {
        if (cancellation.isCancellationRequested()) return std::nullopt;
        for (std::size_t tileX = 0; tileX < columns; ++tileX) {
            std::size_t bestBank = 0;
            double bestError = std::numeric_limits<double>::max();
            for (std::size_t bank = 0; bank < neoGeoPaletteBanks; ++bank) {
                double error = 0.0;
                for (std::size_t y = 0; y < tileSize; ++y) {
                    for (std::size_t x = 0; x < tileSize; ++x) {
                        const RgbSample sample(sourcePixel(
                            source,
                            static_cast<std::uint32_t>(tileX * tileSize + x),
                            static_cast<std::uint32_t>(tileY * tileSize + y)));
                        double nearest = std::numeric_limits<double>::max();
                        for (std::size_t entry = 1; entry < 16; ++entry) {
                            nearest = std::min(nearest,
                                evaluator.distanceSquared(sample,
                                    prepared[bank * 16U + entry]));
                        }
                        error += nearest;
                    }
                }
                if (error < bestError) {
                    bestError = error;
                    bestBank = bank;
                }
            }
            tilePalettes[tileY * columns + tileX]
                = static_cast<std::uint8_t>(bestBank);
        }
    }

    const auto dithering = ditherConfiguration(settings.dither,
                                                settings.errorDistribution);
    if (!dithering) return std::nullopt;
    std::optional<ErrorDiffusionBuffer> errors;
    if (dithering->distributeError)
        errors = ErrorDiffusionBuffer::create(source.width(), source.height());
    if (dithering->distributeError && !errors) return std::nullopt;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(source.width()) * source.height());
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (cancellation.isCancellationRequested()) return std::nullopt;
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const std::size_t bank = tilePalettes[
                static_cast<std::size_t>(y / tileSize) * columns + x / tileSize];
            RgbSample sample(sourcePixel(source, x, y));
            if (dithering->ordered) {
                sample = *applyOrderedDither(sample, x, y,
                                             settings.orderedDitherMapSize,
                                             settings.orderedDitherBrightness);
            }
            if (errors)
                sample = errors->adjustedSample(sample, x, y,
                                                settings.errorAccumulation);
            std::size_t nearest = 1;
            double best = std::numeric_limits<double>::max();
            for (std::size_t entry = 1; entry < 16; ++entry) {
                const double candidate = evaluator.distanceSquared(
                    sample, prepared[bank * 16U + entry]);
                if (candidate < best) {
                    best = candidate;
                    nearest = entry;
                }
            }
            pixels[static_cast<std::size_t>(y) * source.width() + x]
                = static_cast<std::uint8_t>(nearest);
            const RgbColor output = colors[bank * 16U + nearest];
            setPixel(*preview, x, y, output);
            if (errors) {
                errors->distribute(x, y,
                    {sample.red - output.red, sample.green - output.green,
                     sample.blue - output.blue}, dithering->kernel);
            }
        }
        if (progress) progress(*preview, y + 1U, source.height());
    }
    return IndexedSurface{std::move(pixels), std::move(tilePalettes),
                          std::move(colors), std::move(*preview)};
}

std::vector<std::uint8_t> lynxLineData(std::span<const std::uint8_t> pixels)
{
    std::vector<std::uint8_t> result;
    result.reserve(8263U);
    for (std::size_t y = 0; y < lynxHeight; ++y) {
        result.push_back(81U);
        for (std::size_t x = 0; x < lynxWidth; x += 2U) {
            result.push_back(static_cast<std::uint8_t>(
                (pixels[y * lynxWidth + x] << 4U)
                | pixels[y * lynxWidth + x + 1U]));
        }
    }
    result.push_back(0U);
    return result;
}

std::vector<std::uint8_t> lynxScb()
{
    std::vector<std::uint8_t> result(23U);
    result[0] = 0xc4U; // 4bpp, non-collideable background object.
    result[1] = 0xb0U; // Totally literal lines; reload HSIZE and VSIZE.
    result[2] = 0U;
    result[11] = 0x00U; result[12] = 0x01U; // HSIZE 1.0, little-endian.
    result[13] = 0x00U; result[14] = 0x01U; // VSIZE 1.0.
    for (std::size_t pair = 0; pair < 8; ++pair)
        result[15U + pair] = static_cast<std::uint8_t>((pair * 2U << 4U)
                                                       | (pair * 2U + 1U));
    return result;
}

std::vector<std::uint8_t> lynxPalette(std::span<const RgbColor> colors)
{
    std::vector<std::uint8_t> result(32U);
    for (std::size_t index = 0; index < 16; ++index) {
        result[index] = to4(colors[index].green);
        result[16U + index] = static_cast<std::uint8_t>(
            (to4(colors[index].blue) << 4U) | to4(colors[index].red));
    }
    return result;
}

std::uint16_t neoGeoColorWord(RgbColor color)
{
    const std::uint16_t red = to5(color.red);
    const std::uint16_t green = to5(color.green);
    const std::uint16_t blue = to5(color.blue);
    return static_cast<std::uint16_t>(((red >> 1U) << 8U)
        | ((green >> 1U) << 4U) | (blue >> 1U)
        | ((red & 1U) << 14U) | ((green & 1U) << 13U)
        | ((blue & 1U) << 12U));
}

void putBigWord(std::vector<std::uint8_t>& bytes,
                std::size_t wordAddress,
                std::uint16_t value)
{
    const std::size_t offset = wordAddress * 2U;
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> neoGeoPalette(std::span<const RgbColor> colors)
{
    std::vector<std::uint8_t> result(neoGeoPaletteBytes);
    putBigWord(result, 0, 0x8000U);
    for (std::size_t index = 1; index < colors.size(); ++index)
        putBigWord(result, index, neoGeoColorWord(colors[index]));
    putBigWord(result, 4095U, 0x8000U);
    return result;
}

std::vector<std::uint8_t> encodeFixPatterns(const IndexedSurface& surface)
{
    std::vector<std::uint8_t> result(128U * 1024U);
    constexpr std::size_t columns = neoGeoWidth / 8U;
    constexpr std::size_t rows = neoGeoHeight / 8U;
    for (std::size_t tileY = 0; tileY < rows; ++tileY) {
        for (std::size_t tileX = 0; tileX < columns; ++tileX) {
            const std::size_t tile = tileY * columns + tileX;
            for (std::size_t y = 0; y < 8; ++y) {
                for (std::size_t plane = 0; plane < 4; ++plane) {
                    std::uint8_t value = 0;
                    for (std::size_t x = 0; x < 8; ++x) {
                        const std::uint8_t pixel = surface.pixels[
                            (tileY * 8U + y) * neoGeoWidth + tileX * 8U + x];
                        if ((pixel & (1U << plane)) != 0U)
                            value = static_cast<std::uint8_t>(value | (0x80U >> x));
                    }
                    result[tile * 32U + y * 4U + plane] = value;
                }
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> fixMap(const IndexedSurface& surface)
{
    std::vector<std::uint8_t> result(8192U);
    constexpr std::size_t columns = neoGeoWidth / 8U;
    constexpr std::size_t rows = neoGeoHeight / 8U;
    for (std::size_t x = 0; x < columns; ++x) {
        for (std::size_t y = 0; y < rows; ++y) {
            const std::size_t tile = y * columns + x;
            const std::uint16_t word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(surface.tilePalettes[tile]) << 12U)
                | static_cast<std::uint16_t>(tile));
            putBigWord(result, x * 32U + y, word);
        }
    }
    return result;
}

std::vector<std::uint8_t> encodeNeoGeoSprites(const IndexedSurface& surface)
{
    constexpr std::size_t columns = neoGeoWidth / 16U;
    constexpr std::size_t rows = neoGeoHeight / 16U;
    std::vector<std::uint8_t> result(columns * rows * 128U);
    for (std::size_t tileX = 0; tileX < columns; ++tileX) {
        for (std::size_t tileY = 0; tileY < rows; ++tileY) {
            const std::size_t tile = tileX * rows + tileY;
            for (std::size_t y = 0; y < 16; ++y) {
                for (std::size_t half = 0; half < 2; ++half) {
                    for (std::size_t plane = 0; plane < 4; ++plane) {
                        std::uint8_t value = 0;
                        for (std::size_t x = 0; x < 8; ++x) {
                            const std::uint8_t pixel = surface.pixels[
                                (tileY * 16U + y) * neoGeoWidth
                                + tileX * 16U + half * 8U + x];
                            if ((pixel & (1U << plane)) != 0U)
                                value = static_cast<std::uint8_t>(
                                    value | (0x80U >> x));
                        }
                        result[tile * 128U + y * 8U + half * 4U + plane] = value;
                    }
                }
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> neoGeoSpriteVram(const IndexedSurface& surface)
{
    constexpr std::size_t columns = neoGeoWidth / 16U;
    constexpr std::size_t rows = neoGeoHeight / 16U;
    std::vector<std::uint8_t> result(0x8600U * 2U);
    for (std::size_t strip = 0; strip < columns; ++strip) {
        for (std::size_t row = 0; row < rows; ++row) {
            const std::size_t tile = strip * rows + row;
            const std::size_t cell = strip * 64U + row * 2U;
            putBigWord(result, cell, static_cast<std::uint16_t>(tile));
            putBigWord(result, cell + 1U,
                       static_cast<std::uint16_t>(
                           surface.tilePalettes[row * columns + strip] << 8U));
        }
        putBigWord(result, 0x8000U + strip, 0x0fffU);
        const std::uint16_t yAndFlags = static_cast<std::uint16_t>(
            ((496U & 0x01ffU) << 7U) | (strip == 0 ? 0U : 0x0040U) | rows);
        putBigWord(result, 0x8200U + strip, yAndFlags);
        putBigWord(result, 0x8400U + strip,
                   static_cast<std::uint16_t>((strip * 16U) << 7U));
    }
    return result;
}

std::vector<std::uint8_t> metadataRegisters(ConversionMode mode)
{
    std::vector<std::uint8_t> result(16U);
    result[0] = 'R'; result[1] = 'V'; result[2] = 'D'; result[3] = 'P';
    result[4] = static_cast<std::uint8_t>(mode);
    if (mode == ConversionMode::AtariLynxObjectScene) {
        result[5] = 1U; result[6] = 160U; result[7] = 102U;
        result[8] = 0x1fU; result[9] = 0xe0U; // 8160-byte framebuffer.
    } else {
        result[5] = mode == ConversionMode::NeoGeoFixLayer ? 0U : 20U;
        result[6] = 40U; result[7] = 28U;
        result[8] = 96U; // Maximum line sprites per scanline.
    }
    return result;
}

ConversionResult convertLynx(const RgbImage& source,
                             const ConversionSettings& settings,
                             CancellationToken cancellation,
                             ConversionProgressCallback progress)
{
    if (source.width() != lynxWidth || source.height() != lynxHeight)
        return failure("atari-lynx-source-size",
                       "Atari Lynx object scenes require a 160x102 prepared source.");
    auto surface = quantizeLynx(source, settings, cancellation, progress);
    if (!surface) {
        return cancellation.isCancellationRequested() ? cancelled()
            : failure("atari-lynx-quantization",
                      "The Atari Lynx object scene could not be quantized.");
    }
    TargetMemoryImage target{
        .profile = TargetProfileId::AtariLynx,
        .mode = settings.mode,
        .tables = {
            {TargetTableRole::Pattern, lynxLineData(surface->pixels)},
            {TargetTableRole::ObjectAttributes, lynxScb()},
            {TargetTableRole::Palette, lynxPalette(surface->colors)},
            {TargetTableRole::DisplayRegisters, metadataRegisters(settings.mode)},
        },
    };
    return {.status = ConversionStatus::Succeeded,
            .preview = std::move(surface->preview),
            .target = std::move(target)};
}

ConversionResult convertNeoGeo(const RgbImage& source,
                               const ConversionSettings& settings,
                               CancellationToken cancellation,
                               ConversionProgressCallback progress)
{
    if (source.width() != neoGeoWidth || source.height() != neoGeoHeight)
        return failure("neo-geo-source-size",
                       "Neo Geo scenes require a 320x224 prepared source.");
    const bool fix = settings.mode == ConversionMode::NeoGeoFixLayer;
    auto surface = quantizeNeoGeo(source, settings, fix ? 8U : 16U,
                                  cancellation, progress);
    if (!surface) {
        return cancellation.isCancellationRequested() ? cancelled()
            : failure("neo-geo-quantization",
                      "The Neo Geo scene could not be quantized.");
    }
    TargetMemoryImage target{
        .profile = TargetProfileId::NeoGeo,
        .mode = settings.mode,
        .tables = {},
    };
    if (fix) {
        target.tables.push_back({TargetTableRole::Pattern,
                                 encodeFixPatterns(*surface)});
        target.tables.push_back({TargetTableRole::TileMap, fixMap(*surface)});
    } else {
        target.tables.push_back({TargetTableRole::Pattern,
                                 encodeNeoGeoSprites(*surface)});
        target.tables.push_back({TargetTableRole::ObjectAttributes,
                                 neoGeoSpriteVram(*surface)});
    }
    target.tables.push_back({TargetTableRole::Palette,
                             neoGeoPalette(surface->colors)});
    target.tables.push_back({TargetTableRole::DisplayRegisters,
                             metadataRegisters(settings.mode)});
    return {.status = ConversionStatus::Succeeded,
            .preview = std::move(surface->preview),
            .target = std::move(target)};
}

} // namespace

ConversionResult convertObjectProcessor(const RgbImage& source,
                                        const ConversionSettings& settings,
                                        CancellationToken cancellation,
                                        ConversionProgressCallback progress)
{
    switch (settings.mode) {
    case ConversionMode::AtariLynxObjectScene:
        return convertLynx(source, settings, cancellation, std::move(progress));
    case ConversionMode::NeoGeoFixLayer:
    case ConversionMode::NeoGeoChainedSprites:
        return convertNeoGeo(source, settings, cancellation, std::move(progress));
    default:
        return failure("unsupported-object-processor-mode",
                       "The selected mode is not an object-processor mode.");
    }
}

} // namespace retrovdp::core
