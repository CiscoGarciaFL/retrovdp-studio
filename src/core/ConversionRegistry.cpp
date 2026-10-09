#include "retrovdp/core/ConversionRegistry.hpp"

#include "retrovdp/core/Bitmap9918Converter.hpp"
#include "retrovdp/core/CommodoreVdpConverter.hpp"
#include "retrovdp/core/F18AConverter.hpp"
#include "retrovdp/core/HuC6270Converter.hpp"
#include "retrovdp/core/Multicolor9918Converter.hpp"
#include "retrovdp/core/NintendoPpuConverter.hpp"
#include "retrovdp/core/PaletteSelection.hpp"
#include "retrovdp/core/SegaSmsVdpConverter.hpp"
#include "retrovdp/core/SegaGenesisVdpConverter.hpp"
#include "retrovdp/core/TargetProfile.hpp"
#include "retrovdp/core/YamahaVdpConverter.hpp"

#include <algorithm>
#include <string>

namespace retrovdp::core {
namespace {

ConversionResult failed(std::string message)
{
    return {.status = ConversionStatus::Failed,
            .diagnostics = {{DiagnosticSeverity::Error, "unregistered-converter",
                             std::move(message)}}};
}

void adoptProfile(ConversionResult& result, TargetProfileId profile)
{
    if (result.target) result.target->profile = profile;
}

} // namespace

ConversionResult compileRegisteredConversion(const RgbImage& source,
                                            const ConversionSettings& settings,
                                            std::span<const RgbColor> workingPalette,
                                            CancellationToken cancellation,
                                            ConversionProgressCallback progress)
{
    if (!supportsConversionMode(settings.targetProfile, settings.mode)) {
        return failed("The target profile does not advertise the selected mode.");
    }

    ConversionResult result;
    switch (settings.mode) {
    case ConversionMode::GameBoyBackground:
    case ConversionMode::GameBoyColorBackground:
    case ConversionMode::SuperNesMode0Background:
    case ConversionMode::SuperNesMode1Background:
    case ConversionMode::SuperNesMode3Background:
        result = convertNintendoPpu(source, settings, cancellation, std::move(progress));
        break;
    case ConversionMode::HuC6270Background256:
    case ConversionMode::HuC6270Background320:
        result = convertHuC6270Background(
            source, settings, cancellation, std::move(progress));
        break;
    case ConversionMode::VicIIHiresCharacter:
    case ConversionMode::VicIIMulticolorCharacter:
    case ConversionMode::VicIIHiresBitmap:
    case ConversionMode::VicIIMulticolorBitmap:
    case ConversionMode::VicHiresCharacter:
    case ConversionMode::VicMulticolorCharacter:
        result = convertCommodoreDisplay(
            source, settings, cancellation, std::move(progress));
        break;
    case ConversionMode::Mode5GenesisH32:
    case ConversionMode::Mode5GenesisH40:
    case ConversionMode::Mode5GenesisH32Pal:
    case ConversionMode::Mode5GenesisH40Pal:
        result = convertSegaGenesisMode5(
            source, settings, cancellation, std::move(progress));
        break;
    case ConversionMode::Mode4Sms192:
    case ConversionMode::Mode4Sms224:
    case ConversionMode::Mode4Sms240:
        result = convertSegaSmsMode4(source, settings, cancellation, std::move(progress));
        break;
    case ConversionMode::Screen5V9938:
    case ConversionMode::Screen6V9938:
    case ConversionMode::Screen7V9938:
    case ConversionMode::Screen8V9938:
    case ConversionMode::Screen10V9958:
    case ConversionMode::Screen11V9958:
    case ConversionMode::Screen12V9958:
        result = convertYamahaBitmap(source, settings, cancellation, std::move(progress));
        break;
    case ConversionMode::Bitmap9918:
    case ConversionMode::GreyscaleBitmap9918:
    case ConversionMode::BlackAndWhiteBitmap9918:
    case ConversionMode::Multicolor9918:
    case ConversionMode::DualMulticolor9918:
    case ConversionMode::HalfMulticolor9918:
    case ConversionMode::BitmapColorOnly9918: {
        if (workingPalette.size() != 15U) {
            return failed("The TI-compatible compiler requires fifteen working colors.");
        }
        auto palette = Palette::create(
            std::vector<RgbColor>(workingPalette.begin(), workingPalette.end()));
        if (!palette) return failed("The TI-compatible working palette is invalid.");
        switch (settings.mode) {
        case ConversionMode::Bitmap9918:
            result = convertBitmap9918(source, *palette, settings, cancellation,
                                       std::move(progress));
            break;
        case ConversionMode::GreyscaleBitmap9918:
            result = convertGreyscaleBitmap9918(
                source, greyscaleBitmap9918Palette(*palette), settings, cancellation,
                std::move(progress));
            break;
        case ConversionMode::BlackAndWhiteBitmap9918:
            result = convertBlackAndWhiteBitmap9918(source, *palette, settings,
                                                    cancellation, std::move(progress));
            break;
        case ConversionMode::Multicolor9918:
            result = convertMulticolor9918(source, *palette, settings, cancellation,
                                           std::move(progress));
            break;
        case ConversionMode::DualMulticolor9918:
            result = convertDualMulticolor9918(source, *palette, settings, cancellation,
                                               std::move(progress));
            break;
        case ConversionMode::HalfMulticolor9918:
            result = convertHalfMulticolor9918(source, *palette, settings, cancellation,
                                               std::move(progress));
            break;
        case ConversionMode::BitmapColorOnly9918:
            result = convertBitmapColorOnly9918(source, *palette, settings, cancellation,
                                                std::move(progress));
            break;
        default: break;
        }
        break;
    }
    case ConversionMode::PalettedBitmapF18A: {
        auto selected = settings.paletteSelection == PaletteSelectionMode::Popularity
            ? selectPopularPalette(source, 15, PopularityWeighting::HorizontalCenter)
            : selectMedianCutPalette(source, 15, MedianCutColorDepth::Rgb444);
        if (!selected) return failed("A 15-color F18A palette could not be selected.");
        result = convertPalettedBitmapF18A(source, *selected.palette, settings,
                                           cancellation, std::move(progress));
        break;
    }
    case ConversionMode::ScanlinePaletteBitmapF18A:
        result = convertScanlinePaletteBitmapF18A(source, settings, cancellation,
                                                  std::move(progress));
        break;
    }
    adoptProfile(result, settings.targetProfile);
    return result;
}

} // namespace retrovdp::core
