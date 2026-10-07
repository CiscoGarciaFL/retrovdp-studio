#include "retrovdp/core/CommodoreVdpConverter.hpp"

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

constexpr std::array<RgbColor, 16> c64Palette{{
    {0, 0, 0}, {255, 255, 255}, {136, 0, 0}, {170, 255, 238},
    {204, 68, 204}, {0, 204, 85}, {0, 0, 170}, {238, 238, 119},
    {221, 136, 85}, {102, 68, 0}, {255, 119, 119}, {51, 51, 51},
    {119, 119, 119}, {170, 255, 102}, {0, 136, 255}, {187, 187, 187},
}};

// Nominal VIC-20 colors. Analog output varies by 6560/6561 revision and set.
constexpr std::array<RgbColor, 16> vicPalette{{
    {0, 0, 0}, {255, 255, 255}, {182, 31, 33}, {77, 240, 255},
    {180, 65, 223}, {58, 209, 79}, {43, 43, 216}, {255, 255, 79},
    {216, 143, 34}, {255, 194, 91}, {255, 116, 118}, {148, 255, 255},
    {255, 137, 255}, {138, 255, 159}, {128, 128, 255}, {255, 255, 191},
}};

using CellHistogram = std::array<std::uint16_t, 16>;
using IndexedCell = std::array<std::uint8_t, 64>;

struct ModeFacts {
    bool vicII{};
    bool bitmap{};
    bool multicolor{};
    std::uint32_t logicalWidth{};
    std::uint32_t height{};
    std::size_t columns{};
    std::size_t rows{};
};

struct CharacterCompilation {
    std::vector<std::uint8_t> patterns;
    std::vector<std::uint8_t> screen;
    std::vector<std::uint8_t> colorRam;
    bool reduced{};
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

std::optional<ModeFacts> facts(ConversionMode mode)
{
    switch (mode) {
    case ConversionMode::VicIIHiresCharacter:
        return ModeFacts{true, false, false, 320, 200, 40, 25};
    case ConversionMode::VicIIMulticolorCharacter:
        return ModeFacts{true, false, true, 160, 200, 40, 25};
    case ConversionMode::VicIIHiresBitmap:
        return ModeFacts{true, true, false, 320, 200, 40, 25};
    case ConversionMode::VicIIMulticolorBitmap:
        return ModeFacts{true, true, true, 160, 200, 40, 25};
    case ConversionMode::VicHiresCharacter:
        return ModeFacts{false, false, false, 176, 184, 22, 23};
    case ConversionMode::VicMulticolorCharacter:
        return ModeFacts{false, false, true, 88, 184, 22, 23};
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

std::uint8_t nearestColor(RgbSample sample,
                          std::span<const PreparedColorSample> prepared,
                          const ColorDistanceEvaluator& evaluator,
                          std::span<const std::uint8_t> allowed = {})
{
    std::uint8_t nearest = allowed.empty() ? 0U : allowed.front();
    double nearestDistance = std::numeric_limits<double>::max();
    if (allowed.empty()) {
        for (std::size_t color = 0; color < prepared.size(); ++color) {
            const double distance = evaluator.distanceSquared(sample, prepared[color]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = static_cast<std::uint8_t>(color);
            }
        }
    } else {
        for (const std::uint8_t color : allowed) {
            const double distance = evaluator.distanceSquared(sample, prepared[color]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = color;
            }
        }
    }
    return nearest;
}

std::uint8_t mostFrequent(const std::array<std::uint32_t, 16>& histogram,
                          std::span<const bool> excluded = {})
{
    std::uint8_t best = 0;
    for (std::uint8_t color = 0; color < 16; ++color) {
        if (!excluded.empty() && excluded[color]) continue;
        if ((!excluded.empty() && excluded[best])
            || histogram[color] > histogram[best]) best = color;
    }
    return best;
}

std::uint8_t bestLocalColor(const CellHistogram& histogram,
                            std::span<const std::uint8_t> shared,
                            std::uint8_t maximum,
                            const std::array<std::array<double, 16>, 16>& distances)
{
    std::uint8_t best = 0;
    double bestError = std::numeric_limits<double>::max();
    for (std::uint8_t candidate = 0; candidate <= maximum; ++candidate) {
        double error = 0.0;
        for (std::size_t color = 0; color < 16; ++color) {
            if (histogram[color] == 0) continue;
            double nearest = distances[color][candidate];
            for (const std::uint8_t sharedColor : shared)
                nearest = std::min(nearest, distances[color][sharedColor]);
            error += nearest * histogram[color];
        }
        if (error < bestError) {
            bestError = error;
            best = candidate;
        }
    }
    return best;
}

std::array<std::uint8_t, 4> bestBitmapColors(
    const CellHistogram& histogram,
    std::uint8_t backdrop,
    std::size_t count,
    const std::array<std::array<double, 16>, 16>& distances)
{
    std::array<std::uint8_t, 4> result{backdrop, backdrop, backdrop, backdrop};
    std::array<bool, 16> selected{};
    selected[backdrop] = true;
    for (std::size_t entry = 1; entry < count; ++entry) {
        std::uint8_t best = 0;
        double bestGain = -1.0;
        for (std::uint8_t candidate = 0; candidate < 16; ++candidate) {
            if (selected[candidate]) continue;
            double gain = 0.0;
            for (std::size_t color = 0; color < 16; ++color) {
                if (histogram[color] == 0) continue;
                double prior = std::numeric_limits<double>::max();
                for (std::size_t existing = 0; existing < entry; ++existing)
                    prior = std::min(prior, distances[color][result[existing]]);
                gain += std::max(0.0, prior - distances[color][candidate])
                    * histogram[color];
            }
            if (gain > bestGain) {
                bestGain = gain;
                best = candidate;
            }
        }
        result[entry] = best;
        selected[best] = true;
    }
    return result;
}

std::vector<std::uint8_t> encodeCell(const IndexedCell& pixels,
                                     bool multicolor)
{
    std::vector<std::uint8_t> result(8U);
    for (std::size_t row = 0; row < 8; ++row) {
        std::uint8_t value = 0;
        const std::size_t width = multicolor ? 4U : 8U;
        for (std::size_t column = 0; column < width; ++column) {
            if (multicolor)
                value = static_cast<std::uint8_t>(value
                    | ((pixels[row * 8U + column] & 3U) << (6U - column * 2U)));
            else if (pixels[row * 8U + column] != 0U)
                value = static_cast<std::uint8_t>(value | (0x80U >> column));
        }
        result[row] = value;
    }
    return result;
}

std::size_t cellDifference(const IndexedCell& left, const IndexedCell& right,
                           std::size_t logicalCellWidth)
{
    std::size_t result = 0;
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t column = 0; column < logicalCellWidth; ++column)
            result += left[row * 8U + column] != right[row * 8U + column];
    }
    return result;
}

std::vector<std::size_t> chooseCharacterPatterns(
    std::span<const IndexedCell> cells,
    std::size_t logicalCellWidth,
    std::vector<IndexedCell>& patterns,
    bool& reduced)
{
    std::vector<std::size_t> frequencies;
    std::vector<std::size_t> uniqueIndex(cells.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        const auto found = std::ranges::find(patterns, cells[cell]);
        if (found == patterns.end()) {
            uniqueIndex[cell] = patterns.size();
            patterns.push_back(cells[cell]);
            frequencies.push_back(1U);
        } else {
            uniqueIndex[cell] = static_cast<std::size_t>(found - patterns.begin());
            ++frequencies[uniqueIndex[cell]];
        }
    }
    reduced = patterns.size() > 256U;
    if (!reduced) return uniqueIndex;

    std::vector<std::size_t> order(patterns.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::ranges::stable_sort(order, [&frequencies](std::size_t left, std::size_t right) {
        return frequencies[left] > frequencies[right];
    });
    std::vector<IndexedCell> selected;
    selected.reserve(256U);
    for (std::size_t index = 0; index < 256U; ++index)
        selected.push_back(patterns[order[index]]);
    patterns = std::move(selected);

    std::vector<std::size_t> result(cells.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        std::size_t best = 0;
        std::size_t bestDifference = std::numeric_limits<std::size_t>::max();
        for (std::size_t pattern = 0; pattern < patterns.size(); ++pattern) {
            const std::size_t difference = cellDifference(
                cells[cell], patterns[pattern], logicalCellWidth);
            if (difference < bestDifference) {
                bestDifference = difference;
                best = pattern;
                if (difference == 0) break;
            }
        }
        result[cell] = best;
    }
    return result;
}

std::vector<std::uint8_t> vicIIRegisters(ConversionMode mode,
                                         std::array<std::uint8_t, 4> global)
{
    std::vector<std::uint8_t> result(47U);
    const bool bitmap = mode == ConversionMode::VicIIHiresBitmap
        || mode == ConversionMode::VicIIMulticolorBitmap;
    const bool multicolor = mode == ConversionMode::VicIIMulticolorCharacter
        || mode == ConversionMode::VicIIMulticolorBitmap;
    result[0x11] = static_cast<std::uint8_t>(bitmap ? 0x3bU : 0x1bU);
    result[0x16] = static_cast<std::uint8_t>(multicolor ? 0x18U : 0x08U);
    result[0x18] = static_cast<std::uint8_t>(bitmap ? 0x18U : 0x14U);
    result[0x20] = global[0];
    result[0x21] = global[0];
    result[0x22] = global[1];
    result[0x23] = global[2];
    return result;
}

std::vector<std::uint8_t> vicRegisters(bool multicolor,
                                       std::array<std::uint8_t, 4> global)
{
    std::vector<std::uint8_t> result{
        0x0cU, 0x26U, 0x96U, 0x2eU, 0x00U, 0xffU, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1bU,
    };
    if (multicolor) {
        result[14] = static_cast<std::uint8_t>(global[3] << 4U);
        result[15] = static_cast<std::uint8_t>(
            (global[0] << 4U) | 0x08U | (global[1] & 7U));
    } else {
        result[15] = static_cast<std::uint8_t>(
            (global[0] << 4U) | 0x08U | (global[1] & 7U));
    }
    return result;
}

} // namespace

ConversionResult convertCommodoreDisplay(const RgbImage& source,
                                         const ConversionSettings& settings,
                                         CancellationToken cancellation,
                                         ConversionProgressCallback progress)
{
    const auto mode = facts(settings.mode);
    if (!mode) {
        return failure("commodore-vdp-wrong-mode",
                       "The Commodore converter requires a registered VIC or VIC-II mode.");
    }
    if (!validate(settings).empty()) {
        return failure("commodore-vdp-invalid-settings",
                       "Commodore conversion settings are invalid.");
    }
    if (source.width() != mode->logicalWidth || source.height() != mode->height) {
        return failure("commodore-vdp-invalid-dimensions",
                       "The source geometry does not match the selected Commodore mode.");
    }
    const auto dithering = ditherConfiguration(settings.dither,
                                                settings.errorDistribution);
    if (!dithering) {
        return failure("commodore-vdp-invalid-dither",
                       "The Commodore converter received an unsupported dither mode.");
    }
    if (cancellation.isCancellationRequested()) return cancelled();

    const auto& palette = mode->vicII ? c64Palette : vicPalette;
    const ColorDistanceEvaluator evaluator(settings);
    std::array<PreparedColorSample, 16> prepared{};
    std::array<std::array<double, 16>, 16> distances{};
    for (std::size_t color = 0; color < 16; ++color)
        prepared[color] = evaluator.prepare(RgbSample(palette[color]));
    for (std::size_t left = 0; left < 16; ++left) {
        for (std::size_t right = 0; right < 16; ++right) {
            distances[left][right] = evaluator.distanceSquared(
                RgbSample(palette[left]), prepared[right]);
        }
    }

    const std::size_t logicalCellWidth = mode->multicolor ? 4U : 8U;
    const std::size_t cellCount = mode->columns * mode->rows;
    std::vector<CellHistogram> histograms(cellCount);
    std::array<std::uint32_t, 16> allColors{};
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const std::uint8_t color = nearestColor(
                RgbSample(sourcePixel(source, x, y)), prepared, evaluator);
            const std::size_t cell = static_cast<std::size_t>(y / 8U)
                * mode->columns + x / logicalCellWidth;
            ++histograms[cell][color];
            ++allColors[color];
        }
    }

    std::array<bool, 16> selected{};
    std::array<std::uint8_t, 4> global{};
    global[0] = mostFrequent(allColors);
    selected[global[0]] = true;
    for (std::size_t entry = 1; entry < 4; ++entry) {
        global[entry] = mostFrequent(allColors, selected);
        selected[global[entry]] = true;
    }

    std::vector<std::array<std::uint8_t, 4>> cellPalettes(cellCount);
    for (std::size_t cell = 0; cell < cellCount; ++cell) {
        if (!mode->multicolor) {
            if (mode->bitmap) {
                cellPalettes[cell] = bestBitmapColors(
                    histograms[cell], global[0], 2U, distances);
            } else {
                const std::array<std::uint8_t, 1> shared{global[0]};
                cellPalettes[cell] = {global[0], bestLocalColor(
                    histograms[cell], shared, mode->vicII ? 15U : 7U, distances),
                    global[0], global[0]};
            }
        } else if (mode->bitmap) {
            cellPalettes[cell] = bestBitmapColors(
                histograms[cell], global[0], 4U, distances);
        } else if (mode->vicII) {
            const std::array<std::uint8_t, 3> shared{
                global[0], global[1], global[2]};
            cellPalettes[cell] = {global[0], global[1], global[2],
                bestLocalColor(histograms[cell], shared, 7U, distances)};
        } else {
            const std::array<std::uint8_t, 3> shared{
                global[0], static_cast<std::uint8_t>(global[1] & 7U), global[3]};
            // VIC codes are 00 background, 01 border, 10 local, 11 auxiliary.
            cellPalettes[cell] = {shared[0], shared[1],
                bestLocalColor(histograms[cell], shared, 7U, distances), shared[2]};
        }
    }

    auto preview = RgbImage::createTightlyPacked(
        source.width(), source.height(), source.pixelFormat());
    if (!preview) {
        return failure("commodore-vdp-preview-allocation",
                       "The Commodore preview could not be allocated.");
    }
    std::vector<IndexedCell> cells(cellCount);
    std::optional<ErrorDiffusionBuffer> errors;
    if (dithering->distributeError) {
        errors = ErrorDiffusionBuffer::create(source.width(), source.height());
        if (!errors) {
            return failure("commodore-vdp-error-buffer",
                           "The Commodore error-diffusion buffer could not be allocated.");
        }
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (cancellation.isCancellationRequested()) return cancelled();
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const std::size_t cell = static_cast<std::size_t>(y / 8U)
                * mode->columns + x / logicalCellWidth;
            RgbSample sample(sourcePixel(source, x, y));
            if (dithering->ordered)
                sample = *applyOrderedDither(sample, x, y,
                                             settings.orderedDitherMapSize,
                                             settings.orderedDitherBrightness);
            if (errors)
                sample = errors->adjustedSample(sample, x, y,
                                                settings.errorAccumulation);
            std::uint8_t index = 0;
            double nearestDistance = std::numeric_limits<double>::max();
            const std::size_t entries = mode->multicolor ? 4U : 2U;
            for (std::size_t candidate = 0; candidate < entries; ++candidate) {
                const double distance = evaluator.distanceSquared(
                    sample, prepared[cellPalettes[cell][candidate]]);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    index = static_cast<std::uint8_t>(candidate);
                }
            }
            const std::size_t localX = x % logicalCellWidth;
            const std::size_t localY = y % 8U;
            cells[cell][localY * 8U + localX] = index;
            const RgbColor output = palette[cellPalettes[cell][index]];
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

    std::vector<TargetMemoryTable> tables;
    bool patternsReduced = false;
    if (!mode->bitmap) {
        std::vector<IndexedCell> patterns;
        const std::vector<std::size_t> indexes = chooseCharacterPatterns(
            cells, logicalCellWidth, patterns, patternsReduced);
        std::vector<std::uint8_t> patternBytes(2048U);
        for (std::size_t pattern = 0; pattern < patterns.size(); ++pattern) {
            const auto encoded = encodeCell(patterns[pattern], mode->multicolor);
            std::ranges::copy(encoded, patternBytes.begin()
                + static_cast<std::ptrdiff_t>(pattern * 8U));
        }
        std::vector<std::uint8_t> screen(cellCount);
        std::vector<std::uint8_t> colorRam(cellCount);
        for (std::size_t cell = 0; cell < cellCount; ++cell) {
            screen[cell] = static_cast<std::uint8_t>(indexes[cell]);
            const std::uint8_t local = cellPalettes[cell][mode->multicolor ? 3U : 1U];
            colorRam[cell] = static_cast<std::uint8_t>(local
                | (mode->multicolor ? 0x08U : 0U));
        }
        tables.push_back({TargetTableRole::Pattern, std::move(patternBytes)});
        tables.push_back({TargetTableRole::TileMap, std::move(screen)});
        tables.push_back({TargetTableRole::Color, std::move(colorRam)});
    } else {
        std::vector<std::uint8_t> bitmap(cellCount * 8U);
        std::vector<std::uint8_t> screen(cellCount);
        std::vector<std::uint8_t> colorRam;
        if (mode->multicolor) colorRam.resize(cellCount);
        for (std::size_t cell = 0; cell < cellCount; ++cell) {
            const auto encoded = encodeCell(cells[cell], mode->multicolor);
            std::ranges::copy(encoded, bitmap.begin()
                + static_cast<std::ptrdiff_t>(cell * 8U));
            if (mode->multicolor) {
                screen[cell] = static_cast<std::uint8_t>(
                    (cellPalettes[cell][1] << 4U) | cellPalettes[cell][2]);
                colorRam[cell] = cellPalettes[cell][3];
            } else {
                screen[cell] = static_cast<std::uint8_t>(
                    (cellPalettes[cell][1] << 4U) | cellPalettes[cell][0]);
            }
        }
        tables.push_back({TargetTableRole::Framebuffer, std::move(bitmap)});
        tables.push_back({TargetTableRole::TileMap, std::move(screen)});
        if (mode->multicolor)
            tables.push_back({TargetTableRole::Color, std::move(colorRam)});
    }
    tables.push_back({TargetTableRole::DisplayRegisters,
        mode->vicII ? vicIIRegisters(settings.mode, global)
                    : vicRegisters(mode->multicolor, global)});

    std::vector<ConversionDiagnostic> diagnostics;
    diagnostics.push_back({DiagnosticSeverity::Information,
        mode->vicII ? "vic-ii-palette-reference" : "vic-palette-reference",
        "Preview RGB values are nominal; analog Commodore color output varies by chip revision and video standard."});
    if (patternsReduced) {
        diagnostics.push_back({DiagnosticSeverity::Warning,
            "commodore-character-pattern-limit",
            "The source required more than 256 character patterns; the closest reusable patterns were selected."});
    }

    TargetMemoryImage target{
        .profile = mode->vicII ? TargetProfileId::VicII : TargetProfileId::Vic,
        .mode = settings.mode,
        .palette = Palette::create(
            std::vector<RgbColor>(palette.begin(), palette.end())),
        .tables = std::move(tables),
    };
    return {.status = ConversionStatus::Succeeded,
            .preview = std::move(preview),
            .diagnostics = std::move(diagnostics),
            .target = std::move(target)};
}

} // namespace retrovdp::core
