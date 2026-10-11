#include "retrovdp/core/LegacyDisplayConverter.hpp"

#include "retrovdp/core/TargetProfile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace retrovdp::core {
namespace {

constexpr std::array<RgbColor, 16> pcPalette{{
    {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
    {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
    {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
    {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255},
}};
constexpr std::array<RgbColor, 16> applePalette{{
    {0,0,0}, {227,30,96}, {96,78,189}, {255,68,253},
    {0,163,96}, {156,156,156}, {20,207,253}, {208,195,255},
    {96,114,3}, {255,106,60}, {156,156,156}, {255,160,208},
    {20,245,60}, {208,221,141}, {114,255,208}, {255,255,255},
}};

ConversionResult failure(std::string code, std::string message)
{
    return {.status = ConversionStatus::Failed,
            .preview = std::nullopt,
            .diagnostics = {{DiagnosticSeverity::Error, std::move(code),
                             std::move(message)}},
            .target = std::nullopt,
            .generation = 0};
}

ConversionResult cancelled()
{
    return {.status = ConversionStatus::Cancelled,
            .preview = std::nullopt,
            .diagnostics = {},
            .target = std::nullopt,
            .generation = 0};
}

RgbColor pixel(const RgbImage& image, std::uint32_t x, std::uint32_t y)
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

unsigned distance(RgbColor a, RgbColor b)
{
    const int r = static_cast<int>(a.red) - b.red;
    const int g = static_cast<int>(a.green) - b.green;
    const int bl = static_cast<int>(a.blue) - b.blue;
    return static_cast<unsigned>(r * r + g * g + bl * bl);
}

std::uint8_t nearest(RgbColor color, std::span<const RgbColor> palette)
{
    std::size_t best = 0;
    unsigned bestDistance = std::numeric_limits<unsigned>::max();
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const unsigned candidate = distance(color, palette[i]);
        if (candidate < bestDistance) {
            bestDistance = candidate;
            best = i;
        }
    }
    return static_cast<std::uint8_t>(best);
}

std::vector<RgbColor> cpcHardwarePalette()
{
    std::vector<RgbColor> result;
    result.reserve(27);
    for (const std::uint8_t green : {0U, 128U, 255U})
        for (const std::uint8_t red : {0U, 128U, 255U})
            for (const std::uint8_t blue : {0U, 128U, 255U})
                result.push_back({red, green, blue});
    return result;
}

struct Surface {
    std::vector<std::uint8_t> indices;
    std::vector<RgbColor> colors;
    std::vector<std::uint8_t> hardwareEntries;
    RgbImage preview;
};

std::optional<Surface> indexedSurface(const RgbImage& source,
                                      std::span<const RgbColor> hardware,
                                      std::size_t colorCount,
                                      CancellationToken cancellation,
                                      const ConversionProgressCallback& progress)
{
    auto preview = RgbImage::createTightlyPacked(source.width(), source.height(),
                                                 source.pixelFormat());
    if (!preview || hardware.empty() || colorCount == 0) return std::nullopt;
    colorCount = std::min(colorCount, hardware.size());
    std::vector<std::size_t> histogram(hardware.size());
    for (std::uint32_t y = 0; y < source.height(); ++y)
        for (std::uint32_t x = 0; x < source.width(); ++x)
            ++histogram[nearest(pixel(source, x, y), hardware)];
    std::vector<std::uint8_t> entries(hardware.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
        entries[i] = static_cast<std::uint8_t>(i);
    std::stable_sort(entries.begin(), entries.end(), [&histogram](auto a, auto b) {
        return histogram[a] > histogram[b];
    });
    entries.resize(colorCount);
    std::vector<RgbColor> colors;
    for (const auto entry : entries) colors.push_back(hardware[entry]);
    std::vector<std::uint8_t> indices(
        static_cast<std::size_t>(source.width()) * source.height());
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        if (cancellation.isCancellationRequested()) return std::nullopt;
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const auto index = nearest(pixel(source, x, y), colors);
            indices[static_cast<std::size_t>(y) * source.width() + x] = index;
            setPixel(*preview, x, y, colors[index]);
        }
        if (progress) progress(*preview, y + 1U, source.height());
    }
    return Surface{std::move(indices), std::move(colors), std::move(entries),
                   std::move(*preview)};
}

std::vector<std::uint8_t> registers(ConversionMode mode,
                                    std::uint16_t width,
                                    std::uint16_t height)
{
    std::vector<std::uint8_t> bytes(16);
    bytes[0] = 'R'; bytes[1] = 'V'; bytes[2] = 'D'; bytes[3] = 'P';
    bytes[4] = static_cast<std::uint8_t>(mode);
    bytes[5] = static_cast<std::uint8_t>(width);
    bytes[6] = static_cast<std::uint8_t>(width >> 8U);
    bytes[7] = static_cast<std::uint8_t>(height);
    bytes[8] = static_cast<std::uint8_t>(height >> 8U);
    return bytes;
}

std::vector<std::uint8_t> cpcFramebuffer(const Surface& surface, unsigned bpp)
{
    std::vector<std::uint8_t> bytes(16384);
    const std::size_t pixelsPerByte = 8U / bpp;
    const std::size_t bytesPerLine = surface.preview.width() / pixelsPerByte;
    for (std::size_t y = 0; y < surface.preview.height(); ++y) {
        const std::size_t line = (y & 7U) * 0x800U + (y >> 3U) * 80U;
        for (std::size_t bx = 0; bx < bytesPerLine; ++bx) {
            std::uint8_t value = 0;
            if (bpp == 1) {
                for (std::size_t p = 0; p < 8; ++p)
                    value |= (surface.indices[y * surface.preview.width() + bx * 8U + p] & 1U)
                        << (7U - p);
            } else if (bpp == 2) {
                for (std::size_t p = 0; p < 4; ++p) {
                    const auto index = surface.indices[y * surface.preview.width() + bx * 4U + p];
                    value |= ((index >> 1U) & 1U) << (7U - p);
                    value |= (index & 1U) << (3U - p);
                }
            } else {
                const auto left = surface.indices[y * surface.preview.width() + bx * 2U];
                const auto right = surface.indices[y * surface.preview.width() + bx * 2U + 1U];
                value = static_cast<std::uint8_t>(((left & 1U) << 7U) | ((right & 1U) << 6U)
                    | ((left & 4U) << 3U) | ((right & 4U) << 2U)
                    | ((left & 2U) << 2U) | ((right & 2U) << 1U)
                    | ((left & 8U) >> 2U) | ((right & 8U) >> 3U));
            }
            bytes[line + bx] = value;
        }
    }
    return bytes;
}

ConversionResult convertCpc(const RgbImage& source, const ConversionSettings& settings,
                            CancellationToken cancellation,
                            const ConversionProgressCallback& progress)
{
    const unsigned bpp = settings.mode == ConversionMode::CpcMode0 ? 4U
        : settings.mode == ConversionMode::CpcMode1 ? 2U : 1U;
    const auto hardware = cpcHardwarePalette();
    auto surface = indexedSurface(source, hardware, 1U << bpp, cancellation, progress);
    if (!surface) return cancellation.isCancellationRequested()
        ? cancelled()
        : failure("cpc-conversion", "The CPC image could not be quantized.");
    std::vector<std::uint8_t> palette(17, 0);
    for (std::size_t i = 0; i < surface->hardwareEntries.size(); ++i)
        palette[i] = surface->hardwareEntries[i];
    palette[16] = 0;
    TargetMemoryImage target{.profile = TargetProfileId::AmstradCpc,
        .mode = settings.mode,
        .palette = Palette::create(surface->colors),
        .tables = {{TargetTableRole::Framebuffer, cpcFramebuffer(*surface, bpp)},
                   {TargetTableRole::Palette, std::move(palette)},
                   {TargetTableRole::DisplayRegisters,
                    registers(settings.mode, source.width(), source.height())}}};
    return {.status = ConversionStatus::Succeeded, .preview = std::move(surface->preview),
            .diagnostics = {}, .target = std::move(target), .generation = 0};
}

std::vector<std::uint8_t> textBuffer(const RgbImage& source, RgbImage& preview,
                                     std::span<const RgbColor> palette,
                                     std::size_t columns, std::size_t rows,
                                     std::size_t cellWidth, std::size_t cellHeight,
                                     bool attributes)
{
    std::vector<std::uint8_t> result(columns * rows * (attributes ? 2U : 1U));
    for (std::size_t cy = 0; cy < rows; ++cy) {
        for (std::size_t cx = 0; cx < columns; ++cx) {
            std::uint64_t r = 0, g = 0, b = 0;
            for (std::size_t y = 0; y < cellHeight; ++y)
                for (std::size_t x = 0; x < cellWidth; ++x) {
                    const auto color = pixel(source, static_cast<std::uint32_t>(cx * cellWidth + x),
                                             static_cast<std::uint32_t>(cy * cellHeight + y));
                    r += color.red; g += color.green; b += color.blue;
                }
            const auto count = cellWidth * cellHeight;
            const std::uint8_t colorIndex = nearest(
                {static_cast<std::uint8_t>(r / count), static_cast<std::uint8_t>(g / count),
                 static_cast<std::uint8_t>(b / count)}, palette);
            const std::size_t offset = cy * columns + cx;
            if (attributes) {
                result[offset * 2U] = 0xdbU;
                result[offset * 2U + 1U] = colorIndex;
            } else {
                result[offset] = colorIndex == 0 ? 0x20U : 0x7fU;
            }
            for (std::size_t y = 0; y < cellHeight; ++y)
                for (std::size_t x = 0; x < cellWidth; ++x)
                    setPixel(preview, static_cast<std::uint32_t>(cx * cellWidth + x),
                             static_cast<std::uint32_t>(cy * cellHeight + y), palette[colorIndex]);
        }
    }
    return result;
}

ConversionResult convertPc(const RgbImage& source, const ConversionSettings& settings,
                           bool ega, CancellationToken cancellation,
                           const ConversionProgressCallback& progress)
{
    const bool text = settings.mode == ConversionMode::CgaText40x25
        || settings.mode == ConversionMode::CgaText80x25
        || settings.mode == ConversionMode::EgaText;
    auto preview = RgbImage::createTightlyPacked(source.width(), source.height(), source.pixelFormat());
    if (!preview) return failure("pc-display-allocation", "The PC display preview could not be allocated.");
    std::vector<std::uint8_t> framebuffer;
    std::vector<RgbColor> colors(pcPalette.begin(), pcPalette.end());
    if (text) {
        const std::size_t columns = settings.mode == ConversionMode::CgaText40x25 ? 40U : 80U;
        const std::size_t cellHeight = ega ? 14U : 8U;
        framebuffer = textBuffer(source, *preview, colors, columns, 25U,
                                 source.width() / columns, cellHeight, true);
    } else {
        std::span<const RgbColor> modePalette = colors;
        std::array<RgbColor, 4> cga4{{pcPalette[0], pcPalette[3], pcPalette[5], pcPalette[15]}};
        std::array<RgbColor, 2> mono{{pcPalette[0], pcPalette[15]}};
        if (!ega && settings.mode == ConversionMode::CgaGraphics320x200) modePalette = cga4;
        if (!ega && settings.mode == ConversionMode::CgaGraphics640x200) modePalette = mono;
        auto surface = indexedSurface(source, modePalette, modePalette.size(), cancellation, progress);
        if (!surface) return cancellation.isCancellationRequested()
            ? cancelled()
            : failure("pc-display-conversion", "The PC graphics image could not be quantized.");
        *preview = std::move(surface->preview);
        const std::size_t bytesPerLine = source.width() / (ega ? 8U
            : settings.mode == ConversionMode::CgaGraphics320x200 ? 4U : 8U);
        if (ega) {
            const std::size_t planeSize = bytesPerLine * source.height();
            framebuffer.assign(planeSize * 4U, 0);
            for (std::size_t y = 0; y < source.height(); ++y)
                for (std::size_t x = 0; x < source.width(); ++x) {
                    const auto index = surface->indices[y * source.width() + x];
                    for (std::size_t plane = 0; plane < 4; ++plane)
                        if ((index & (1U << plane)) != 0U)
                            framebuffer[plane * planeSize + y * bytesPerLine + x / 8U]
                                |= static_cast<std::uint8_t>(0x80U >> (x & 7U));
                }
        } else {
            framebuffer.assign(16384, 0);
            const bool low = settings.mode == ConversionMode::CgaGraphics320x200;
            for (std::size_t y = 0; y < source.height(); ++y) {
                const std::size_t line = (y & 1U) * 0x2000U + (y >> 1U) * bytesPerLine;
                for (std::size_t x = 0; x < source.width(); ++x) {
                    const auto index = surface->indices[y * source.width() + x];
                    if (low) framebuffer[line + x / 4U] |= index << (6U - 2U * (x & 3U));
                    else framebuffer[line + x / 8U] |= index << (7U - (x & 7U));
                }
            }
        }
    }
    if (progress) progress(*preview, source.height(), source.height());
    std::vector<std::uint8_t> paletteBytes(ega ? 16U : 1U);
    if (ega) for (std::size_t i = 0; i < 16; ++i) paletteBytes[i] = static_cast<std::uint8_t>(i);
    else if (settings.mode == ConversionMode::CgaGraphics320x200)
        paletteBytes[0] = 0x30U;
    else if (settings.mode == ConversionMode::CgaGraphics640x200)
        paletteBytes[0] = 0x0fU;
    TargetMemoryImage target{.profile = ega ? TargetProfileId::IbmEga : TargetProfileId::IbmCga,
        .mode = settings.mode, .palette = Palette::create(std::move(colors)),
        .tables = {{TargetTableRole::Framebuffer, std::move(framebuffer)},
                   {TargetTableRole::Palette, std::move(paletteBytes)},
                   {TargetTableRole::DisplayRegisters,
                    registers(settings.mode, source.width(), source.height())}}};
    return {.status = ConversionStatus::Succeeded, .preview = std::move(*preview),
            .diagnostics = {}, .target = std::move(target), .generation = 0};
}

std::size_t appleTextOffset(std::size_t row, std::size_t column)
{
    return (row & 7U) * 0x80U + (row >> 3U) * 40U + column;
}

std::size_t appleHiresOffset(std::size_t y, std::size_t byteX)
{
    return (y & 7U) * 0x400U + ((y >> 3U) & 7U) * 0x80U
        + (y >> 6U) * 40U + byteX;
}

ConversionResult convertApple(const RgbImage& source, const ConversionSettings& settings,
                              CancellationToken cancellation,
                              const ConversionProgressCallback& progress)
{
    const bool text = settings.mode == ConversionMode::AppleIIeText;
    const bool lores = settings.mode == ConversionMode::AppleIIeLores;
    const bool doubleHires = settings.mode == ConversionMode::AppleIIeDoubleHires;
    auto preview = RgbImage::createTightlyPacked(source.width(), source.height(), source.pixelFormat());
    if (!preview) return failure("apple-iie-allocation", "The Apple IIe preview could not be allocated.");
    std::vector<std::uint8_t> bytes(text || lores ? 1024U : doubleHires ? 16384U : 8192U);
    if (text) {
        constexpr std::array<RgbColor, 2> monoText{{{0,0,0}, {255,255,255}}};
        auto linear = textBuffer(source, *preview, monoText,
                                 40, 24, 7, 8, false);
        for (std::size_t row = 0; row < 24; ++row)
            for (std::size_t col = 0; col < 40; ++col)
                bytes[appleTextOffset(row, col)] = linear[row * 40U + col] == 0x20U ? 0xa0U : 0xc0U;
    } else if (lores) {
        auto surface = indexedSurface(source, applePalette, 16, cancellation, progress);
        if (!surface) return cancellation.isCancellationRequested()
            ? cancelled()
            : failure("apple-iie-conversion", "The Apple IIe lo-res image could not be quantized.");
        *preview = std::move(surface->preview);
        for (std::size_t row = 0; row < 24; ++row)
            for (std::size_t col = 0; col < 40; ++col)
                bytes[appleTextOffset(row, col)] = static_cast<std::uint8_t>(
                    surface->indices[(row * 2U) * 40U + col]
                    | (surface->indices[(row * 2U + 1U) * 40U + col] << 4U));
    } else {
        std::array<RgbColor, 2> mono{{{0,0,0}, {255,255,255}}};
        auto surface = indexedSurface(source, mono, 2, cancellation, progress);
        if (!surface) return cancellation.isCancellationRequested()
            ? cancelled()
            : failure("apple-iie-conversion", "The Apple IIe hi-res image could not be quantized.");
        *preview = std::move(surface->preview);
        for (std::size_t y = 0; y < 192; ++y) {
            const std::size_t byteCount = doubleHires ? 80U : 40U;
            for (std::size_t bx = 0; bx < byteCount; ++bx) {
                std::uint8_t value = 0;
                for (std::size_t bit = 0; bit < 7; ++bit)
                    value |= surface->indices[y * source.width() + bx * 7U + bit] << bit;
                if (doubleHires) {
                    const std::size_t bank = bx & 1U;
                    bytes[bank * 8192U + appleHiresOffset(y, bx / 2U)] = value;
                } else bytes[appleHiresOffset(y, bx)] = value;
            }
        }
    }
    if (progress) progress(*preview, source.height(), source.height());
    TargetMemoryImage target{.profile = TargetProfileId::AppleIIe, .mode = settings.mode,
        .palette = Palette::create(std::vector<RgbColor>(applePalette.begin(), applePalette.end())),
        .tables = {{TargetTableRole::Framebuffer, std::move(bytes)},
                   {TargetTableRole::DisplayRegisters,
                    registers(settings.mode, source.width(), source.height())}}};
    return {.status = ConversionStatus::Succeeded, .preview = std::move(*preview),
            .diagnostics = {}, .target = std::move(target), .generation = 0};
}

ConversionResult convertCharacterDisplay(const RgbImage& source,
                                         const ConversionSettings& settings,
                                         CancellationToken cancellation,
                                         const ConversionProgressCallback& progress)
{
    const bool zx = settings.mode == ConversionMode::Zx81Text32x24
        || settings.mode == ConversionMode::Zx81Semigraphics64x48;
    const bool semigraphics = settings.mode == ConversionMode::Zx81Semigraphics64x48
        || settings.mode == ConversionMode::Trs80Semigraphics128x48;
    std::array<RgbColor, 2> mono{{{0,0,0}, {255,255,255}}};
    auto surface = indexedSurface(source, mono, 2, cancellation, progress);
    if (!surface) return cancellation.isCancellationRequested()
        ? cancelled()
        : failure("character-display-conversion", "The character display could not be quantized.");
    std::vector<std::uint8_t> cells(zx ? 793U : 1024U, 0x20U);
    if (zx) {
        cells[0] = 0x76U;
        for (std::size_t row = 0; row < 24; ++row) {
            for (std::size_t col = 0; col < 32; ++col) {
                std::uint8_t code = 0;
                if (semigraphics) {
                    for (std::size_t sy = 0; sy < 2; ++sy)
                        for (std::size_t sx = 0; sx < 2; ++sx)
                            if (surface->indices[(row * 2U + sy) * 64U + col * 2U + sx] == 0)
                                code |= static_cast<std::uint8_t>(1U << (sy * 2U + sx));
                    code = code < 8U ? code
                        : static_cast<std::uint8_t>(0x80U | ((code ^ 0x0fU) & 7U));
                } else {
                    std::size_t dark = 0;
                    for (std::size_t y = 0; y < 8; ++y)
                        for (std::size_t x = 0; x < 8; ++x)
                            dark += surface->indices[(row * 8U + y) * 256U + col * 8U + x] == 0;
                    code = dark >= 32U ? 0x80U : 0U;
                }
                cells[1U + row * 33U + col] = code;
            }
            cells[1U + row * 33U + 32U] = 0x76U;
        }
    } else if (semigraphics) {
        for (std::size_t row = 0; row < 16; ++row)
            for (std::size_t col = 0; col < 64; ++col) {
                std::uint8_t code = 0x80U;
                for (std::size_t sy = 0; sy < 3; ++sy)
                    for (std::size_t sx = 0; sx < 2; ++sx)
                        if (surface->indices[(row * 3U + sy) * 128U + col * 2U + sx] != 0)
                            code |= static_cast<std::uint8_t>(1U << (sy * 2U + sx));
                cells[row * 64U + col] = code;
            }
    } else {
        for (std::size_t row = 0; row < 16; ++row)
            for (std::size_t col = 0; col < 64; ++col) {
                std::size_t light = 0;
                for (std::size_t y = 0; y < 12; ++y)
                    for (std::size_t x = 0; x < 8; ++x)
                        light += surface->indices[(row * 12U + y) * 512U + col * 8U + x];
                cells[row * 64U + col] = light >= 48U ? 0x7fU : 0x20U;
            }
    }
    TargetMemoryImage target{.profile = zx ? TargetProfileId::TimexTs1000
                                           : TargetProfileId::Trs80Model1_3,
        .mode = settings.mode, .palette = Palette::create(std::vector<RgbColor>(mono.begin(), mono.end())),
        .tables = {{TargetTableRole::Framebuffer, std::move(cells)},
                   {TargetTableRole::DisplayRegisters,
                    registers(settings.mode, source.width(), source.height())}}};
    return {.status = ConversionStatus::Succeeded, .preview = std::move(surface->preview),
            .diagnostics = {}, .target = std::move(target), .generation = 0};
}

} // namespace

ConversionResult convertLegacyDisplay(const RgbImage& source,
                                      const ConversionSettings& settings,
                                      CancellationToken cancellation,
                                      ConversionProgressCallback progress)
{
    const auto& descriptor = displayMode(settings.mode);
    if (source.width() != descriptor.geometry.width || source.height() != descriptor.geometry.height)
        return failure("legacy-display-source-size", "The prepared source does not match the selected display mode.");
    switch (settings.targetProfile) {
    case TargetProfileId::AmstradCpc:
        return convertCpc(source, settings, cancellation, progress);
    case TargetProfileId::IbmCga:
        return convertPc(source, settings, false, cancellation, progress);
    case TargetProfileId::IbmEga:
        return convertPc(source, settings, true, cancellation, progress);
    case TargetProfileId::AppleIIe:
        return convertApple(source, settings, cancellation, progress);
    case TargetProfileId::TimexTs1000:
    case TargetProfileId::Trs80Model1_3:
        return convertCharacterDisplay(source, settings, cancellation, progress);
    default:
        return failure("unsupported-legacy-display", "The selected target is not a display-adapter target.");
    }
}

} // namespace retrovdp::core
