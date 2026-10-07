#pragma once

#include <cstdint>

namespace retrovdp::core {

enum class TargetProfileId : std::uint8_t {
    Tms9918A,
    F18A,
    V9938,
    V9958,
    SegaMasterSystem,
    SegaGenesis,
    HuC6270,
    VicII,
    Vic,
};

enum class ConversionMode : std::uint8_t {
    Bitmap9918,
    GreyscaleBitmap9918,
    BlackAndWhiteBitmap9918,
    Multicolor9918,
    DualMulticolor9918,
    HalfMulticolor9918,
    BitmapColorOnly9918,
    PalettedBitmapF18A,
    ScanlinePaletteBitmapF18A,
    Screen5V9938,
    Screen6V9938,
    Screen7V9938,
    Screen8V9938,
    Screen10V9958,
    Screen11V9958,
    Screen12V9958,
    Mode4Sms192,
    Mode4Sms224,
    Mode4Sms240,
    Mode5GenesisH32,
    Mode5GenesisH40,
    Mode5GenesisH32Pal,
    Mode5GenesisH40Pal,
    HuC6270Background256,
    HuC6270Background320,
    VicIIHiresCharacter,
    VicIIMulticolorCharacter,
    VicIIHiresBitmap,
    VicIIMulticolorBitmap,
    VicHiresCharacter,
    VicMulticolorCharacter,
};

enum class DitherMode : std::uint8_t {
    None,
    FloydSteinberg,
    Atkinson,
    Pattern,
    Diagonal,
    Ordered,
    OrderedWithError,
    Custom,
};

struct ErrorDistributionKernel {
    std::uint8_t downLeft{};
    std::uint8_t down{};
    std::uint8_t downRight{};
    std::uint8_t right{};
    std::uint8_t farRight{};
    std::uint8_t downTwo{};

    [[nodiscard]] constexpr int totalWeight() const
    {
        return downLeft + down + downRight + right + farRight + downTwo;
    }
};

enum class OrderedDitherMapSize : std::uint8_t {
    TwoByTwo = 2,
    FourByFour = 4,
};

enum class ErrorAccumulationMode : std::uint8_t {
    Average,
    Accumulate,
};

enum class PaletteSelectionMode : std::uint8_t {
    MedianCut,
    Popularity,
};

struct ConversionSettings {
    TargetProfileId targetProfile{TargetProfileId::Tms9918A};
    ConversionMode mode{ConversionMode::Bitmap9918};
    DitherMode dither{DitherMode::Atkinson};
    OrderedDitherMapSize orderedDitherMapSize{OrderedDitherMapSize::TwoByTwo};
    int orderedDitherBrightness{0};
    ErrorAccumulationMode errorAccumulation{ErrorAccumulationMode::Accumulate};
    ErrorDistributionKernel errorDistribution{2, 2, 2, 2, 1, 1};
    int targetWidth{256};
    int targetHeight{192};
    double gamma{1.0};
    double maximumColorShiftPercent{1.0};
    int maximumMulticolorDifferencePercent{95};
    bool perceptualColorMatching{false};
    double perceptualRedWeight{0.30};
    double perceptualGreenWeight{0.52};
    double perceptualBlueWeight{0.18};
    double lumaEmphasis{1.2};
    bool stretchHistogram{false};
    PaletteSelectionMode paletteSelection{PaletteSelectionMode::MedianCut};
    int scanlineStaticColorCount{0};
    bool scanlineRegion1{true};
    bool scanlineRegion2{true};
    bool scanlineRegion3{true};
};

} // namespace retrovdp::core
