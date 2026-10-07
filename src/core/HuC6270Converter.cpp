#include "retrovdp/core/HuC6270Converter.hpp"

#include "retrovdp/core/ColorMath.hpp"
#include "retrovdp/core/Dithering.hpp"
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

constexpr std::uint32_t tileSize = 8;
constexpr std::size_t hardwareColorCount = 512;
constexpr std::size_t paletteBankCount = 16;
constexpr std::size_t colorsPerPalette = 16;
constexpr std::size_t completeColorTableBytes = 512U * 2U;
constexpr std::size_t registerBytes = 21U * 2U;

using Histogram = std::array<std::uint16_t, hardwareColorCount>;
using HardwarePalette = std::array<std::uint16_t, colorsPerPalette>;
using IndexedTile = std::array<std::uint8_t, tileSize * tileSize>;

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

bool isHuC6270Mode(ConversionMode mode)
{
    return mode == ConversionMode::HuC6270Background256
        || mode == ConversionMode::HuC6270Background320;
}

std::uint8_t to3(std::uint8_t value)
{
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(value) * 7U + 127U) / 255U);
}

std::uint16_t hardwareCode(RgbColor color)
{
    return static_cast<std::uint16_t>(to3(color.blue)
        | (to3(color.red) << 3U) | (to3(color.green) << 6U));
}

RgbColor hardwareColor(std::uint16_t code)
{
    return {
        static_cast<std::uint8_t>(((code >> 3U) & 7U) * 255U / 7U),
        static_cast<std::uint8_t>(((code >> 6U) & 7U) * 255U / 7U),
        static_cast<std::uint8_t>((code & 7U) * 255U / 7U),
    };
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

HardwarePalette paletteFromHistogram(const Histogram& histogram,
                                     std::uint16_t backdrop)
{
    HardwarePalette result{};
    result.fill(backdrop);
    result[0] = backdrop;
    std::array<bool, hardwareColorCount> selected{};
    selected[backdrop] = true;
    for (std::size_t entry = 1; entry < colorsPerPalette; ++entry) {
        std::size_t best = backdrop;
        for (std::size_t color = 0; color < hardwareColorCount; ++color) {
            if (!selected[color] && histogram[color] > histogram[best]) best = color;
        }
        if (histogram[best] == 0) break;
        result[entry] = static_cast<std::uint16_t>(best);
        selected[best] = true;
    }
    return result;
}

double paletteError(const Histogram& histogram,
                    const HardwarePalette& palette,
                    const std::vector<double>& distances)
{
    double result = 0.0;
    for (std::size_t color = 0; color < hardwareColorCount; ++color) {
        if (histogram[color] == 0) continue;
        double nearest = std::numeric_limits<double>::max();
        for (const std::uint16_t entry : palette) {
            nearest = std::min(nearest,
                distances[color * hardwareColorCount + entry]);
        }
        result += nearest * histogram[color];
    }
    return result;
}

std::array<HardwarePalette, paletteBankCount> selectPalettes(
    std::span<const Histogram> tileHistograms,
    std::uint16_t backdrop,
    const std::vector<double>& distances,
    std::vector<std::uint8_t>& assignments)
{
    std::vector<HardwarePalette> candidates;
    std::vector<std::size_t> candidateFrequency;
    candidates.reserve(tileHistograms.size());
    for (const auto& histogram : tileHistograms) {
        const HardwarePalette candidate = paletteFromHistogram(histogram, backdrop);
        const auto found = std::ranges::find(candidates, candidate);
        if (found == candidates.end()) {
            candidates.push_back(candidate);
            candidateFrequency.push_back(1U);
        } else {
            ++candidateFrequency[static_cast<std::size_t>(found - candidates.begin())];
        }
    }

    std::vector<std::size_t> order(candidates.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::ranges::stable_sort(order, [&candidateFrequency](std::size_t left,
                                                         std::size_t right) {
        return candidateFrequency[left] > candidateFrequency[right];
    });

    std::array<HardwarePalette, paletteBankCount> palettes{};
    for (std::size_t bank = 0; bank < paletteBankCount; ++bank) {
        palettes[bank] = order.empty()
            ? HardwarePalette{} : candidates[order[std::min(bank, order.size() - 1U)]];
        palettes[bank][0] = backdrop;
    }

    assignments.assign(tileHistograms.size(), 0U);
    for (int iteration = 0; iteration < 4; ++iteration) {
        std::array<Histogram, paletteBankCount> bankHistograms{};
        std::array<std::size_t, paletteBankCount> bankCounts{};
        for (std::size_t tile = 0; tile < tileHistograms.size(); ++tile) {
            std::size_t bestBank = 0;
            double bestError = paletteError(tileHistograms[tile], palettes[0], distances);
            for (std::size_t bank = 1; bank < paletteBankCount; ++bank) {
                const double error = paletteError(
                    tileHistograms[tile], palettes[bank], distances);
                if (error < bestError) {
                    bestError = error;
                    bestBank = bank;
                }
            }
            assignments[tile] = static_cast<std::uint8_t>(bestBank);
            ++bankCounts[bestBank];
            for (std::size_t color = 0; color < hardwareColorCount; ++color) {
                bankHistograms[bestBank][color] = static_cast<std::uint16_t>(
                    std::min<unsigned>(65535U,
                        bankHistograms[bestBank][color]
                            + tileHistograms[tile][color]));
            }
        }
        for (std::size_t bank = 0; bank < paletteBankCount; ++bank) {
            if (bankCounts[bank] != 0)
                palettes[bank] = paletteFromHistogram(bankHistograms[bank], backdrop);
        }
    }
    return palettes;
}

std::vector<std::uint8_t> encodePatterns(std::span<const IndexedTile> patterns,
                                         std::size_t byteSize)
{
    std::vector<std::uint8_t> result(byteSize);
    for (std::size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
        const std::size_t base = patternIndex * 32U;
        for (std::size_t y = 0; y < tileSize; ++y) {
            std::array<std::uint8_t, 4> planes{};
            for (std::size_t x = 0; x < tileSize; ++x) {
                const std::uint8_t value = patterns[patternIndex][y * tileSize + x];
                for (std::size_t plane = 0; plane < 4; ++plane) {
                    if ((value & (1U << plane)) != 0U)
                        planes[plane] = static_cast<std::uint8_t>(
                            planes[plane] | (0x80U >> x));
                }
            }
            result[base + y * 2U] = planes[0];
            result[base + y * 2U + 1U] = planes[1];
            result[base + 16U + y * 2U] = planes[2];
            result[base + 16U + y * 2U + 1U] = planes[3];
        }
    }
    return result;
}

std::vector<std::uint8_t> encodePalette(
    const std::array<HardwarePalette, paletteBankCount>& palettes)
{
    std::vector<std::uint8_t> result(completeColorTableBytes);
    for (std::size_t bank = 0; bank < paletteBankCount; ++bank) {
        for (std::size_t entry = 0; entry < colorsPerPalette; ++entry) {
            const std::uint16_t word = palettes[bank][entry];
            const std::size_t offset = (bank * colorsPerPalette + entry) * 2U;
            result[offset] = static_cast<std::uint8_t>(word & 0xffU);
            result[offset + 1U] = static_cast<std::uint8_t>(word >> 8U);
        }
    }
    return result;
}

void setWord(std::vector<std::uint8_t>& bytes, std::size_t wordIndex,
             std::uint16_t value)
{
    bytes[wordIndex * 2U] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[wordIndex * 2U + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

std::vector<std::uint8_t> displayRegisters(ConversionMode mode,
                                           std::uint32_t height)
{
    const bool wide = mode == ConversionMode::HuC6270Background320;
    std::vector<std::uint8_t> result(registerBytes);
    setWord(result, 5, 0x0080U); // background enabled; sprites start disabled
    setWord(result, 9, wide ? 0x0010U : 0x0000U); // 64x32 or 32x32 BAT
    setWord(result, 10, 0x0202U);
    setWord(result, 11, static_cast<std::uint16_t>((wide ? 39U : 31U) | 0x0200U));
    setWord(result, 12, 0x0f02U);
    setWord(result, 13, static_cast<std::uint16_t>(height - 1U));
    setWord(result, 14, 0x0003U);
    setWord(result, 20, wide ? 1U : 0U); // appended HuC6260 clock-control word
    return result;
}

} // namespace

ConversionResult convertHuC6270Background(const RgbImage& source,
                                          const ConversionSettings& settings,
                                          CancellationToken cancellation,
                                          ConversionProgressCallback progress)
{
    if (!isHuC6270Mode(settings.mode)) {
        return failure("huc6270-wrong-mode",
                       "The HuC6270 converter requires a registered background mode.");
    }
    if (!validate(settings).empty()) {
        return failure("huc6270-invalid-settings",
                       "HuC6270 conversion settings are invalid.");
    }
    const auto& descriptor = displayMode(settings.mode);
    if (source.width() != descriptor.geometry.width
        || source.height() != descriptor.geometry.height) {
        return failure("huc6270-invalid-dimensions",
                       "The source geometry does not match the selected HuC6270 mode.");
    }
    const auto dithering = ditherConfiguration(settings.dither,
                                                settings.errorDistribution);
    if (!dithering) {
        return failure("huc6270-invalid-dither",
                       "The HuC6270 converter received an unsupported dither mode.");
    }
    if (cancellation.isCancellationRequested()) return cancelled();

    const std::size_t visibleColumns = source.width() / tileSize;
    const std::size_t visibleRows = source.height() / tileSize;
    const std::size_t tileCount = visibleColumns * visibleRows;
    std::vector<Histogram> histograms(tileCount);
    std::array<std::uint32_t, hardwareColorCount> allColors{};
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const std::uint16_t code = hardwareCode(sourcePixel(source, x, y));
            const std::size_t tile = static_cast<std::size_t>(y / tileSize)
                * visibleColumns + x / tileSize;
            ++histograms[tile][code];
            ++allColors[code];
        }
    }
    const std::uint16_t backdrop = static_cast<std::uint16_t>(
        std::max_element(allColors.begin(), allColors.end()) - allColors.begin());

    const ColorDistanceEvaluator evaluator(settings);
    std::array<PreparedColorSample, hardwareColorCount> prepared{};
    for (std::size_t code = 0; code < hardwareColorCount; ++code)
        prepared[code] = evaluator.prepare(RgbSample(hardwareColor(
            static_cast<std::uint16_t>(code))));
    std::vector<double> distances(hardwareColorCount * hardwareColorCount);
    for (std::size_t left = 0; left < hardwareColorCount; ++left) {
        for (std::size_t right = 0; right < hardwareColorCount; ++right) {
            distances[left * hardwareColorCount + right] =
                evaluator.distanceSquared(
                    RgbSample(hardwareColor(static_cast<std::uint16_t>(left))),
                    prepared[right]);
        }
    }

    std::vector<std::uint8_t> assignments;
    const auto palettes = selectPalettes(
        histograms, backdrop, distances, assignments);
    auto preview = RgbImage::createTightlyPacked(
        source.width(), source.height(), source.pixelFormat());
    if (!preview) {
        return failure("huc6270-preview-allocation",
                       "The HuC6270 preview could not be allocated.");
    }
    std::vector<IndexedTile> tiles(tileCount);
    std::optional<ErrorDiffusionBuffer> errors;
    if (dithering->distributeError) {
        errors = ErrorDiffusionBuffer::create(source.width(), source.height());
        if (!errors) {
            return failure("huc6270-error-buffer",
                           "The HuC6270 error-diffusion buffer could not be allocated.");
        }
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (cancellation.isCancellationRequested()) return cancelled();
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const std::size_t tile = static_cast<std::size_t>(y / tileSize)
                * visibleColumns + x / tileSize;
            const auto& palette = palettes[assignments[tile]];
            RgbSample sample(sourcePixel(source, x, y));
            if (dithering->ordered)
                sample = *applyOrderedDither(sample, x, y,
                                             settings.orderedDitherMapSize,
                                             settings.orderedDitherBrightness);
            if (errors)
                sample = errors->adjustedSample(sample, x, y,
                                                settings.errorAccumulation);
            std::uint8_t nearest = 0;
            double nearestDistance = std::numeric_limits<double>::max();
            for (std::size_t entry = 0; entry < colorsPerPalette; ++entry) {
                const double distance = evaluator.distanceSquared(
                    sample, prepared[palette[entry]]);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = static_cast<std::uint8_t>(entry);
                }
            }
            tiles[tile][static_cast<std::size_t>(y % tileSize) * tileSize
                        + x % tileSize] = nearest;
            const RgbColor output = hardwareColor(palette[nearest]);
            setPixel(*preview, x, y, output);
            if (errors) {
                errors->distribute(x, y,
                    {sample.red - output.red, sample.green - output.green,
                     sample.blue - output.blue}, dithering->kernel);
            }
        }
        if (progress && ((y + 1U) % 8U == 0U || y + 1U == source.height()))
            progress(*preview, y + 1U, source.height());
    }

    std::vector<IndexedTile> patterns;
    patterns.reserve(tileCount);
    std::vector<std::size_t> patternIndexes(tileCount);
    for (std::size_t tile = 0; tile < tileCount; ++tile) {
        const auto found = std::ranges::find(patterns, tiles[tile]);
        if (found == patterns.end()) {
            patternIndexes[tile] = patterns.size();
            patterns.push_back(tiles[tile]);
        } else {
            patternIndexes[tile] = static_cast<std::size_t>(found - patterns.begin());
        }
    }

    const bool wide = settings.mode == ConversionMode::HuC6270Background320;
    const std::size_t mapColumns = wide ? 64U : 32U;
    const std::size_t mapRows = 32U;
    const std::size_t mapBytes = mapColumns * mapRows * 2U;
    const std::size_t patternBytes = 64U * 1024U - mapBytes;
    const std::size_t firstPatternIndex = mapBytes / 32U;
    if (patterns.size() > patternBytes / 32U) {
        return failure("huc6270-pattern-overflow",
                       "The source requires more unique patterns than fit in HuC6270 VRAM.");
    }
    std::vector<std::uint8_t> bat(mapBytes);
    for (std::size_t row = 0; row < visibleRows; ++row) {
        for (std::size_t column = 0; column < visibleColumns; ++column) {
            const std::size_t tile = row * visibleColumns + column;
            const std::uint16_t word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(assignments[tile]) << 12U)
                | static_cast<std::uint16_t>(firstPatternIndex
                    + patternIndexes[tile]));
            const std::size_t offset = (row * mapColumns + column) * 2U;
            bat[offset] = static_cast<std::uint8_t>(word & 0xffU);
            bat[offset + 1U] = static_cast<std::uint8_t>(word >> 8U);
        }
    }

    TargetMemoryImage target{
        .profile = TargetProfileId::HuC6270,
        .mode = settings.mode,
        .palette = std::nullopt,
        .tables = {
            {TargetTableRole::Pattern, encodePatterns(patterns, patternBytes)},
            {TargetTableRole::TileMap, std::move(bat)},
            {TargetTableRole::Palette, encodePalette(palettes)},
            {TargetTableRole::DisplayRegisters,
             displayRegisters(settings.mode, source.height())},
        },
    };
    return {.status = ConversionStatus::Succeeded,
            .preview = std::move(preview),
            .diagnostics = {{DiagnosticSeverity::Information,
                             "huc6270-pattern-usage",
                             "The HuC6270 background uses "
                                 + std::to_string(patterns.size())
                                 + " unique native patterns."}},
            .target = std::move(target)};
}

} // namespace retrovdp::core
