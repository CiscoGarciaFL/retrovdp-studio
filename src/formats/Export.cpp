#include "retrovdp/formats/Export.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace retrovdp::formats {
namespace {

using core::ConversionMode;
using core::TargetMemoryImage;
using core::TargetMemoryTable;
using core::TargetTableRole;

constexpr std::size_t graphicsTableSize = 6144U;
constexpr std::size_t colecoMaximumSize = 32U * 1024U;

core::FormatId makeFormatId(std::string_view value)
{
    auto id = core::FormatId::create(value);
    if (!id) throw std::logic_error("invalid export format identifier");
    return std::move(*id);
}

GeneratedFileManifest failure(ExportFormat format, ExportError error, std::string message)
{
    GeneratedFileManifest result;
    result.format = format;
    result.error = error;
    result.message = std::move(message);
    return result;
}

GeneratedFileManifest success(ExportFormat format, GeneratedFile file)
{
    GeneratedFileManifest result;
    result.format = format;
    result.files.push_back(std::move(file));
    return result;
}

bool validBaseName(std::string_view name)
{
    return !name.empty() && name != "." && name != ".."
        && name.find('/') == std::string_view::npos
        && name.find('\\') == std::string_view::npos
        && name.find('\0') == std::string_view::npos;
}

std::string asciiUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return value;
}

bool endsWithCaseInsensitive(std::string_view value, std::string_view suffix)
{
    if (value.size() < suffix.size()) return false;
    const std::size_t offset = value.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[offset + index]))
            != std::tolower(static_cast<unsigned char>(suffix[index]))) {
            return false;
        }
    }
    return true;
}

std::string withExtension(std::string baseName, std::string_view extension)
{
    if (!endsWithCaseInsensitive(baseName, extension)) baseName.append(extension);
    return baseName;
}

bool isYamahaBitmapMode(ConversionMode mode)
{
    return mode == ConversionMode::Screen5V9938 || mode == ConversionMode::Screen6V9938
        || mode == ConversionMode::Screen7V9938 || mode == ConversionMode::Screen8V9938
        || mode == ConversionMode::Screen10V9958 || mode == ConversionMode::Screen11V9958
        || mode == ConversionMode::Screen12V9958;
}

bool isSegaSmsMode(ConversionMode mode)
{
    return mode == ConversionMode::Mode4Sms192
        || mode == ConversionMode::Mode4Sms224
        || mode == ConversionMode::Mode4Sms240;
}

bool isSegaGenesisMode(ConversionMode mode)
{
    return mode == ConversionMode::Mode5GenesisH32
        || mode == ConversionMode::Mode5GenesisH40
        || mode == ConversionMode::Mode5GenesisH32Pal
        || mode == ConversionMode::Mode5GenesisH40Pal;
}

bool isHuC6270Mode(ConversionMode mode)
{
    return mode == ConversionMode::HuC6270Background256
        || mode == ConversionMode::HuC6270Background320;
}

bool isCommodoreMode(ConversionMode mode)
{
    return mode == ConversionMode::VicIIHiresCharacter
        || mode == ConversionMode::VicIIMulticolorCharacter
        || mode == ConversionMode::VicIIHiresBitmap
        || mode == ConversionMode::VicIIMulticolorBitmap
        || mode == ConversionMode::VicHiresCharacter
        || mode == ConversionMode::VicMulticolorCharacter;
}

const TargetMemoryTable* table(const TargetMemoryImage& image, TargetTableRole role)
{
    const auto found = std::ranges::find(image.tables, role, &TargetMemoryTable::role);
    return found == image.tables.end() ? nullptr : &*found;
}

const TargetMemoryTable* patternTable(const TargetMemoryImage& image)
{
    if (const auto* result = table(image, TargetTableRole::Pattern)) return result;
    if (const auto* result = table(image, TargetTableRole::FixedPattern)) return result;
    if (const auto* result = table(image, TargetTableRole::Multicolor)) return result;
    if (const auto* result = table(image, TargetTableRole::MulticolorFrame1)) return result;
    return nullptr;
}

const TargetMemoryTable* colorTable(const TargetMemoryImage& image)
{
    if (const auto* result = table(image, TargetTableRole::Color)) return result;
    return table(image, TargetTableRole::MulticolorFrame2);
}

std::vector<std::uint8_t> effectiveColor(const TargetMemoryImage& image)
{
    if (const auto* source = colorTable(image)) return source->bytes;
    return std::vector<std::uint8_t>(graphicsTableSize, 0x1fU);
}

char tableSuffix(TargetTableRole role, ConversionMode mode)
{
    switch (role) {
    case TargetTableRole::Pattern:
    case TargetTableRole::FixedPattern:
    case TargetTableRole::MulticolorFrame1: return 'P';
    case TargetTableRole::Multicolor:
        return mode == ConversionMode::HalfMulticolor9918 ? 'M' : 'P';
    case TargetTableRole::Color:
    case TargetTableRole::MulticolorFrame2: return 'C';
    case TargetTableRole::Palette:
    case TargetTableRole::ScanlinePalettes: return 'M';
    case TargetTableRole::Framebuffer: return 'V';
    case TargetTableRole::TileMap: return 'N';
    case TargetTableRole::DisplayRegisters: return 'R';
    }
    return 'M';
}

bool isPaletteTable(TargetTableRole role)
{
    return role == TargetTableRole::Palette || role == TargetTableRole::ScanlinePalettes;
}

std::array<std::uint8_t, 128> tiFilesHeader(std::size_t size)
{
    std::array<std::uint8_t, 128> result{};
    constexpr std::array<std::uint8_t, 8> signature{7, 'T', 'I', 'F', 'I', 'L', 'E', 'S'};
    std::ranges::copy(signature, result.begin());
    result[9] = static_cast<std::uint8_t>((size + 255U) / 256U);
    result[10] = 1U;
    result[12] = static_cast<std::uint8_t>(size % 256U);
    return result;
}

std::array<std::uint8_t, 128> v9t9Header(std::string_view name, std::size_t size)
{
    std::array<std::uint8_t, 128> result{};
    std::fill_n(result.begin(), 10, static_cast<std::uint8_t>(' '));
    for (std::size_t index = 0; index < std::min<std::size_t>(10, name.size()); ++index) {
        const unsigned char value = static_cast<unsigned char>(name[index]);
        result[index] = value >= 0x21U && value <= 0x7eU ? value : '_';
    }
    result[12] = 1U;
    result[15] = static_cast<std::uint8_t>((size + 255U) / 256U);
    return result;
}

void append(std::vector<std::uint8_t>& output, std::span<const std::uint8_t> bytes)
{
    output.insert(output.end(), bytes.begin(), bytes.end());
}

template<std::size_t Size>
void append(std::vector<std::uint8_t>& output, const std::array<std::uint8_t, Size>& bytes)
{
    append(output, std::span<const std::uint8_t>(bytes));
}

void normalizeForRle(std::vector<TargetMemoryTable>& tables)
{
    auto pattern = std::ranges::find_if(tables, [](const TargetMemoryTable& value) {
        return value.role == TargetTableRole::Pattern
            || value.role == TargetTableRole::FixedPattern;
    });
    const auto color = std::ranges::find(tables, TargetTableRole::Color, &TargetMemoryTable::role);
    if (pattern == tables.end() || color == tables.end()
        || pattern->bytes.size() != color->bytes.size()) {
        return;
    }

    for (std::size_t index = 1; index < color->bytes.size(); ++index) {
        const std::uint8_t current = color->bytes[index];
        const std::uint8_t swapped = static_cast<std::uint8_t>((current >> 4U) | (current << 4U));
        if (color->bytes[index - 1] == swapped) {
            color->bytes[index] = swapped;
            pattern->bytes[index] = static_cast<std::uint8_t>(~pattern->bytes[index]);
        }
        if (pattern->bytes[index - 1] == 0xffU && pattern->bytes[index] == 0x00U) {
            pattern->bytes[index] = 0xffU;
            const std::uint8_t low = color->bytes[index] & 0x0fU;
            color->bytes[index] = static_cast<std::uint8_t>(low | (low << 4U));
        }
        if (pattern->bytes[index] == 0xffU && pattern->bytes[index - 1] == 0x00U) {
            pattern->bytes[index] = 0x00U;
            const std::uint8_t high = color->bytes[index] & 0xf0U;
            color->bytes[index] = static_cast<std::uint8_t>(high | (high >> 4U));
        }
    }
}

GeneratedFileManifest tableFiles(const ExportRequest& request)
{
    GeneratedFileManifest result;
    result.format = request.format;
    std::vector<TargetMemoryTable> tables = request.target->tables;
    if (request.format == ExportFormat::Rle) normalizeForRle(tables);
    const std::string fileBase = request.format == ExportFormat::V9t9
        ? request.baseName.substr(0, 6) : request.baseName;

    std::size_t encodedTotal = 0;
    std::size_t rawTotal = 0;
    for (const auto& source : tables) {
        const char suffix = tableSuffix(source.role, request.target->mode);
        const std::string upperBase = asciiUpper(fileBase);
        std::string fileName = upperBase + ".TIA" + suffix;
        if (request.format == ExportFormat::Raw
            && isYamahaBitmapMode(request.target->mode)) {
            if (source.role == TargetTableRole::Framebuffer) fileName = upperBase + ".VRAM";
            else if (source.role == TargetTableRole::Palette) fileName = upperBase + ".PAL";
            else if (source.role == TargetTableRole::DisplayRegisters)
                fileName = upperBase + ".REG";
        } else if (request.format == ExportFormat::Raw
                   && (isSegaSmsMode(request.target->mode)
                       || isSegaGenesisMode(request.target->mode))) {
            if (source.role == TargetTableRole::Pattern)
                fileName = upperBase + ".TILES";
            else if (source.role == TargetTableRole::TileMap)
                fileName = upperBase + ".MAP";
            else if (source.role == TargetTableRole::Palette)
                fileName = upperBase + ".PAL";
            else if (source.role == TargetTableRole::DisplayRegisters)
                fileName = upperBase + ".REG";
        } else if (request.format == ExportFormat::Raw
                   && isHuC6270Mode(request.target->mode)) {
            if (source.role == TargetTableRole::Pattern)
                fileName = upperBase + ".TILES";
            else if (source.role == TargetTableRole::TileMap)
                fileName = upperBase + ".BAT";
            else if (source.role == TargetTableRole::Palette)
                fileName = upperBase + ".PAL";
            else if (source.role == TargetTableRole::DisplayRegisters)
                fileName = upperBase + ".REG";
        } else if (request.format == ExportFormat::Raw
                   && isCommodoreMode(request.target->mode)) {
            if (source.role == TargetTableRole::Pattern)
                fileName = upperBase + ".CHR";
            else if (source.role == TargetTableRole::Framebuffer)
                fileName = upperBase + ".BITMAP";
            else if (source.role == TargetTableRole::TileMap)
                fileName = upperBase + ".SCR";
            else if (source.role == TargetTableRole::Color)
                fileName = upperBase + ".COL";
            else if (source.role == TargetTableRole::DisplayRegisters)
                fileName = upperBase + ".REG";
        }
        GeneratedFile file{std::move(fileName), {}};
        rawTotal += source.bytes.size();

        if (request.format == ExportFormat::TiFiles) {
            append(file.bytes, tiFilesHeader(source.bytes.size()));
            append(file.bytes, source.bytes);
        } else if (request.format == ExportFormat::V9t9) {
            append(file.bytes,
                   v9t9Header(fileBase + '_' + suffix, source.bytes.size()));
            append(file.bytes, source.bytes);
        } else if (request.format == ExportFormat::Rle && !isPaletteTable(source.role)) {
            file.bytes = encodeLegacyRle(source.bytes);
            encodedTotal += file.bytes.size();
        } else {
            file.bytes = source.bytes;
            encodedTotal += file.bytes.size();
        }
        result.files.push_back(std::move(file));
    }
    if (request.format == ExportFormat::Rle && encodedTotal > rawTotal) {
        result.warnings.emplace_back("RLE output is larger than the uncompressed tables.");
    }
    return result;
}

GeneratedFileManifest msxScreen2(const ExportRequest& request)
{
    const auto* pattern = patternTable(*request.target);
    if (pattern == nullptr || pattern->bytes.size() != graphicsTableSize) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "MSX Screen 2 requires a 6144-byte pattern table.");
    }
    const auto color = effectiveColor(*request.target);
    GeneratedFile file{withExtension(request.baseName, ".sc2"), {}};
    file.bytes.reserve(14343U);
    constexpr std::array<std::uint8_t, 7> header{0xfe, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00};
    append(file.bytes, header);
    append(file.bytes, pattern->bytes);
    for (unsigned copy = 0; copy < 3; ++copy) {
        for (unsigned value = 0; value < 256; ++value) {
            file.bytes.push_back(static_cast<std::uint8_t>(value));
        }
    }
    for (unsigned index = 0; index < 320; ++index) {
        file.bytes.insert(file.bytes.end(), {0xd1, 0x00, 0x00, 0x00});
    }
    append(file.bytes, color);
    return success(request.format, std::move(file));
}

GeneratedFileManifest cvPaint(const ExportRequest& request)
{
    GeneratedFile file{withExtension(request.baseName, ".pc"), {}};
    const auto* pattern = patternTable(*request.target);
    if (pattern == nullptr || pattern->bytes.size() != graphicsTableSize) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "Coleco CVPaint requires a 6144-byte pattern table.");
    }
    append(file.bytes, pattern->bytes);
    append(file.bytes, effectiveColor(*request.target));
    return success(request.format, std::move(file));
}

GeneratedFileManifest powerPaint(const ExportRequest& request)
{
    const auto* pattern = patternTable(*request.target);
    if (pattern == nullptr || pattern->bytes.size() < graphicsTableSize) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "Adam PowerPaint requires a 6144-byte pattern table.");
    }
    const auto color = effectiveColor(*request.target);
    GeneratedFile file{withExtension(request.baseName, ".pp"), {}};
    file.bytes.reserve(10240U);
    for (std::size_t offset = 0; offset < 20U * 256U; offset += 256U) {
        file.bytes.insert(file.bytes.end(), 16U, 0x00U);
        append(file.bytes, std::span(pattern->bytes).subspan(offset, 240U));
    }
    for (std::size_t offset = 0; offset < 20U * 256U; offset += 256U) {
        file.bytes.insert(file.bytes.end(), 16U, 0xf1U);
        append(file.bytes, std::span(color).subspan(offset, 240U));
    }
    return success(request.format, std::move(file));
}

GeneratedFileManifest adamHgr(const ExportRequest& request)
{
    const auto* pattern = patternTable(*request.target);
    if (pattern == nullptr || pattern->bytes.size() < graphicsTableSize) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "Adam HGR requires a 6144-byte pattern table.");
    }
    const auto color = effectiveColor(*request.target);
    GeneratedFile file{request.baseName, {}};
    if (!endsWithCaseInsensitive(file.fileName, ".hgr")
        && !endsWithCaseInsensitive(file.fileName, ".hgrh")) {
        file.fileName += ".HGRH";
    }
    constexpr std::array<std::uint8_t, 21> header{1, 0, 2, 0x6c, 0x6b};
    file.bytes.reserve(10261U);
    append(file.bytes, header);
    append(file.bytes, std::span(color).first(5120U));
    append(file.bytes, std::span(pattern->bytes).first(5120U));
    return success(request.format, std::move(file));
}

GeneratedFileManifest colecoRom(const ExportRequest& request)
{
    const auto& loader = request.loaders.colecoVisionRom;
    if (loader.empty()) {
        return failure(request.format, ExportError::MissingLoaderTemplate,
                       "ColecoVision ROM export requires a loader template.");
    }
    if (loader.size() < 0x100U || loader.size() >= colecoMaximumSize) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The ColecoVision loader template is invalid.");
    }
    const auto* pattern = patternTable(*request.target);
    if (pattern == nullptr || pattern->bytes.size() != graphicsTableSize) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "ColecoVision ROM requires a 6144-byte pattern table.");
    }
    const auto color = effectiveColor(*request.target);
    if (loader.size() + pattern->bytes.size() + color.size() + 1U > colecoMaximumSize) {
        return failure(request.format, ExportError::OutputTooLarge,
                       "The loader and image tables exceed the 32 KiB cartridge limit.");
    }

    GeneratedFile file{withExtension(request.baseName, ".rom"),
                       std::vector<std::uint8_t>(loader.begin(), loader.end())};
    std::copy_n(file.bytes.begin() + 0x2d, 13, file.bytes.begin() + 0xf0);
    file.bytes[0x0a] = 0xf0U;
    file.bytes[0xff] = 0U;
    constexpr std::array<std::uint8_t, 16> tag{
        'C', 'o', 'n', 'v', 'e', 'r', 't', '9', '9', '1', '8', 'T', 'u', 'r', 's', 'i'};
    std::ranges::copy(tag, file.bytes.begin() + 0xc0);

    const std::uint16_t patternAddress = static_cast<std::uint16_t>(0x8000U + file.bytes.size());
    file.bytes[0xe2] = static_cast<std::uint8_t>(patternAddress & 0xffU);
    file.bytes[0xe3] = static_cast<std::uint8_t>(patternAddress >> 8U);
    append(file.bytes, pattern->bytes);
    const std::uint16_t colorAddress = static_cast<std::uint16_t>(0x8000U + file.bytes.size());
    file.bytes[0xe0] = static_cast<std::uint8_t>(colorAddress & 0xffU);
    file.bytes[0xe1] = static_cast<std::uint8_t>(colorAddress >> 8U);
    append(file.bytes, color);
    file.bytes.push_back(0U); // Preserve the original writer's trailing byte.
    return success(request.format, std::move(file));
}

std::optional<std::size_t> updateProgram(std::span<const std::uint8_t> source,
                                         std::vector<std::uint8_t>& target,
                                         std::size_t offset)
{
    for (const std::uint8_t value : source) {
        if (offset + 1U < target.size() && target[offset] == 0xffU
            && target[offset + 1U] == 0xfeU) {
            offset += 2U;
        }
        if (offset >= target.size()) return std::nullopt;
        target[offset++] = value;
    }
    return offset;
}

bool patch16(std::vector<std::uint8_t>& bytes,
             std::size_t offset,
             std::uint16_t expected,
             std::uint16_t value)
{
    if (offset + 1U >= bytes.size()
        || bytes[offset] != static_cast<std::uint8_t>(expected >> 8U)
        || bytes[offset + 1U] != static_cast<std::uint8_t>(expected & 0xffU)) {
        return false;
    }
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
    return true;
}

GeneratedFileManifest extendedBasic(const ExportRequest& request, bool compressed)
{
    const auto& loader = request.loaders.extendedBasicProgram;
    if (loader.empty()) {
        return failure(request.format, ExportError::MissingLoaderTemplate,
                       "Extended BASIC export requires a program loader template.");
    }
    if (loader.size() <= 0x34c3U) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC loader template is too small.");
    }
    const auto* patternSource = patternTable(*request.target);
    if (patternSource == nullptr || patternSource->bytes.size() != graphicsTableSize) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "Extended BASIC requires a 6144-byte pattern table.");
    }
    // The original single-file XB writer did not run the paired table
    // normalization used by the multi-file RLE writer. Preserve that proven
    // distinction because it changes both encoded sizes and relocation.
    const auto* pattern = patternSource;
    auto color = effectiveColor(*request.target);
    std::vector<std::uint8_t> work(loader.begin(), loader.end());

    if (!compressed) {
        if (work[0x3fe] != 0xcdU || work[0x3ff] != 0xe6U
            || work[0x1c2e] != 0xe5U || work[0x1c2f] != 0xe6U
            || !updateProgram(pattern->bytes, work, 0x3fe)
            || !updateProgram(color, work, 0x1c2e)) {
            return failure(request.format, ExportError::InvalidLoaderTemplate,
                           "The Extended BASIC loader patch points are invalid.");
        }
        GeneratedFile file{request.baseName, std::move(work)};
        return success(request.format, std::move(file));
    }

    const auto encodedPattern = encodeLegacyRle(pattern->bytes);
    const auto encodedColor = encodeLegacyRle(color);
    if (encodedPattern.size() + encodedColor.size()
        > pattern->bytes.size() + color.size()) {
        return failure(request.format, ExportError::OutputTooLarge,
                       "The RLE tables do not fit in the Extended BASIC loader.");
    }
    const int colorAddressEstimate = 0xfde6
        - static_cast<int>((encodedColor.size() + 1U) & ~std::size_t{1});
    const int patternAddressEstimate = colorAddressEstimate
        - static_cast<int>((encodedPattern.size() + 1U) & ~std::size_t{1});
    int relocation = patternAddressEstimate - 0xcde6;
    relocation = ((relocation - 253) / 254) * 254;
    const int patternAddress = 0xcde6 + relocation;
    const int colorAddress = patternAddress
        + static_cast<int>((encodedPattern.size() + 1U) & ~std::size_t{1});
    if (relocation < 0 || patternAddress < 0 || colorAddress > 0xffff) {
        return failure(request.format, ExportError::OutputTooLarge,
                       "The RLE tables cannot be placed in the Extended BASIC loader.");
    }

    auto cursor = updateProgram(encodedPattern, work, 0x3fe);
    if (!cursor) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC pattern patch point is invalid.");
    }
    if ((encodedPattern.size() & 1U) != 0U) {
        cursor = updateProgram(std::span(encodedPattern).first(1), work, *cursor);
    }
    if (!cursor || !(cursor = updateProgram(encodedColor, work, *cursor))) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC color patch point is invalid.");
    }
    if ((encodedColor.size() & 1U) != 0U
        && !(cursor = updateProgram(std::span(encodedColor).first(1), work, *cursor))) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC color padding is invalid.");
    }

    const int oldLineStart = static_cast<int>(work[0x83]) * 256 + work[0x84];
    const int oldLineEnd = static_cast<int>(work[0x85]) * 256 + work[0x86];
    const int lineSize = oldLineEnd - oldLineStart;
    const int newLineStart = oldLineStart + relocation;
    const int newLineEnd = oldLineEnd + relocation;
    if (lineSize < 0 || 0x181U + static_cast<std::size_t>(lineSize) > work.size()) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC line table is invalid.");
    }
    work[0x83] = static_cast<std::uint8_t>(newLineStart >> 8);
    work[0x84] = static_cast<std::uint8_t>(newLineStart);
    work[0x85] = static_cast<std::uint8_t>(newLineEnd >> 8);
    work[0x86] = static_cast<std::uint8_t>(newLineEnd);
    const int lineXor = newLineEnd ^ newLineStart;
    work[0x87] = static_cast<std::uint8_t>(lineXor >> 8);
    work[0x88] = static_cast<std::uint8_t>(lineXor);
    for (std::size_t index = 0x181U; index < 0x181U + static_cast<std::size_t>(lineSize);
         index += 4U) {
        const int address = static_cast<int>(work[index + 2U]) * 256
            + work[index + 3U] + relocation;
        work[index + 2U] = static_cast<std::uint8_t>(address >> 8);
        work[index + 3U] = static_cast<std::uint8_t>(address);
    }

    constexpr std::string_view hex = "0123456789ABCDEF";
    const auto patchHex = [&](std::size_t offset, int value) {
        for (int shift = 12; shift >= 0; shift -= 4) {
            work[offset++] = static_cast<std::uint8_t>(hex[(value >> shift) & 0x0f]);
        }
    };
    patchHex(0x352, patternAddress);
    patchHex(0x362, colorAddress);
    if (!patch16(work, 0x34a6, 0xcde6, static_cast<std::uint16_t>(patternAddress))
        || !patch16(work, 0x34b6, 0xe5e6, static_cast<std::uint16_t>(colorAddress))
        || !patch16(work, 0x34b2, 0xfeda, 0xfeaa)
        || !patch16(work, 0x34c2, 0xfeda, 0xfeaa)) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC machine-code patch points are invalid.");
    }

    constexpr std::size_t executableOffset = 0x3380U;
    const std::size_t destination = executableOffset - static_cast<std::size_t>(relocation)
        - static_cast<std::size_t>(relocation / 254 * 2);
    if (destination > executableOffset || destination + work.size() - executableOffset > work.size()) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC relocation range is invalid.");
    }
    std::memmove(work.data() + destination,
                 work.data() + executableOffset,
                 work.size() - executableOffset);
    int recordCount = static_cast<int>(work[0x0f]) * 256 + work[0x0e];
    recordCount -= relocation / 254;
    const std::size_t outputSize = static_cast<std::size_t>(recordCount) * 256U + 128U;
    if (recordCount <= 0 || outputSize > work.size()) {
        return failure(request.format, ExportError::InvalidLoaderTemplate,
                       "The Extended BASIC record count is invalid.");
    }
    work[0x0f] = static_cast<std::uint8_t>(recordCount >> 8);
    work[0x0e] = static_cast<std::uint8_t>(recordCount);
    work.resize(outputSize);
    GeneratedFile file{request.baseName, std::move(work)};
    return success(request.format, std::move(file));
}

} // namespace

core::FormatId exportFormatId(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Raw: return makeFormatId("raw");
    case ExportFormat::Rle: return makeFormatId("rle");
    case ExportFormat::TiFiles: return makeFormatId("tifiles");
    case ExportFormat::V9t9: return makeFormatId("v9t9");
    case ExportFormat::MsxScreen2: return makeFormatId("msx-sc2");
    case ExportFormat::ColecoCvPaint: return makeFormatId("coleco-cvpaint");
    case ExportFormat::AdamPowerPaint: return makeFormatId("adam-powerpaint");
    case ExportFormat::AdamHgr: return makeFormatId("adam-hgr");
    case ExportFormat::ColecoVisionRom: return makeFormatId("coleco-rom");
    case ExportFormat::ExtendedBasicProgram: return makeFormatId("extended-basic");
    case ExportFormat::ExtendedBasicRleProgram: return makeFormatId("extended-basic-rle");
    case ExportFormat::Png: return makeFormatId("png");
    }
    throw std::out_of_range("unknown export format");
}

std::optional<ExportFormat> exportFormat(const core::FormatId& id)
{
    return exportFormat(id.value());
}

std::optional<ExportFormat> exportFormat(std::string_view id)
{
    constexpr std::array values{
        ExportFormat::Raw, ExportFormat::Rle, ExportFormat::TiFiles,
        ExportFormat::V9t9, ExportFormat::MsxScreen2, ExportFormat::ColecoCvPaint,
        ExportFormat::AdamPowerPaint, ExportFormat::AdamHgr,
        ExportFormat::ColecoVisionRom, ExportFormat::ExtendedBasicProgram,
        ExportFormat::ExtendedBasicRleProgram, ExportFormat::Png,
    };
    for (const auto value : values) {
        if (exportFormatId(value) == id) return value;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> encodeLegacyRle(std::span<const std::uint8_t> input)
{
    std::vector<std::uint8_t> output;
    output.reserve(input.size());
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto beginsRun = [&](std::size_t index) {
            return index + 2U < input.size() && input[index] == input[index + 1U]
                && input[index] == input[index + 2U];
        };
        if (beginsRun(offset)) {
            std::size_t end = offset + 3U;
            while (end < input.size() && input[end] == input[offset]
                   && end - offset < 127U) {
                ++end;
            }
            output.push_back(static_cast<std::uint8_t>(0x80U | (end - offset)));
            output.push_back(input[offset]);
            offset = end;
        } else {
            std::size_t end = offset + 1U;
            while (end < input.size() && !beginsRun(end) && end - offset < 127U) ++end;
            output.push_back(static_cast<std::uint8_t>(end - offset));
            output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                          input.begin() + static_cast<std::ptrdiff_t>(end));
            offset = end;
        }
    }
    return output;
}

bool isExportApplicable(ExportFormat format, ConversionMode mode)
{
    switch (format) {
    case ExportFormat::Raw:
    case ExportFormat::Rle:
    case ExportFormat::TiFiles:
    case ExportFormat::V9t9:
    case ExportFormat::Png: return true;
    case ExportFormat::MsxScreen2:
    case ExportFormat::ColecoCvPaint:
    case ExportFormat::AdamPowerPaint:
    case ExportFormat::AdamHgr:
    case ExportFormat::ColecoVisionRom:
    case ExportFormat::ExtendedBasicProgram:
    case ExportFormat::ExtendedBasicRleProgram:
        return mode == ConversionMode::Bitmap9918
            || mode == ConversionMode::GreyscaleBitmap9918
            || mode == ConversionMode::BlackAndWhiteBitmap9918
            || mode == ConversionMode::BitmapColorOnly9918;
    }
    return false;
}

GeneratedFileManifest generateExport(const ExportRequest& request)
{
    if (!validBaseName(request.baseName)) {
        return failure(request.format, ExportError::InvalidBaseName,
                       "The export base name must be a single non-empty file name.");
    }
    if (request.format == ExportFormat::Png) {
        return failure(request.format, ExportError::AdapterRequired,
                       "PNG export is provided by the Qt image-I/O adapter.");
    }
    if (request.target == nullptr) {
        return failure(request.format, ExportError::MissingTarget,
                       "This export requires completed target-memory data.");
    }
    if (!core::validateTargetTables(*request.target)) {
        return failure(request.format, ExportError::InvalidTargetTables,
                       "The conversion result has an invalid target-table layout.");
    }
    if (!isExportApplicable(request.format, request.target->mode)) {
        return failure(request.format, ExportError::UnsupportedForConversionMode,
                       "The requested format does not support this conversion mode.");
    }

    switch (request.format) {
    case ExportFormat::Raw:
    case ExportFormat::Rle:
    case ExportFormat::TiFiles:
    case ExportFormat::V9t9: return tableFiles(request);
    case ExportFormat::MsxScreen2: return msxScreen2(request);
    case ExportFormat::ColecoCvPaint: return cvPaint(request);
    case ExportFormat::AdamPowerPaint: return powerPaint(request);
    case ExportFormat::AdamHgr: return adamHgr(request);
    case ExportFormat::ColecoVisionRom: return colecoRom(request);
    case ExportFormat::ExtendedBasicProgram: return extendedBasic(request, false);
    case ExportFormat::ExtendedBasicRleProgram: return extendedBasic(request, true);
    case ExportFormat::Png: break;
    }
    return failure(request.format, ExportError::EncodingFailed, "Unknown export format.");
}

} // namespace retrovdp::formats
