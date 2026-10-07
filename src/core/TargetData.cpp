#include "retrovdp/core/TargetData.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace retrovdp::core {
namespace {

RegionRoleId makeRegionRoleId(std::string_view value)
{
    auto id = RegionRoleId::create(value);
    if (!id) throw std::logic_error("invalid target table role identifier");
    return std::move(*id);
}

constexpr std::array bitmap9918Tables{
    TargetTableLayout{TargetTableRole::Pattern, 6144},
    TargetTableLayout{TargetTableRole::Color, 6144},
};
constexpr std::array blackAndWhite9918Tables{
    TargetTableLayout{TargetTableRole::Pattern, 6144},
};
constexpr std::array multicolor9918Tables{
    TargetTableLayout{TargetTableRole::Multicolor, 1536},
};
constexpr std::array dualMulticolor9918Tables{
    TargetTableLayout{TargetTableRole::MulticolorFrame1, 1536},
    TargetTableLayout{TargetTableRole::MulticolorFrame2, 1536},
};
constexpr std::array halfMulticolor9918Tables{
    TargetTableLayout{TargetTableRole::Pattern, 6144},
    TargetTableLayout{TargetTableRole::Color, 6144},
    TargetTableLayout{TargetTableRole::Multicolor, 2048},
};
constexpr std::array bitmapColorOnly9918Tables{
    TargetTableLayout{TargetTableRole::FixedPattern, 6144},
    TargetTableLayout{TargetTableRole::Color, 6144},
};
constexpr std::array palettedBitmapF18ATables{
    TargetTableLayout{TargetTableRole::Pattern, 6144},
    TargetTableLayout{TargetTableRole::Color, 6144},
    TargetTableLayout{TargetTableRole::Palette, 32},
};
constexpr std::array scanlinePaletteBitmapF18ATables{
    TargetTableLayout{TargetTableRole::Pattern, 6144},
    TargetTableLayout{TargetTableRole::Color, 6144},
    TargetTableLayout{TargetTableRole::ScanlinePalettes, 6144},
};
constexpr std::array screen5V9938Tables{
    TargetTableLayout{TargetTableRole::Framebuffer, 256U * 212U / 2U},
    TargetTableLayout{TargetTableRole::Palette, 32},
};
constexpr std::array screen6V9938Tables{
    TargetTableLayout{TargetTableRole::Framebuffer, 512U * 212U / 4U},
    TargetTableLayout{TargetTableRole::Palette, 8},
};
constexpr std::array screen7V9938Tables{
    TargetTableLayout{TargetTableRole::Framebuffer, 512U * 212U / 2U},
    TargetTableLayout{TargetTableRole::Palette, 32},
};
constexpr std::array screen8V9938Tables{
    TargetTableLayout{TargetTableRole::Framebuffer, 256U * 212U},
};
constexpr std::array screenYjkV9958Tables{
    TargetTableLayout{TargetTableRole::Framebuffer, 256U * 212U},
};
constexpr std::array screenYaeV9958Tables{
    TargetTableLayout{TargetTableRole::Framebuffer, 256U * 212U},
    TargetTableLayout{TargetTableRole::Palette, 32},
};
constexpr std::array mode4Sms192Tables{
    TargetTableLayout{TargetTableRole::Pattern, 0x3800U},
    TargetTableLayout{TargetTableRole::TileMap, 2048U},
    TargetTableLayout{TargetTableRole::Palette, 32U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 11U},
};
constexpr std::array mode4SmsExtendedTables{
    TargetTableLayout{TargetTableRole::Pattern, 0x3700U},
    TargetTableLayout{TargetTableRole::TileMap, 2048U},
    TargetTableLayout{TargetTableRole::Palette, 32U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 11U},
};
constexpr std::array mode5GenesisH32Tables{
    TargetTableLayout{TargetTableRole::Pattern, 0xb000U},
    TargetTableLayout{TargetTableRole::TileMap, 2048U},
    TargetTableLayout{TargetTableRole::Palette, 128U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 24U},
};
constexpr std::array mode5GenesisH40Tables{
    TargetTableLayout{TargetTableRole::Pattern, 0xa800U},
    TargetTableLayout{TargetTableRole::TileMap, 4096U},
    TargetTableLayout{TargetTableRole::Palette, 128U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 24U},
};
constexpr std::array huc6270Background256Tables{
    TargetTableLayout{TargetTableRole::Pattern, 0xf800U},
    TargetTableLayout{TargetTableRole::TileMap, 0x0800U},
    TargetTableLayout{TargetTableRole::Palette, 1024U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 42U},
};
constexpr std::array huc6270Background320Tables{
    TargetTableLayout{TargetTableRole::Pattern, 0xf000U},
    TargetTableLayout{TargetTableRole::TileMap, 0x1000U},
    TargetTableLayout{TargetTableRole::Palette, 1024U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 42U},
};
constexpr std::array vicIICharacterTables{
    TargetTableLayout{TargetTableRole::Pattern, 2048U},
    TargetTableLayout{TargetTableRole::TileMap, 1000U},
    TargetTableLayout{TargetTableRole::Color, 1000U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 47U},
};
constexpr std::array vicIIHiresBitmapTables{
    TargetTableLayout{TargetTableRole::Framebuffer, 8000U},
    TargetTableLayout{TargetTableRole::TileMap, 1000U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 47U},
};
constexpr std::array vicIIMulticolorBitmapTables{
    TargetTableLayout{TargetTableRole::Framebuffer, 8000U},
    TargetTableLayout{TargetTableRole::TileMap, 1000U},
    TargetTableLayout{TargetTableRole::Color, 1000U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 47U},
};
constexpr std::array vicCharacterTables{
    TargetTableLayout{TargetTableRole::Pattern, 2048U},
    TargetTableLayout{TargetTableRole::TileMap, 506U},
    TargetTableLayout{TargetTableRole::Color, 506U},
    TargetTableLayout{TargetTableRole::DisplayRegisters, 16U},
};

void setPaletteError(PaletteError* destination, PaletteError error)
{
    if (destination != nullptr) {
        *destination = error;
    }
}

} // namespace

RegionRoleId targetTableRoleId(TargetTableRole role)
{
    switch (role) {
    case TargetTableRole::Pattern: return makeRegionRoleId("pattern");
    case TargetTableRole::Color: return makeRegionRoleId("color");
    case TargetTableRole::Multicolor: return makeRegionRoleId("multicolor");
    case TargetTableRole::MulticolorFrame1: return makeRegionRoleId("multicolor-frame-1");
    case TargetTableRole::MulticolorFrame2: return makeRegionRoleId("multicolor-frame-2");
    case TargetTableRole::FixedPattern: return makeRegionRoleId("fixed-pattern");
    case TargetTableRole::Palette: return makeRegionRoleId("palette");
    case TargetTableRole::ScanlinePalettes: return makeRegionRoleId("scanline-palettes");
    case TargetTableRole::Framebuffer: return makeRegionRoleId("framebuffer");
    case TargetTableRole::TileMap: return makeRegionRoleId("tile-map");
    case TargetTableRole::DisplayRegisters: return makeRegionRoleId("display-registers");
    }
    throw std::out_of_range("unknown target table role");
}

std::optional<TargetTableRole> targetTableRole(const RegionRoleId& id)
{
    return targetTableRole(id.value());
}

std::optional<TargetTableRole> targetTableRole(std::string_view id)
{
    constexpr std::array roles{
        TargetTableRole::Pattern, TargetTableRole::Color, TargetTableRole::Multicolor,
        TargetTableRole::MulticolorFrame1, TargetTableRole::MulticolorFrame2,
        TargetTableRole::FixedPattern, TargetTableRole::Palette,
        TargetTableRole::ScanlinePalettes,
        TargetTableRole::Framebuffer, TargetTableRole::TileMap,
        TargetTableRole::DisplayRegisters,
    };
    for (const auto role : roles) {
        if (targetTableRoleId(role) == id) return role;
    }
    return std::nullopt;
}

std::optional<Palette> Palette::create(std::vector<RgbColor> colors, PaletteError* error)
{
    if (colors.empty()) {
        setPaletteError(error, PaletteError::Empty);
        return std::nullopt;
    }
    if (colors.size() > maximumColorCount) {
        setPaletteError(error, PaletteError::TooManyColors);
        return std::nullopt;
    }

    setPaletteError(error, PaletteError::None);
    return Palette(std::move(colors));
}

const RgbColor& Palette::at(std::size_t index) const
{
    return colors_.at(index);
}

Palette::Palette(std::vector<RgbColor> colors)
    : colors_(std::move(colors))
{
}

std::span<const TargetTableLayout> expectedTargetTables(ConversionMode mode)
{
    switch (mode) {
    case ConversionMode::Bitmap9918:
    case ConversionMode::GreyscaleBitmap9918:
        return bitmap9918Tables;
    case ConversionMode::BlackAndWhiteBitmap9918:
        return blackAndWhite9918Tables;
    case ConversionMode::Multicolor9918:
        return multicolor9918Tables;
    case ConversionMode::DualMulticolor9918:
        return dualMulticolor9918Tables;
    case ConversionMode::HalfMulticolor9918:
        return halfMulticolor9918Tables;
    case ConversionMode::BitmapColorOnly9918:
        return bitmapColorOnly9918Tables;
    case ConversionMode::PalettedBitmapF18A:
        return palettedBitmapF18ATables;
    case ConversionMode::ScanlinePaletteBitmapF18A:
        return scanlinePaletteBitmapF18ATables;
    case ConversionMode::Screen5V9938: return screen5V9938Tables;
    case ConversionMode::Screen6V9938: return screen6V9938Tables;
    case ConversionMode::Screen7V9938: return screen7V9938Tables;
    case ConversionMode::Screen8V9938:
    case ConversionMode::Screen12V9958: return screen8V9938Tables;
    case ConversionMode::Screen10V9958:
    case ConversionMode::Screen11V9958: return screenYaeV9958Tables;
    case ConversionMode::Mode4Sms192: return mode4Sms192Tables;
    case ConversionMode::Mode4Sms224:
    case ConversionMode::Mode4Sms240: return mode4SmsExtendedTables;
    case ConversionMode::Mode5GenesisH32:
    case ConversionMode::Mode5GenesisH32Pal: return mode5GenesisH32Tables;
    case ConversionMode::Mode5GenesisH40:
    case ConversionMode::Mode5GenesisH40Pal: return mode5GenesisH40Tables;
    case ConversionMode::HuC6270Background256: return huc6270Background256Tables;
    case ConversionMode::HuC6270Background320: return huc6270Background320Tables;
    case ConversionMode::VicIIHiresCharacter:
    case ConversionMode::VicIIMulticolorCharacter: return vicIICharacterTables;
    case ConversionMode::VicIIHiresBitmap: return vicIIHiresBitmapTables;
    case ConversionMode::VicIIMulticolorBitmap: return vicIIMulticolorBitmapTables;
    case ConversionMode::VicHiresCharacter:
    case ConversionMode::VicMulticolorCharacter: return vicCharacterTables;
    }
    return {};
}

TargetTableValidation validateTargetTables(ConversionMode mode,
                                           std::span<const TargetMemoryTable> tables)
{
    const std::span<const TargetTableLayout> expected = expectedTargetTables(mode);
    if (expected.empty()) {
        return {.error = TargetTableError::UnsupportedConversionMode};
    }
    if (tables.size() != expected.size()) {
        const std::size_t mismatchIndex =
            tables.size() < expected.size() ? tables.size() : expected.size();
        return {
            .error = TargetTableError::TableCountMismatch,
            .tableIndex = mismatchIndex,
            .expected = mismatchIndex < expected.size() ? expected[mismatchIndex]
                                                        : TargetTableLayout{},
        };
    }

    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (tables[index].role != expected[index].role) {
            return {
                .error = TargetTableError::TableRoleMismatch,
                .tableIndex = index,
                .expected = expected[index],
            };
        }
        if (tables[index].bytes.size() != expected[index].byteSize) {
            return {
                .error = TargetTableError::TableSizeMismatch,
                .tableIndex = index,
                .expected = expected[index],
            };
        }
    }
    return {};
}

} // namespace retrovdp::core
