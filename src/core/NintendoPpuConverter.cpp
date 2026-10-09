#include "retrovdp/core/NintendoPpuConverter.hpp"

#include "retrovdp/core/ColorMath.hpp"
#include "retrovdp/core/Dithering.hpp"
#include "retrovdp/core/PaletteSelection.hpp"
#include "retrovdp/core/TargetProfile.hpp"
#include "retrovdp/core/Validation.hpp"

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

constexpr std::size_t tileSize = 8;
constexpr std::size_t tilePixels = tileSize * tileSize;
using IndexedTile = std::array<std::uint8_t, tilePixels>;

struct ModeRules {
    TargetProfileId profile{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t bitsPerPixel{};
    std::size_t paletteBanks{};
    std::size_t colorsPerBank{};
    std::size_t patternCapacity{};
    std::size_t patternBytes{};
    bool color{};
    bool attributeMap{};
    bool snes{};
};

struct TileResult {
    IndexedTile pixels{};
    std::uint8_t palette{};
    std::uint16_t pattern{};
    bool horizontalFlip{};
    bool verticalFlip{};
};

ConversionResult failure(std::string code, std::string message)
{
    return {
        .status = ConversionStatus::Failed,
        .preview = std::nullopt,
        .diagnostics = {{DiagnosticSeverity::Error, std::move(code), std::move(message)}},
        .target = std::nullopt,
    };
}

ConversionResult cancelled()
{
    return {
        .status = ConversionStatus::Cancelled,
        .preview = std::nullopt,
        .diagnostics = {},
        .target = std::nullopt,
    };
}

std::optional<ModeRules> rules(ConversionMode mode)
{
    switch (mode) {
    case ConversionMode::GameBoyBackground:
        return ModeRules{TargetProfileId::GameBoy, 160, 144, 2, 1, 4,
                         256, 4096, false, false, false};
    case ConversionMode::GameBoyColorBackground:
        return ModeRules{TargetProfileId::GameBoyColor, 160, 144, 2, 8, 4,
                         512, 8192, true, true, false};
    case ConversionMode::SuperNesMode0Background:
        return ModeRules{TargetProfileId::SuperNes, 256, 224, 2, 8, 4,
                         1024, 16384, true, false, true};
    case ConversionMode::SuperNesMode1Background:
        return ModeRules{TargetProfileId::SuperNes, 256, 224, 4, 8, 16,
                         1024, 32768, true, false, true};
    case ConversionMode::SuperNesMode3Background:
        return ModeRules{TargetProfileId::SuperNes, 256, 224, 8, 1, 256,
                         896, 57344, true, false, true};
    default: return std::nullopt;
    }
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

RgbColor rgb555(RgbColor color)
{
    const auto channel = [](std::uint8_t value) {
        const unsigned five = (static_cast<unsigned>(value) * 31U + 127U) / 255U;
        return static_cast<std::uint8_t>((five * 255U + 15U) / 31U);
    };
    return {channel(color.red), channel(color.green), channel(color.blue)};
}

std::vector<RgbColor> selectColors(const RgbImage& source, const ModeRules& mode)
{
    if (!mode.color) {
        return {{224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}};
    }
    const std::size_t count = mode.paletteBanks * mode.colorsPerBank;
    PaletteSelectionResult selected = selectMedianCutPalette(
        source, count, MedianCutColorDepth::Rgb888);
    std::vector<RgbColor> colors;
    if (selected.palette) {
        colors.assign(selected.palette->colors().begin(), selected.palette->colors().end());
    }
    if (colors.empty()) colors.push_back({0, 0, 0});
    for (RgbColor& color : colors) color = rgb555(color);
    while (colors.size() < count) colors.push_back(colors.back());
    colors.resize(count);
    return colors;
}

double distance(RgbColor left, RgbColor right, const ConversionSettings& settings)
{
    return colorDistanceSquared(RgbSample(left), RgbSample(right), settings);
}

std::uint8_t bestPalette(const RgbImage& source,
                         std::uint32_t tileX,
                         std::uint32_t tileY,
                         const ModeRules& mode,
                         std::span<const RgbColor> colors,
                         const ConversionSettings& settings)
{
    std::uint8_t best = 0;
    double bestError = std::numeric_limits<double>::max();
    for (std::size_t bank = 0; bank < mode.paletteBanks; ++bank) {
        double error = 0.0;
        const auto palette = colors.subspan(bank * mode.colorsPerBank,
                                            mode.colorsPerBank);
        for (std::uint32_t y = 0; y < tileSize; ++y) {
            for (std::uint32_t x = 0; x < tileSize; ++x) {
                const RgbColor pixel = sourcePixel(
                    source, tileX * tileSize + x, tileY * tileSize + y);
                double nearest = std::numeric_limits<double>::max();
                for (const RgbColor candidate : palette)
                    nearest = std::min(nearest, distance(pixel, candidate, settings));
                error += nearest;
            }
        }
        if (error < bestError) {
            bestError = error;
            best = static_cast<std::uint8_t>(bank);
        }
    }
    return best;
}

IndexedTile transformed(const IndexedTile& source, bool horizontal, bool vertical)
{
    IndexedTile result{};
    for (std::size_t y = 0; y < tileSize; ++y) {
        for (std::size_t x = 0; x < tileSize; ++x) {
            const std::size_t sourceX = horizontal ? tileSize - 1U - x : x;
            const std::size_t sourceY = vertical ? tileSize - 1U - y : y;
            result[y * tileSize + x] = source[sourceY * tileSize + sourceX];
        }
    }
    return result;
}

std::size_t mismatch(const IndexedTile& left, const IndexedTile& right)
{
    std::size_t result = 0;
    for (std::size_t index = 0; index < left.size(); ++index)
        result += left[index] != right[index];
    return result;
}

void assignPattern(TileResult& tile,
                   std::vector<IndexedTile>& patterns,
                   const ModeRules& mode,
                   bool& reduced)
{
    const std::array transforms{
        std::pair{false, false}, std::pair{true, false},
        std::pair{false, true}, std::pair{true, true}};
    const std::size_t transformCount = mode.profile == TargetProfileId::GameBoy ? 1U : 4U;
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        for (std::size_t transformIndex = 0; transformIndex < transformCount;
             ++transformIndex) {
            const auto [horizontal, vertical] = transforms[transformIndex];
            if (tile.pixels == transformed(patterns[index], horizontal, vertical)) {
                tile.pattern = static_cast<std::uint16_t>(index);
                tile.horizontalFlip = horizontal;
                tile.verticalFlip = vertical;
                return;
            }
        }
    }
    if (patterns.size() < mode.patternCapacity) {
        tile.pattern = static_cast<std::uint16_t>(patterns.size());
        patterns.push_back(tile.pixels);
        return;
    }

    reduced = true;
    std::size_t bestPattern = 0;
    std::size_t bestError = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        for (std::size_t transformIndex = 0; transformIndex < transformCount;
             ++transformIndex) {
            const auto [horizontal, vertical] = transforms[transformIndex];
            const std::size_t error = mismatch(
                tile.pixels, transformed(patterns[index], horizontal, vertical));
            if (error < bestError) {
                bestError = error;
                bestPattern = index;
                tile.horizontalFlip = horizontal;
                tile.verticalFlip = vertical;
            }
        }
    }
    tile.pattern = static_cast<std::uint16_t>(bestPattern);
}

std::vector<std::uint8_t> encodePatterns(std::span<const IndexedTile> patterns,
                                         const ModeRules& mode)
{
    const std::size_t bytesPerTile = mode.bitsPerPixel * 8U;
    std::vector<std::uint8_t> result(mode.patternBytes);
    for (std::size_t tile = 0;
         tile < patterns.size() && (tile + 1U) * bytesPerTile <= result.size(); ++tile) {
        for (std::size_t plane = 0; plane < mode.bitsPerPixel; ++plane) {
            for (std::size_t y = 0; y < tileSize; ++y) {
                std::uint8_t value = 0;
                for (std::size_t x = 0; x < tileSize; ++x) {
                    if ((patterns[tile][y * tileSize + x] & (1U << plane)) != 0U)
                        value = static_cast<std::uint8_t>(value | (0x80U >> x));
                }
                const std::size_t offset = mode.snes
                    ? tile * bytesPerTile + (plane / 2U) * 16U + y * 2U + plane % 2U
                    : tile * bytesPerTile + y * 2U + plane;
                result[offset] = value;
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> encodePalette(std::span<const RgbColor> colors,
                                        const ModeRules& mode)
{
    if (!mode.color) return {0xe4U, 0xe4U, 0xe4U};
    std::vector<std::uint8_t> result(mode.snes ? 512U : 128U);
    const auto writeColors = [&result](std::span<const RgbColor> source,
                                       std::size_t destination) {
        for (std::size_t index = 0;
             index < source.size() && destination + index * 2U + 1U < result.size();
             ++index) {
            const RgbColor color = source[index];
            const std::uint16_t word = static_cast<std::uint16_t>(
                (color.red * 31U / 255U)
                | ((color.green * 31U / 255U) << 5U)
                | ((color.blue * 31U / 255U) << 10U));
            result[destination + index * 2U] = static_cast<std::uint8_t>(word);
            result[destination + index * 2U + 1U] = static_cast<std::uint8_t>(word >> 8U);
        }
    };
    writeColors(colors, 0);
    if (!mode.snes) writeColors(colors, 64U);
    return result;
}

std::vector<std::uint8_t> registers(ConversionMode mode, const ModeRules& rules)
{
    if (!rules.snes) {
        std::vector<std::uint8_t> result(12U);
        result[0] = 0x91U; // LCD enabled, BG enabled, unsigned tile data.
        result[7] = rules.color ? 0x01U : 0xfcU;
        return result;
    }
    std::vector<std::uint8_t> result(64U);
    result[0x05] = mode == ConversionMode::SuperNesMode0Background ? 0U
        : mode == ConversionMode::SuperNesMode1Background ? 1U : 3U;
    result[0x07] = 0U; // BG1 map at VRAM word address zero, 32x32.
    result[0x0b] = 0x01U; // BG1 character data at VRAM word address $1000.
    result[0x2c] = 0x01U; // BG1 on the main screen.
    return result;
}

} // namespace

ConversionResult convertNintendoPpu(const RgbImage& source,
                                    const ConversionSettings& settings,
                                    CancellationToken cancellation,
                                    ConversionProgressCallback progress)
{
    const auto mode = rules(settings.mode);
    if (!mode) return failure("nintendo-ppu-wrong-mode",
                              "The Nintendo PPU converter requires a registered PPU mode.");
    if (!validate(settings).empty())
        return failure("nintendo-ppu-invalid-settings",
                       "Nintendo PPU conversion settings are invalid.");
    if (source.width() != mode->width || source.height() != mode->height)
        return failure("nintendo-ppu-invalid-dimensions",
                       "The source geometry does not match the selected PPU mode.");
    const auto dithering = ditherConfiguration(
        settings.dither, settings.errorDistribution);
    if (!dithering)
        return failure("nintendo-ppu-invalid-dither",
                       "The Nintendo PPU converter received an unsupported dither mode.");
    if (cancellation.isCancellationRequested()) return cancelled();

    auto preview = RgbImage::createTightlyPacked(
        source.width(), source.height(), source.pixelFormat());
    if (!preview) return failure("nintendo-ppu-preview-allocation",
                                 "The Nintendo PPU preview could not be allocated.");
    const std::vector<RgbColor> colors = selectColors(source, *mode);
    const std::size_t columns = source.width() / tileSize;
    const std::size_t rows = source.height() / tileSize;
    std::vector<TileResult> tiles(columns * rows);
    std::vector<IndexedTile> patterns;
    patterns.reserve(std::min(mode->patternCapacity, tiles.size()));
    for (std::size_t tileY = 0; tileY < rows; ++tileY) {
        if (cancellation.isCancellationRequested()) return cancelled();
        for (std::size_t tileX = 0; tileX < columns; ++tileX) {
            TileResult& tile = tiles[tileY * columns + tileX];
            tile.palette = bestPalette(source,
                static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY),
                *mode, colors, settings);
        }
    }

    const ColorDistanceEvaluator evaluator(settings);
    std::vector<PreparedColorSample> prepared;
    prepared.reserve(colors.size());
    for (const RgbColor color : colors)
        prepared.push_back(evaluator.prepare(RgbSample(color)));
    std::optional<ErrorDiffusionBuffer> errors;
    if (dithering->distributeError) {
        errors = ErrorDiffusionBuffer::create(source.width(), source.height());
        if (!errors)
            return failure("nintendo-ppu-error-buffer",
                           "The Nintendo PPU error-diffusion buffer could not be allocated.");
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (cancellation.isCancellationRequested()) return cancelled();
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            TileResult& tile = tiles[static_cast<std::size_t>(y / tileSize) * columns
                                     + x / tileSize];
            const std::size_t paletteStart = static_cast<std::size_t>(tile.palette)
                * mode->colorsPerBank;
            RgbSample sample(sourcePixel(source, x, y));
            if (dithering->ordered) {
                sample = *applyOrderedDither(sample, x, y,
                                             settings.orderedDitherMapSize,
                                             settings.orderedDitherBrightness);
            }
            if (errors)
                sample = errors->adjustedSample(sample, x, y, settings.errorAccumulation);
            std::size_t nearest = 0;
            double nearestDistance = std::numeric_limits<double>::max();
            for (std::size_t entry = 0; entry < mode->colorsPerBank; ++entry) {
                const double candidate = evaluator.distanceSquared(
                    sample, prepared[paletteStart + entry]);
                if (candidate < nearestDistance) {
                    nearestDistance = candidate;
                    nearest = entry;
                }
            }
            tile.pixels[static_cast<std::size_t>(y % tileSize) * tileSize
                        + x % tileSize] = static_cast<std::uint8_t>(nearest);
            const RgbColor output = colors[paletteStart + nearest];
            setPixel(*preview, x, y, output);
            if (errors) {
                errors->distribute(x, y,
                    {sample.red - output.red, sample.green - output.green,
                     sample.blue - output.blue}, dithering->kernel);
            }
        }
        if (progress && ((y + 1U) % tileSize == 0U || y + 1U == source.height()))
            progress(*preview, y + 1U, source.height());
    }

    bool reduced = false;
    for (TileResult& tile : tiles) assignPattern(tile, patterns, *mode, reduced);

    // Reconstruct the final preview from the encoded pattern choices. This is
    // observable for Game Boy sources that exceed the active 256-tile window
    // and are reduced to their closest reusable patterns.
    for (std::size_t tileY = 0; tileY < rows; ++tileY) {
        for (std::size_t tileX = 0; tileX < columns; ++tileX) {
            const TileResult& tile = tiles[tileY * columns + tileX];
            const IndexedTile pixels = transformed(
                patterns[tile.pattern], tile.horizontalFlip, tile.verticalFlip);
            const auto palette = std::span(colors).subspan(
                static_cast<std::size_t>(tile.palette) * mode->colorsPerBank,
                mode->colorsPerBank);
            for (std::size_t y = 0; y < tileSize; ++y) {
                for (std::size_t x = 0; x < tileSize; ++x) {
                    setPixel(*preview,
                        static_cast<std::uint32_t>(tileX * tileSize + x),
                        static_cast<std::uint32_t>(tileY * tileSize + y),
                        palette[pixels[y * tileSize + x]]);
                }
            }
        }
    }

    std::vector<std::uint8_t> map(mode->snes ? 2048U : 1024U);
    std::vector<std::uint8_t> attributes(mode->attributeMap ? 1024U : 0U);
    for (std::size_t y = 0; y < rows; ++y) {
        for (std::size_t x = 0; x < columns; ++x) {
            const TileResult& tile = tiles[y * columns + x];
            const std::size_t cell = y * 32U + x;
            if (mode->snes) {
                std::uint16_t word = static_cast<std::uint16_t>(tile.pattern & 0x03ffU);
                word |= static_cast<std::uint16_t>((tile.palette & 0x07U) << 10U);
                if (tile.horizontalFlip) word |= 0x4000U;
                if (tile.verticalFlip) word |= 0x8000U;
                map[cell * 2U] = static_cast<std::uint8_t>(word);
                map[cell * 2U + 1U] = static_cast<std::uint8_t>(word >> 8U);
            } else {
                map[cell] = static_cast<std::uint8_t>(tile.pattern & 0xffU);
                if (mode->attributeMap) {
                    std::uint8_t attribute = tile.palette & 0x07U;
                    if (tile.pattern >= 256U) attribute |= 0x08U;
                    if (tile.horizontalFlip) attribute |= 0x20U;
                    if (tile.verticalFlip) attribute |= 0x40U;
                    attributes[cell] = attribute;
                }
            }
        }
    }

    TargetMemoryImage target{
        .profile = mode->profile,
        .mode = settings.mode,
        .palette = std::nullopt,
        .tables = {
            {TargetTableRole::Pattern, encodePatterns(patterns, *mode)},
            {TargetTableRole::TileMap, std::move(map)},
        },
    };
    if (mode->attributeMap)
        target.tables.push_back({TargetTableRole::AttributeMap, std::move(attributes)});
    target.tables.push_back({TargetTableRole::Palette, encodePalette(colors, *mode)});
    target.tables.push_back({TargetTableRole::DisplayRegisters,
                             registers(settings.mode, *mode)});

    std::vector<ConversionDiagnostic> diagnostics;
    if (reduced) diagnostics.push_back({
        DiagnosticSeverity::Warning,
        "nintendo-ppu-pattern-limit",
        "The source exceeded the active tile-addressing budget; the closest reusable patterns were selected.",
    });
    return {
        .status = ConversionStatus::Succeeded,
        .preview = std::move(preview),
        .diagnostics = std::move(diagnostics),
        .target = std::move(target),
    };
}

} // namespace retrovdp::core
