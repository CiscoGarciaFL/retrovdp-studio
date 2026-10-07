#pragma once

#include "retrovdp/core/ConversionSettings.hpp"
#include "retrovdp/core/StableId.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace retrovdp::core {

enum class TargetProfileStatus : std::uint8_t {
    Implemented,
    Planned,
};

enum class TargetKind : std::uint8_t {
    VideoDisplayProcessor,
};

enum class PaletteModel : std::uint8_t {
    Fixed,
    ProgrammableRgb,
    FixedRgb332,
    Yjk,
    YjkWithPalette,
    FixedRevisionDependent,
};

struct Rational {
    std::uint32_t numerator{1};
    std::uint32_t denominator{1};
};

struct DisplayGeometry {
    std::uint32_t width{};
    std::uint32_t height{};
    Rational pixelAspectRatio{};
};

struct PaletteDescriptor {
    PaletteModel model{PaletteModel::Fixed};
    std::uint16_t entryCount{};
    std::uint16_t workingColorCount{};
    std::uint8_t channelBits{};
};

struct CharacterPatternDescriptor {
    std::uint16_t pixelWidth{8};
    std::uint16_t pixelHeight{8};
    std::uint16_t patternsPerSet{256};
    std::uint16_t setCount{3};
    std::uint16_t mapColumns{32};
    std::uint16_t mapRows{24};
};

struct SpriteDescriptor {
    std::uint16_t minimumPixelSize{8};
    std::uint16_t maximumPixelSize{16};
    std::uint16_t patternsPerSet{32};
    std::uint16_t maximumVisibleSprites{32};
    std::uint8_t maximumColorDepth{1};
    bool usesGlobalSize{true};
    bool supportsPerSpriteSize{};
};

enum class ModeOption : std::uint16_t {
    None = 0,
    WorkingPalette = 1U << 0U,
    PaletteSelection = 1U << 1U,
    ScanlinePalette = 1U << 2U,
    MulticolorFlickerLimit = 1U << 3U,
};

[[nodiscard]] constexpr ModeOption operator|(ModeOption left, ModeOption right)
{
    return static_cast<ModeOption>(
        static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr bool hasOption(ModeOption value, ModeOption requested)
{
    return (static_cast<std::uint16_t>(value)
            & static_cast<std::uint16_t>(requested))
        == static_cast<std::uint16_t>(requested);
}

struct DisplayModeDescriptor {
    ConversionMode legacyMode{ConversionMode::Bitmap9918};
    ModeId stableId;
    std::string_view displayName;
    TargetProfileId primaryTarget{TargetProfileId::Tms9918A};
    DisplayGeometry geometry;
    PaletteDescriptor palette;
    ModeOption options{ModeOption::None};
};

enum class TargetCapability : std::uint32_t {
    None = 0,
    FixedPalette = 1U << 0U,
    ProgrammablePalette = 1U << 1U,
    CharacterPatterns = 1U << 2U,
    Sprites = 1U << 3U,
    TileMaps = 1U << 4U,
    BitmapConversion = 1U << 5U,
    EnhancedColor = 1U << 6U,
    MultipleTileLayers = 1U << 7U,
    SpriteMode2 = 1U << 8U,
    HorizontalScroll = 1U << 9U,
    YjkColor = 1U << 10U,
};

[[nodiscard]] constexpr TargetCapability operator|(TargetCapability left,
                                                   TargetCapability right)
{
    return static_cast<TargetCapability>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool hasCapability(TargetCapability value,
                                           TargetCapability requested)
{
    return (static_cast<std::uint32_t>(value)
            & static_cast<std::uint32_t>(requested))
        == static_cast<std::uint32_t>(requested);
}

struct TargetProfile {
    TargetProfileId id{TargetProfileId::Tms9918A};
    TargetId stableId;
    std::string_view displayName;
    TargetKind kind{TargetKind::VideoDisplayProcessor};
    TargetProfileStatus status{TargetProfileStatus::Planned};
    std::uint32_t nominalVramBytes{};
    TargetCapability capabilities{TargetCapability::None};
    CharacterPatternDescriptor characterPatterns{};
    SpriteDescriptor sprites{};
    std::span<const ConversionMode> conversionModes;
};

enum class RegistryError : std::uint8_t {
    None,
    DuplicateTargetProfile,
    DuplicateTargetId,
    DuplicateMode,
    DuplicateModeId,
    InvalidGeometry,
    InvalidPalette,
    InvalidCharacterPatterns,
    InvalidSprites,
    ImplementedTargetHasNoModes,
    DuplicateTargetMode,
    UnknownTargetMode,
    UnknownPrimaryTarget,
    PrimaryTargetDoesNotSupportMode,
};

struct RegistryValidation {
    RegistryError error{RegistryError::None};
    std::size_t index{};

    [[nodiscard]] explicit constexpr operator bool() const
    {
        return error == RegistryError::None;
    }
};

[[nodiscard]] std::span<const TargetProfile> targetProfiles();
[[nodiscard]] std::span<const DisplayModeDescriptor> displayModes();
[[nodiscard]] const TargetProfile& targetProfile(TargetProfileId id);
[[nodiscard]] std::optional<TargetProfileId> targetProfileId(std::string_view stableId);
[[nodiscard]] std::optional<TargetProfileId> targetProfileId(const TargetId& stableId);
[[nodiscard]] const DisplayModeDescriptor& displayMode(ConversionMode mode);
[[nodiscard]] const DisplayModeDescriptor* findDisplayMode(const ModeId& stableId);
[[nodiscard]] std::optional<ConversionMode> conversionMode(std::string_view stableId);
[[nodiscard]] std::optional<ConversionMode> conversionMode(const ModeId& stableId);
[[nodiscard]] RegistryValidation validateRegistry(
    std::span<const TargetProfile> profiles,
    std::span<const DisplayModeDescriptor> modes);
[[nodiscard]] RegistryValidation validateRegistry();
[[nodiscard]] bool supportsConversionMode(TargetProfileId profile,
                                          ConversionMode mode);
[[nodiscard]] TargetProfileId primaryTargetProfile(ConversionMode mode);
[[nodiscard]] TargetProfileId effectiveTargetProfile(TargetProfileId requested,
                                                      ConversionMode mode);
[[nodiscard]] ConversionMode defaultConversionMode(TargetProfileId profile);

} // namespace retrovdp::core
