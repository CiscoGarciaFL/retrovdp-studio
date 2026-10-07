#include "retrovdp/formats/Export.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace retrovdp;
namespace fs = std::filesystem;

struct TestContext {
    int failures{};

    void expect(bool condition, const std::string& message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

fs::path golden(std::string_view relative)
{
    return fs::path(RETROVDP_GOLDEN_DIR) / fs::path(relative);
}

std::vector<std::uint8_t> readFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::span<const std::uint8_t> payload(const std::vector<std::uint8_t>& file)
{
    return std::span(file).subspan(128U);
}

const formats::GeneratedFile* findFile(const formats::GeneratedFileManifest& manifest,
                                       std::string_view name)
{
    const auto found = std::ranges::find(manifest.files, name, &formats::GeneratedFile::fileName);
    return found == manifest.files.end() ? nullptr : &*found;
}

core::TargetMemoryImage bitmapTarget()
{
    const auto patternFile = readFile(golden(
        "reference/original-1_9_1/bitmap-9918a/default/tiny-rgba/TINY-RGBA.TIAP"));
    const auto colorFile = readFile(golden(
        "reference/original-1_9_1/bitmap-9918a/default/tiny-rgba/TINY-RGBA.TIAC"));
    return {
        .mode = core::ConversionMode::Bitmap9918,
        .tables = {
            {core::TargetTableRole::Pattern,
             std::vector<std::uint8_t>(payload(patternFile).begin(), payload(patternFile).end())},
            {core::TargetTableRole::Color,
             std::vector<std::uint8_t>(payload(colorFile).begin(), payload(colorFile).end())},
        },
    };
}

void expectGolden(TestContext& test,
                  const formats::GeneratedFileManifest& manifest,
                  std::string_view name,
                  std::string_view relative)
{
    const auto* generated = findFile(manifest, name);
    test.expect(generated != nullptr, "manifest should contain " + std::string(name));
    if (generated != nullptr) {
        test.expect(generated->bytes == readFile(golden(relative)),
                    std::string(name) + " should match the original byte-for-byte");
    }
}

void testTableFormats(TestContext& test, const core::TargetMemoryImage& target)
{
    const auto raw = formats::generateExport({
        .format = formats::ExportFormat::Raw,
        .baseName = "tinyraw",
        .target = &target,
    });
    test.expect(static_cast<bool>(raw), "RAW export should succeed");
    expectGolden(test, raw, "TINYRAW.TIAP",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/raw/TINYRAW.TIAP");
    expectGolden(test, raw, "TINYRAW.TIAC",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/raw/TINYRAW.TIAC");

    const auto tifiles = formats::generateExport({
        .format = formats::ExportFormat::TiFiles,
        .baseName = "tiny-rgba",
        .target = &target,
    });
    expectGolden(test, tifiles, "TINY-RGBA.TIAP",
                 "reference/original-1_9_1/bitmap-9918a/default/tiny-rgba/TINY-RGBA.TIAP");
    expectGolden(test, tifiles, "TINY-RGBA.TIAC",
                 "reference/original-1_9_1/bitmap-9918a/default/tiny-rgba/TINY-RGBA.TIAC");

    const auto v9t9 = formats::generateExport({
        .format = formats::ExportFormat::V9t9,
        .baseName = "tinyv9",
        .target = &target,
    });
    expectGolden(test, v9t9, "TINYV9.TIAP",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/v9t9/TINYV9.TIAP");
    expectGolden(test, v9t9, "TINYV9.TIAC",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/v9t9/TINYV9.TIAC");
    const auto truncatedV9t9 = formats::generateExport({
        .format = formats::ExportFormat::V9t9,
        .baseName = "longname",
        .target = &target,
    });
    test.expect(findFile(truncatedV9t9, "LONGNA.TIAP") != nullptr,
                "V9T9 output should truncate its base name to six characters");

    const auto rle = formats::generateExport({
        .format = formats::ExportFormat::Rle,
        .baseName = "tinyrle",
        .target = &target,
    });
    expectGolden(test, rle, "TINYRLE.TIAP",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/rle/TINYRLE.TIAP");
    expectGolden(test, rle, "TINYRLE.TIAC",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/rle/TINYRLE.TIAC");

    const std::vector<std::uint8_t> edge{1, 1, 1, 2, 3, 4, 4};
    const std::vector<std::uint8_t> encoded{0x83, 1, 4, 2, 3, 4, 4};
    test.expect(formats::encodeLegacyRle(edge) == encoded,
                "RLE should encode runs of three and retain shorter runs as literals");
}

void testMachineFormats(TestContext& test, const core::TargetMemoryImage& target)
{
    struct Case {
        formats::ExportFormat format;
        std::string name;
        std::string output;
        std::string relative;
    };
    const std::vector<Case> cases{
        {formats::ExportFormat::MsxScreen2, "tiny", "tiny.sc2",
         "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/msx-sc2/tiny.sc2"},
        {formats::ExportFormat::ColecoCvPaint, "tiny", "tiny.pc",
         "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/cvpaint/tiny.pc"},
        {formats::ExportFormat::AdamPowerPaint, "tiny", "tiny.pp",
         "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/powerpaint/tiny.pp"},
        {formats::ExportFormat::AdamHgr, "tiny", "tiny.HGRH",
         "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/hgr/tiny.HGRH"},
    };
    for (const auto& item : cases) {
        const auto result = formats::generateExport({
            .format = item.format,
            .baseName = item.name,
            .target = &target,
        });
        test.expect(static_cast<bool>(result), item.output + " export should succeed");
        expectGolden(test, result, item.output, item.relative);
    }
}

void testTemplateFormats(TestContext& test, const core::TargetMemoryImage& target)
{
    const auto expectedRom = readFile(golden(
        "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/coleco-rom/tiny.rom"));
    constexpr std::size_t tableBytes = 2U * 6144U;
    const auto romTemplate = std::span(expectedRom).first(expectedRom.size() - tableBytes - 1U);
    const auto rom = formats::generateExport({
        .format = formats::ExportFormat::ColecoVisionRom,
        .baseName = "tiny",
        .target = &target,
        .loaders = {.colecoVisionRom = romTemplate},
    });
    test.expect(static_cast<bool>(rom), "ColecoVision ROM export should accept a valid template");
    const auto* generatedRom = findFile(rom, "tiny.rom");
    test.expect(generatedRom != nullptr && generatedRom->bytes == expectedRom,
                "ColecoVision ROM should match the original byte-for-byte");

    auto xbTemplate = readFile(golden(
        "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/ti-xb/tinyxb"));
    // Reconstruct the four sentinel bytes that the captured direct export
    // replaces. The remainder is the approved loader fixture.
    xbTemplate[0x3fe] = 0xcd;
    xbTemplate[0x3ff] = 0xe6;
    xbTemplate[0x1c2e] = 0xe5;
    xbTemplate[0x1c2f] = 0xe6;
    const auto xb = formats::generateExport({
        .format = formats::ExportFormat::ExtendedBasicProgram,
        .baseName = "tinyxb",
        .target = &target,
        .loaders = {.extendedBasicProgram = xbTemplate},
    });
    test.expect(static_cast<bool>(xb),
                "Extended BASIC export should accept a valid template: " + xb.message);
    expectGolden(test, xb, "tinyxb",
                 "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/ti-xb/tinyxb");

    const auto xbRle = formats::generateExport({
        .format = formats::ExportFormat::ExtendedBasicRleProgram,
        .baseName = "tinyxbr",
        .target = &target,
        .loaders = {.extendedBasicProgram = xbTemplate},
    });
    test.expect(static_cast<bool>(xbRle), "RLE Extended BASIC export should succeed");
    if (const auto* generated = findFile(xbRle, "tinyxbr")) {
        const auto expected = readFile(golden(
            "reference/original-1_9_1/exports/bitmap-9918a/tiny-rgba/ti-xb-rle/tinyxbr"));
        test.expect(generated->bytes.size() == expected.size(),
                    "RLE Extended BASIC should match the original record count");
        test.expect(std::equal(generated->bytes.begin(), generated->bytes.begin() + 0x2ce2,
                               expected.begin())
                        && std::equal(generated->bytes.begin() + 0x2e5e,
                                      generated->bytes.end(), expected.begin() + 0x2e5e),
                    "RLE Extended BASIC should match the golden container outside template-owned bytes");
        test.expect(std::equal(generated->bytes.begin() + 0x352,
                               generated->bytes.begin() + 0x366,
                               expected.begin() + 0x352),
                    "RLE Extended BASIC should patch the golden table addresses");
    }

    const auto missing = formats::generateExport({
        .format = formats::ExportFormat::ColecoVisionRom,
        .baseName = "tiny",
        .target = &target,
    });
    test.expect(missing.error == formats::ExportError::MissingLoaderTemplate,
                "template-based formats should clearly report a missing template");
}

void testValidation(TestContext& test, const core::TargetMemoryImage& target)
{
    auto multicolor = target;
    multicolor.mode = core::ConversionMode::Multicolor9918;
    multicolor.tables = {{core::TargetTableRole::Multicolor,
                          std::vector<std::uint8_t>(1536U, 0)}};
    const auto rejected = formats::generateExport({
        .format = formats::ExportFormat::MsxScreen2,
        .baseName = "image",
        .target = &multicolor,
    });
    test.expect(rejected.error == formats::ExportError::UnsupportedForConversionMode,
                "MSX Screen 2 should reject multicolor conversion output");

    const auto unsafe = formats::generateExport({
        .format = formats::ExportFormat::Raw,
        .baseName = "../image",
        .target = &target,
    });
    test.expect(unsafe.error == formats::ExportError::InvalidBaseName,
                "export names should not escape the selected directory");

    const auto png = formats::generateExport({
        .format = formats::ExportFormat::Png,
        .baseName = "image",
        .target = &target,
    });
    test.expect(png.error == formats::ExportError::AdapterRequired,
                "portable core should route PNG through the Qt adapter");
}

void testAllTargetLayouts(TestContext& test)
{
    const std::vector<core::ConversionMode> modes{
        core::ConversionMode::Bitmap9918,
        core::ConversionMode::GreyscaleBitmap9918,
        core::ConversionMode::BlackAndWhiteBitmap9918,
        core::ConversionMode::Multicolor9918,
        core::ConversionMode::DualMulticolor9918,
        core::ConversionMode::HalfMulticolor9918,
        core::ConversionMode::BitmapColorOnly9918,
        core::ConversionMode::PalettedBitmapF18A,
        core::ConversionMode::ScanlinePaletteBitmapF18A,
        core::ConversionMode::Screen5V9938,
        core::ConversionMode::Screen6V9938,
        core::ConversionMode::Screen7V9938,
        core::ConversionMode::Screen8V9938,
        core::ConversionMode::Screen10V9958,
        core::ConversionMode::Screen11V9958,
        core::ConversionMode::Screen12V9958,
        core::ConversionMode::Mode4Sms192,
        core::ConversionMode::Mode4Sms224,
        core::ConversionMode::Mode4Sms240,
        core::ConversionMode::Mode5GenesisH32,
        core::ConversionMode::Mode5GenesisH40,
        core::ConversionMode::Mode5GenesisH32Pal,
        core::ConversionMode::Mode5GenesisH40Pal,
        core::ConversionMode::HuC6270Background256,
        core::ConversionMode::HuC6270Background320,
        core::ConversionMode::VicIIHiresCharacter,
        core::ConversionMode::VicIIMulticolorCharacter,
        core::ConversionMode::VicIIHiresBitmap,
        core::ConversionMode::VicIIMulticolorBitmap,
        core::ConversionMode::VicHiresCharacter,
        core::ConversionMode::VicMulticolorCharacter,
    };
    const std::vector<formats::ExportFormat> tableFormats{
        formats::ExportFormat::Raw,
        formats::ExportFormat::Rle,
        formats::ExportFormat::TiFiles,
        formats::ExportFormat::V9t9,
    };
    const std::vector<formats::ExportFormat> bitmapFormats{
        formats::ExportFormat::MsxScreen2,
        formats::ExportFormat::ColecoCvPaint,
        formats::ExportFormat::AdamPowerPaint,
        formats::ExportFormat::AdamHgr,
        formats::ExportFormat::ColecoVisionRom,
        formats::ExportFormat::ExtendedBasicProgram,
        formats::ExportFormat::ExtendedBasicRleProgram,
    };
    for (const auto mode : modes) {
        core::TargetMemoryImage image{.mode = mode};
        for (const auto layout : core::expectedTargetTables(mode)) {
            image.tables.push_back({layout.role,
                                    std::vector<std::uint8_t>(layout.byteSize, 0x5a)});
        }
        for (const auto format : tableFormats) {
            test.expect(formats::isExportApplicable(format, mode),
                        "table formats should apply to every conversion mode");
            const auto manifest = formats::generateExport({
                .format = format,
                .baseName = "layout",
                .target = &image,
            });
            test.expect(static_cast<bool>(manifest),
                        "every valid target layout should support every table format");
            test.expect(manifest.files.size() == image.tables.size(),
                        "table exports should emit one file for every target table");
            std::vector<std::string> names;
            for (const auto& file : manifest.files) names.push_back(file.fileName);
            std::ranges::sort(names);
            test.expect(std::ranges::adjacent_find(names) == names.end(),
                        "table export file names should be unique for every target layout");
            const bool nativeSegaAssets = mode == core::ConversionMode::Mode4Sms192
                || mode == core::ConversionMode::Mode4Sms224
                || mode == core::ConversionMode::Mode4Sms240
                || mode == core::ConversionMode::Mode5GenesisH32
                || mode == core::ConversionMode::Mode5GenesisH40
                || mode == core::ConversionMode::Mode5GenesisH32Pal
                || mode == core::ConversionMode::Mode5GenesisH40Pal;
            if (format == formats::ExportFormat::Raw && nativeSegaAssets) {
                test.expect(findFile(manifest, "LAYOUT.TILES") != nullptr
                                && findFile(manifest, "LAYOUT.MAP") != nullptr
                                && findFile(manifest, "LAYOUT.PAL") != nullptr
                                && findFile(manifest, "LAYOUT.REG") != nullptr,
                            "raw Sega VDP exports should use native asset extensions");
            }
            const bool hucAssets = mode == core::ConversionMode::HuC6270Background256
                || mode == core::ConversionMode::HuC6270Background320;
            if (format == formats::ExportFormat::Raw && hucAssets) {
                test.expect(findFile(manifest, "LAYOUT.TILES") != nullptr
                                && findFile(manifest, "LAYOUT.BAT") != nullptr
                                && findFile(manifest, "LAYOUT.PAL") != nullptr
                                && findFile(manifest, "LAYOUT.REG") != nullptr,
                            "raw HuC6270 exports should use native asset extensions");
            }
            const bool commodoreAssets = mode >= core::ConversionMode::VicIIHiresCharacter
                && mode <= core::ConversionMode::VicMulticolorCharacter;
            if (format == formats::ExportFormat::Raw && commodoreAssets) {
                test.expect((findFile(manifest, "LAYOUT.CHR") != nullptr
                             || findFile(manifest, "LAYOUT.BITMAP") != nullptr)
                                && findFile(manifest, "LAYOUT.SCR") != nullptr
                                && findFile(manifest, "LAYOUT.REG") != nullptr,
                            "raw VIC and VIC-II exports should use native asset extensions");
            }
        }
        test.expect(formats::isExportApplicable(formats::ExportFormat::Png, mode),
                    "PNG should apply to every preview-producing conversion mode");
        const bool bitmapFamily = mode == core::ConversionMode::Bitmap9918
            || mode == core::ConversionMode::GreyscaleBitmap9918
            || mode == core::ConversionMode::BlackAndWhiteBitmap9918
            || mode == core::ConversionMode::BitmapColorOnly9918;
        for (const auto format : bitmapFormats) {
            test.expect(formats::isExportApplicable(format, mode) == bitmapFamily,
                        "machine-specific bitmap formats should use the documented mode matrix");
        }
    }
}

void testStableFormatIds(TestContext& test)
{
    constexpr std::array values{
        formats::ExportFormat::Raw, formats::ExportFormat::Rle,
        formats::ExportFormat::TiFiles, formats::ExportFormat::V9t9,
        formats::ExportFormat::MsxScreen2, formats::ExportFormat::ColecoCvPaint,
        formats::ExportFormat::AdamPowerPaint, formats::ExportFormat::AdamHgr,
        formats::ExportFormat::ColecoVisionRom,
        formats::ExportFormat::ExtendedBasicProgram,
        formats::ExportFormat::ExtendedBasicRleProgram, formats::ExportFormat::Png,
    };
    for (const auto value : values) {
        const auto id = formats::exportFormatId(value);
        test.expect(formats::exportFormat(id) == value,
                    "export format IDs should round-trip through the registry boundary");
    }
    test.expect(!formats::exportFormat("unknown-format").has_value(),
                "unknown export format IDs should be rejected");
}

} // namespace

int main()
{
    TestContext test;
    const auto target = bitmapTarget();
    test.expect(static_cast<bool>(core::validateTargetTables(target)),
                "golden target tables should satisfy the core contract");
    testTableFormats(test, target);
    testMachineFormats(test, target);
    testTemplateFormats(test, target);
    testValidation(test, target);
    testAllTargetLayouts(test);
    testStableFormatIds(test);
    if (test.failures != 0) {
        std::cerr << test.failures << " export-format test(s) failed\n";
        return 1;
    }
    std::cout << "All export-format tests passed\n";
    return 0;
}
