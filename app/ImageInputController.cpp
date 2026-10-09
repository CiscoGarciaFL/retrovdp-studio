#include "ImageInputController.hpp"

#include "ConversionPipeline.hpp"

#include "retrovdp/core/Dithering.hpp"
#include "retrovdp/core/ImageTransform.hpp"
#include "retrovdp/imageio/ExportWriter.hpp"

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QMimeData>
#include <QPointer>
#include <QPainter>
#include <QRawFont>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {

using namespace retrovdp;

constexpr std::size_t maximumDrawingHistoryEntries = 64;
constexpr std::size_t maximumDrawingHistoryBytes = 128U * 1024U * 1024U;
constexpr auto livePreviewFrameInterval = std::chrono::milliseconds(16);
constexpr auto screenImageSelectionMimeType =
    "application/x-retrovdp-screen-image-selection";
constexpr auto sourceImageSelectionMimeType =
    "application/x-retrovdp-source-image-selection";

void trimDrawingHistory(std::vector<std::shared_ptr<core::RgbImage>>& history)
{
    std::size_t totalBytes = 0;
    for (const auto& image : history) {
        if (image) totalBytes += image->bytes().size();
    }
    while (history.size() > 1U
           && (history.size() > maximumDrawingHistoryEntries
               || totalBytes > maximumDrawingHistoryBytes)) {
        if (history.front()) totalBytes -= history.front()->bytes().size();
        history.erase(history.begin());
    }
}

QString dataUrl(const QImage& image)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(encoded.toBase64());
}

QString dataUrl(const core::RgbImage& image)
{
    return dataUrl(imageio::toQImage(image));
}

struct TiArtistGlyph {
    int blocksWide{};
    int blocksHigh{};
    int advance{};
    std::vector<std::uint8_t> patterns;
};

struct TiArtistFont {
    QHash<QChar, TiArtistGlyph> glyphs;
    int lineHeight{};
};

QStringList tiArtistRecords(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("The TI Artist font could not be opened: %1")
                                .arg(file.errorString());
        return {};
    }
    if (file.size() < 0 || file.size() > 4 * 1024 * 1024) {
        if (error) *error = QObject::tr("The TI Artist font file is too large.");
        return {};
    }
    QByteArray bytes = file.readAll();
    constexpr std::array<char, 8> tiFilesSignature{7, 'T', 'I', 'F', 'I', 'L', 'E', 'S'};
    const bool tiFiles = bytes.size() >= 128
        && std::equal(tiFilesSignature.begin(), tiFilesSignature.end(), bytes.begin());
    const bool plainTextHeader = bytes.left(128).contains("FONT")
        || bytes.left(128).contains('\n') || bytes.left(128).contains('\r');
    const bool v9t9 = bytes.size() >= 128 && !plainTextHeader
        && std::all_of(bytes.begin(), bytes.begin() + 10, [](char value) {
               const auto byte = static_cast<unsigned char>(value);
               return byte == ' ' || (byte >= 0x21U && byte <= 0x7eU);
           });
    if (tiFiles || v9t9) bytes = bytes.sliced(128);

    QStringList records;
    if (bytes.contains('\n') || bytes.contains('\r')) {
        records = QString::fromLatin1(bytes).split(
            QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    } else {
        for (qsizetype sectorStart = 0; sectorStart < bytes.size(); sectorStart += 256) {
            const qsizetype sectorEnd = std::min(sectorStart + 256, bytes.size());
            qsizetype cursor = sectorStart;
            while (cursor < sectorEnd) {
                const auto length = static_cast<unsigned char>(bytes[cursor++]);
                if (length == 0xffU || length == 0U) break;
                if (length > 80U || cursor + length > sectorEnd) {
                    records.clear();
                    break;
                }
                records.push_back(QString::fromLatin1(bytes.constData() + cursor, length));
                cursor += length;
            }
            if (records.isEmpty() && sectorStart == 0) break;
        }
    }
    for (QString& record : records) record = record.trimmed();
    records.removeAll(QString{});
    if (records.isEmpty() && error) {
        *error = QObject::tr("The file does not contain readable TI Artist DV80 records.");
    }
    return records;
}

std::optional<std::vector<int>> commaSeparatedIntegers(const QString& record,
                                                       int expectedCount)
{
    const QStringList fields = record.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (fields.size() != expectedCount) return std::nullopt;
    std::vector<int> values;
    values.reserve(static_cast<std::size_t>(expectedCount));
    for (const QString& field : fields) {
        bool ok = false;
        const int value = field.trimmed().toInt(&ok, 0);
        if (!ok || value < 0 || value > 255) return std::nullopt;
        values.push_back(value);
    }
    return values;
}

std::optional<QChar> tiArtistCharacter(const QString& record)
{
    const QString value = record.trimmed();
    if (value.compare(QStringLiteral("SPACE"), Qt::CaseInsensitive) == 0) {
        return QChar(QLatin1Char(' '));
    }
    if (value.size() == 1) return value.front();
    if (value.size() >= 3
        && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
            || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')))) {
        return value.at(1);
    }
    bool ok = false;
    const int code = value.toInt(&ok, 0);
    if (ok && code >= 0 && code <= 255) return QChar(static_cast<char16_t>(code));
    return std::nullopt;
}

std::optional<TiArtistFont> loadTiArtistFont(const QString& path, QString* error = nullptr)
{
    const QStringList records = tiArtistRecords(path, error);
    if (records.isEmpty()) return std::nullopt;
    int cursor = 0;
    while (cursor < records.size()
           && records[cursor].compare(QStringLiteral("FONT:"), Qt::CaseInsensitive) != 0
           && records[cursor].compare(QStringLiteral("FONT"), Qt::CaseInsensitive) != 0) {
        ++cursor;
    }
    if (cursor == records.size()) {
        if (error) *error = QObject::tr("The file is not a TI Artist FONT: file.");
        return std::nullopt;
    }
    ++cursor;
    TiArtistFont font;
    while (cursor < records.size()) {
        if (records[cursor].compare(QStringLiteral("FONT:"), Qt::CaseInsensitive) == 0
            || records[cursor].compare(QStringLiteral("FONT"), Qt::CaseInsensitive) == 0) {
            ++cursor;
            continue;
        }
        const auto character = tiArtistCharacter(records[cursor++]);
        if (!character || cursor >= records.size()) break;
        const auto dimensions = commaSeparatedIntegers(records[cursor++], 3);
        if (!dimensions || (*dimensions)[0] < 1 || (*dimensions)[1] < 1
            || (*dimensions)[0] > 32 || (*dimensions)[1] > 24
            || (*dimensions)[2] < 1) {
            if (error) *error = QObject::tr("A TI Artist glyph has invalid dimensions.");
            return std::nullopt;
        }
        TiArtistGlyph glyph;
        glyph.blocksWide = (*dimensions)[0];
        glyph.blocksHigh = (*dimensions)[1];
        glyph.advance = (*dimensions)[2];
        const int blockCount = glyph.blocksWide * glyph.blocksHigh;
        glyph.patterns.reserve(static_cast<std::size_t>(blockCount) * 8U);
        for (int block = 0; block < blockCount; ++block) {
            if (cursor >= records.size()) {
                if (error) *error = QObject::tr("A TI Artist glyph ends before its pattern data.");
                return std::nullopt;
            }
            const auto pattern = commaSeparatedIntegers(records[cursor++], 8);
            if (!pattern) {
                if (error) *error = QObject::tr("A TI Artist glyph pattern is not eight bytes.");
                return std::nullopt;
            }
            for (int byte : *pattern) glyph.patterns.push_back(
                static_cast<std::uint8_t>(byte));
        }
        font.lineHeight = std::max(font.lineHeight, glyph.blocksHigh * 8);
        font.glyphs.insert(*character, std::move(glyph));
    }
    if (font.glyphs.isEmpty() || font.lineHeight <= 0) {
        if (error) *error = QObject::tr("The TI Artist font contains no usable glyphs.");
        return std::nullopt;
    }
    return font;
}

QImage renderTiArtistText(const TiArtistFont& font,
                          const QString& text,
                          int pixelSize,
                          QColor color)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int spaceAdvance = std::max(1, font.lineHeight / 2);
    int nativeWidth = 1;
    for (const QString& line : lines) {
        int lineWidth = 0;
        for (const QChar character : line) {
            const auto found = font.glyphs.constFind(character);
            lineWidth += found == font.glyphs.cend() ? spaceAdvance : found->advance;
        }
        nativeWidth = std::max(nativeWidth, lineWidth);
    }
    const int nativeHeight = std::max(
        1, static_cast<int>(lines.size()) * font.lineHeight);
    QImage native(nativeWidth, nativeHeight, QImage::Format_RGBA8888);
    native.fill(Qt::transparent);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        int penX = 0;
        for (const QChar character : lines[lineIndex]) {
            auto found = font.glyphs.constFind(character);
            if (found == font.glyphs.cend()) found = font.glyphs.constFind(QLatin1Char('?'));
            if (found == font.glyphs.cend()) {
                penX += spaceAdvance;
                continue;
            }
            const TiArtistGlyph& glyph = *found;
            for (int blockY = 0; blockY < glyph.blocksHigh; ++blockY) {
                for (int blockX = 0; blockX < glyph.blocksWide; ++blockX) {
                    const std::size_t blockOffset = static_cast<std::size_t>(
                        blockY * glyph.blocksWide + blockX) * 8U;
                    for (int row = 0; row < 8; ++row) {
                        const std::uint8_t bits = glyph.patterns[blockOffset
                            + static_cast<std::size_t>(row)];
                        for (int column = 0; column < 8; ++column) {
                            if ((bits & (0x80U >> column)) == 0) continue;
                            const int x = penX + blockX * 8 + column;
                            const int y = lineIndex * font.lineHeight + blockY * 8 + row;
                            if (x >= 0 && x < native.width() && y >= 0 && y < native.height())
                                native.setPixelColor(x, y, color);
                        }
                    }
                }
            }
            penX += glyph.advance;
        }
    }
    const double scale = static_cast<double>(std::clamp(pixelSize, 1, 4096))
        / font.lineHeight;
    return native.scaled(std::max(1, qRound(native.width() * scale)),
                         std::max(1, qRound(native.height() * scale)),
                         Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

std::shared_ptr<core::RgbImage> coreImage(const QImage& image)
{
    auto loaded = imageio::loadClipboardImage(image);
    if (!loaded) return {};
    return std::make_shared<core::RgbImage>(std::move(*loaded.image));
}

QString modeName(core::ConversionMode mode)
{
    const auto name = core::displayMode(mode).displayName;
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QString formatConversionDetails(const core::TargetMemoryImage& target,
                                const core::RgbImage& preview,
                                std::size_t byteCount)
{
    return QObject::tr("%1 — %2×%3 — %4 target bytes")
        .arg(modeName(target.mode))
        .arg(preview.width())
        .arg(preview.height())
        .arg(byteCount);
}

formats::ExportFormat exportFormatForIndex(int index)
{
    constexpr std::array availableFormats{
        formats::ExportFormat::TiFiles,
        formats::ExportFormat::V9t9,
        formats::ExportFormat::Raw,
        formats::ExportFormat::Rle,
        formats::ExportFormat::MsxScreen2,
        formats::ExportFormat::ColecoCvPaint,
        formats::ExportFormat::AdamPowerPaint,
        formats::ExportFormat::AdamHgr,
        formats::ExportFormat::Png,
    };
    return availableFormats[static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(availableFormats.size()) - 1))];
}

core::ConversionResult runConversion(const core::ConversionRequest& request,
                                     core::ScalingFilter scalingFilter,
                                     core::ImageFillMode fillMode,
                                     int horizontalOffset,
                                     int verticalOffset,
                                     core::RgbColor backgroundColor,
                                     std::vector<core::RgbColor> workingColors,
                                     bool powerPaintFraming,
                                     bool sourceAlreadyFramed,
                                     core::ConversionProgressCallback progress)
{
    return appsupport::runConversion(
        request,
        {
            .scalingFilter = scalingFilter,
            .fillMode = fillMode,
            .horizontalOffset = horizontalOffset,
            .verticalOffset = verticalOffset,
            .backgroundColor = backgroundColor,
            .workingPalette = std::move(workingColors),
            .powerPaintFraming = powerPaintFraming,
            .sourceAlreadyFramed = sourceAlreadyFramed,
            .progress = std::move(progress),
        });
}

core::ConversionSettings screenImageConversionSettings(
    core::ConversionSettings settings)
{
    // The Screen Image is already framed and adjusted. Reapply only the active
    // converter's palette and chipset layout rules so repeated applications do
    // not compound source-image corrections or dithering.
    settings.dither = core::DitherMode::None;
    settings.gamma = 1.0;
    settings.maximumColorShiftPercent = 0.0;
    settings.stretchHistogram = false;
    return settings;
}

std::vector<core::RgbColor> decodeF18Palette(std::span<const std::uint8_t> bytes)
{
    std::vector<core::RgbColor> colors;
    for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
        colors.push_back({
            static_cast<std::uint8_t>((bytes[offset] & 0x0fU) * 17U),
            static_cast<std::uint8_t>((bytes[offset + 1U] >> 4U) * 17U),
            static_cast<std::uint8_t>((bytes[offset + 1U] & 0x0fU) * 17U),
        });
    }
    return colors;
}

std::vector<core::RgbColor> decodeTargetPalette(
    const core::TargetMemoryImage& target,
    std::span<const std::uint8_t> bytes)
{
    if (target.profile == core::TargetProfileId::SegaMasterSystem) {
        std::vector<core::RgbColor> colors;
        colors.reserve(bytes.size());
        for (const std::uint8_t value : bytes) {
            colors.push_back({
                static_cast<std::uint8_t>((value & 0x03U) * 85U),
                static_cast<std::uint8_t>(((value >> 2U) & 0x03U) * 85U),
                static_cast<std::uint8_t>(((value >> 4U) & 0x03U) * 85U),
            });
        }
        return colors;
    }
    if (target.profile == core::TargetProfileId::SegaGenesis) {
        std::vector<core::RgbColor> colors;
        colors.reserve(bytes.size() / 2U);
        for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
            const std::uint16_t word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[offset]) << 8U)
                | bytes[offset + 1U]);
            colors.push_back({
                static_cast<std::uint8_t>(((word >> 1U) & 0x07U) * 255U / 7U),
                static_cast<std::uint8_t>(((word >> 5U) & 0x07U) * 255U / 7U),
                static_cast<std::uint8_t>(((word >> 9U) & 0x07U) * 255U / 7U),
            });
        }
        return colors;
    }
    if (target.profile == core::TargetProfileId::HuC6270) {
        std::vector<core::RgbColor> colors;
        colors.reserve(bytes.size() / 2U);
        for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
            const std::uint16_t word = static_cast<std::uint16_t>(
                bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
            colors.push_back({
                static_cast<std::uint8_t>(((word >> 3U) & 0x07U) * 255U / 7U),
                static_cast<std::uint8_t>(((word >> 6U) & 0x07U) * 255U / 7U),
                static_cast<std::uint8_t>((word & 0x07U) * 255U / 7U),
            });
        }
        return colors;
    }
    if (target.profile == core::TargetProfileId::GameBoy) {
        return {{224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}};
    }
    if (target.profile == core::TargetProfileId::GameBoyColor
        || target.profile == core::TargetProfileId::SuperNes) {
        std::vector<core::RgbColor> colors;
        colors.reserve(bytes.size() / 2U);
        for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
            const std::uint16_t word = static_cast<std::uint16_t>(
                bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
            colors.push_back({
                static_cast<std::uint8_t>((word & 0x1fU) * 255U / 31U),
                static_cast<std::uint8_t>(((word >> 5U) & 0x1fU) * 255U / 31U),
                static_cast<std::uint8_t>(((word >> 10U) & 0x1fU) * 255U / 31U),
            });
        }
        return colors;
    }
    if (target.profile == core::TargetProfileId::V9938
        || target.profile == core::TargetProfileId::V9958) {
        std::vector<core::RgbColor> colors;
        for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
            colors.push_back({
                static_cast<std::uint8_t>(((bytes[offset] >> 4U) & 0x07U) * 255U / 7U),
                static_cast<std::uint8_t>((bytes[offset + 1U] & 0x07U) * 255U / 7U),
                static_cast<std::uint8_t>((bytes[offset] & 0x07U) * 255U / 7U),
            });
        }
        return colors;
    }
    return decodeF18Palette(bytes);
}

QString colorName(const core::RgbColor& color)
{
    return QStringLiteral("#%1%2%3")
        .arg(color.red, 2, 16, QLatin1Char('0'))
        .arg(color.green, 2, 16, QLatin1Char('0'))
        .arg(color.blue, 2, 16, QLatin1Char('0'))
        .toUpper();
}

std::vector<core::RgbColor> defaultWorkingColors()
{
    return appsupport::defaultWorkingPalette();
}

core::RgbColor nearestBackgroundColor(
    const QColor& requested,
    std::span<const core::RgbColor> colors)
{
    core::RgbColor nearest = colors.front();
    int nearestDistance = std::numeric_limits<int>::max();
    for (const auto& candidate : colors) {
        const int red = requested.red() - static_cast<int>(candidate.red);
        const int green = requested.green() - static_cast<int>(candidate.green);
        const int blue = requested.blue() - static_cast<int>(candidate.blue);
        const int distance = red * red + green * green + blue * blue;
        if (distance < nearestDistance) {
            nearest = candidate;
            nearestDistance = distance;
        }
    }
    return nearest;
}

std::vector<core::RgbColor> standardSourceSwatch()
{
    return {
        {0, 0, 0}, {64, 64, 64}, {128, 128, 128}, {192, 192, 192}, {255, 255, 255},
        {128, 0, 0}, {255, 0, 0}, {255, 128, 128}, {128, 64, 0}, {255, 128, 0},
        {255, 192, 128}, {128, 128, 0}, {255, 255, 0}, {255, 255, 128}, {0, 128, 0},
        {0, 255, 0}, {128, 255, 128}, {0, 128, 128}, {0, 255, 255}, {128, 255, 255},
        {0, 0, 128}, {0, 0, 255}, {128, 128, 255}, {128, 0, 128}, {255, 0, 255},
        {255, 128, 255}, {64, 32, 0}, {192, 96, 0}, {255, 192, 0}, {96, 64, 32},
        {160, 128, 64}, {224, 192, 128}, {32, 64, 96}, {64, 128, 192}, {128, 192, 224},
    };
}

std::vector<core::RgbColor> standardPcPalette()
{
    // The first two entries remain black and white so the picker keeps its
    // stable foreground/background convention.
    return {
        {0, 0, 0},       {255, 255, 255}, {128, 0, 0},   {0, 128, 0},
        {128, 128, 0},   {0, 0, 128},     {128, 0, 128}, {0, 128, 128},
        {192, 192, 192}, {128, 128, 128}, {255, 0, 0},   {0, 255, 0},
        {255, 255, 0},   {0, 0, 255},     {255, 0, 255}, {0, 255, 255},
    };
}

core::RgbColor darkerTone(core::RgbColor color)
{
    return {static_cast<std::uint8_t>(color.red * 0.72),
            static_cast<std::uint8_t>(color.green * 0.72),
            static_cast<std::uint8_t>(color.blue * 0.72)};
}

core::RgbColor lighterTone(core::RgbColor color)
{
    return {static_cast<std::uint8_t>(color.red + (255 - color.red) * 0.28),
            static_cast<std::uint8_t>(color.green + (255 - color.green) * 0.28),
            static_cast<std::uint8_t>(color.blue + (255 - color.blue) * 0.28)};
}

core::RgbColor middleTone(core::RgbColor color)
{
    return {static_cast<std::uint8_t>((color.red + 128) / 2),
            static_cast<std::uint8_t>((color.green + 128) / 2),
            static_cast<std::uint8_t>((color.blue + 128) / 2)};
}

std::vector<core::RgbColor> pcSpectrum(std::size_t count)
{
    const auto base = standardPcPalette();
    std::vector<core::RgbColor> colors = base;
    if (count >= 32U) {
        for (const auto color : base) colors.push_back(lighterTone(color));
    }
    if (count >= 64U) {
        for (const auto color : base) colors.push_back(darkerTone(color));
        for (const auto color : base) colors.push_back(middleTone(color));
    }
    return colors;
}

QVariantList colorList(const std::vector<core::RgbColor>& colors)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(colors.size()));
    for (const auto& color : colors) {
        result.push_back(QColor(color.red, color.green, color.blue));
    }
    return result;
}

struct SourceColorChoice {
    std::uint32_t rgb{};
    int hue{};
    int saturation{};
    int value{};
    bool neutral{};
};

SourceColorChoice sourceColorChoice(std::uint32_t rgb)
{
    const QColor color(static_cast<int>((rgb >> 16U) & 0xffU),
                       static_cast<int>((rgb >> 8U) & 0xffU),
                       static_cast<int>(rgb & 0xffU));
    return {.rgb = rgb,
            .hue = color.hsvHue(),
            .saturation = color.hsvSaturation(),
            .value = color.value(),
            .neutral = color.hsvSaturation() <= 8};
}

bool sourceColorLess(const SourceColorChoice& left, const SourceColorChoice& right)
{
    if (left.neutral != right.neutral) return left.neutral;
    if (left.neutral) {
        if (left.value != right.value) return left.value < right.value;
        return left.rgb < right.rgb;
    }
    if (left.hue != right.hue) return left.hue < right.hue;
    if (left.value != right.value) return left.value < right.value;
    if (left.saturation != right.saturation) return left.saturation > right.saturation;
    return left.rgb < right.rgb;
}

} // namespace

ImageInputController::ImageInputController(QObject* parent)
    : QObject(parent), workingPalette_(defaultWorkingColors())
{
    debounceTimer_.setSingleShot(true);
    debounceTimer_.setInterval(160);
    connect(&debounceTimer_, &QTimer::timeout, this, &ImageInputController::startConversion);
    undoCoalesceTimer_.setSingleShot(true);
    undoCoalesceTimer_.setInterval(500);
    loadSettings();
    const QString appDataPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    tiArtistLibraryPath_ = QDir(appDataPath).filePath(QStringLiteral("TI Artist"));
    tiArtistFontsPath_ = QDir(tiArtistLibraryPath_).filePath(QStringLiteral("Fonts"));
    QDir().mkpath(tiArtistFontsPath_);
    reloadScreenImageFonts();
}

ImageInputController::~ImageInputController()
{
    jobs_.cancelCurrent();
}

bool ImageInputController::hasConversion() const
{
    return result_.has_value() && result_->succeeded() && result_->preview.has_value()
        && result_->target.has_value();
}

int ImageInputController::conversionMode() const
{
    return static_cast<int>(settings_.mode);
}

int ImageInputController::targetProfile() const
{
    return static_cast<int>(settings_.targetProfile);
}

int ImageInputController::targetWidth() const
{
    return static_cast<int>(core::displayMode(settings_.mode).geometry.width);
}

int ImageInputController::targetHeight() const
{
    return static_cast<int>(core::displayMode(settings_.mode).geometry.height);
}

double ImageInputController::targetPixelAspectRatio() const
{
    const auto ratio = core::displayMode(settings_.mode).geometry.pixelAspectRatio;
    return static_cast<double>(ratio.numerator)
        / static_cast<double>(ratio.denominator);
}

QVariantList ImageInputController::targetProfileNames() const
{
    QVariantList names;
    for (const auto& profile : core::targetProfiles()) {
        if (profile.status != core::TargetProfileStatus::Implemented) continue;
        names.push_back(QString::fromLatin1(profile.displayName));
    }
    return names;
}

QVariantList ImageInputController::availableConversionModeNames() const
{
    QVariantList names;
    for (const auto mode : core::targetProfile(settings_.targetProfile).conversionModes) {
        names.push_back(modeName(mode));
    }
    return names;
}

QVariantList ImageInputController::availableConversionModeValues() const
{
    QVariantList values;
    for (const auto mode : core::targetProfile(settings_.targetProfile).conversionModes) {
        values.push_back(static_cast<int>(mode));
    }
    return values;
}

int ImageInputController::ditherMode() const
{
    return static_cast<int>(settings_.dither);
}

QColor ImageInputController::backgroundColor() const
{
    return QColor(backgroundColor_.red, backgroundColor_.green, backgroundColor_.blue);
}

QColor ImageInputController::foregroundColor() const
{
    return QColor(foregroundColor_.red, foregroundColor_.green, foregroundColor_.blue);
}

QColor ImageInputController::screenImageBackgroundColor() const
{
    return QColor(screenImageBackgroundColor_.red,
                  screenImageBackgroundColor_.green,
                  screenImageBackgroundColor_.blue);
}

QColor ImageInputController::screenImageForegroundColor() const
{
    return QColor(screenImageForegroundColor_.red,
                  screenImageForegroundColor_.green,
                  screenImageForegroundColor_.blue);
}

QVariantList ImageInputController::backgroundPaletteColors() const
{
    QVariantList colors;
    for (const auto& color : workingPalette_) {
        colors.push_back(colorName(color));
    }
    return colors;
}

QVariantList ImageInputController::workingPaletteColors() const
{
    return backgroundPaletteColors();
}

void ImageInputController::refreshSourceColorChoices()
{
    sourceSpectrum16Colors_ = colorList(pcSpectrum(16));
    sourceSpectrum32Colors_ = colorList(pcSpectrum(32));
    sourceSpectrum64Colors_ = colorList(pcSpectrum(64));
    sourceSwatchColors_ = colorList(standardSourceSwatch());
    sourceUsedColors_.clear();
    const core::RgbImage* colorSource = image_ ? image_.get() : screenImage_.get();
    if (colorSource == nullptr) {
        emit sourceColorsChanged();
        return;
    }

    std::unordered_set<std::uint32_t> seenColors;
    std::vector<SourceColorChoice> uniqueColors;
    constexpr std::size_t maximumTrackedColors = 1'000'000;
    seenColors.reserve(std::min(maximumTrackedColors,
                                static_cast<std::size_t>(colorSource->width())
                                    * colorSource->height()));
    const auto collectColors = [&](const core::RgbImage& source) {
        const std::size_t channels = core::bytesPerPixel(source.pixelFormat());
        for (std::uint32_t y = 0; y < source.height(); ++y) {
            const auto row = source.row(y);
            for (std::uint32_t x = 0; x < source.width(); ++x) {
                const auto* pixel = row.data() + static_cast<std::size_t>(x) * channels;
                if (channels == 4 && pixel[3] == 0) continue;
                const std::uint32_t key = (static_cast<std::uint32_t>(pixel[0]) << 16U)
                    | (static_cast<std::uint32_t>(pixel[1]) << 8U) | pixel[2];
                if (seenColors.size() >= maximumTrackedColors) continue;
                if (seenColors.insert(key).second) {
                    uniqueColors.push_back(sourceColorChoice(key));
                }
            }
        }
    };
    collectColors(*colorSource);
    if (image_ && drawingLayer_) collectColors(*drawingLayer_);
    std::sort(uniqueColors.begin(), uniqueColors.end(), sourceColorLess);
    constexpr std::size_t maximumDisplayedColors = 4096;
    const std::size_t displayed = std::min(maximumDisplayedColors, uniqueColors.size());
    sourceUsedColors_.reserve(static_cast<qsizetype>(displayed));
    for (std::size_t index = 0; index < displayed; ++index) {
        const std::size_t sourceIndex = uniqueColors.size() <= maximumDisplayedColors
            ? index
            : index * (uniqueColors.size() - 1) / (displayed - 1);
        const auto key = uniqueColors[sourceIndex].rgb;
        sourceUsedColors_.push_back(QColor(
            static_cast<int>((key >> 16U) & 0xffU),
            static_cast<int>((key >> 8U) & 0xffU),
            static_cast<int>(key & 0xffU)));
    }
    emit sourceColorsChanged();
}

int ImageInputController::perceptualRedWeight() const
{
    return qRound(settings_.perceptualRedWeight * 100.0);
}

int ImageInputController::perceptualGreenWeight() const
{
    return qRound(settings_.perceptualGreenWeight * 100.0);
}

int ImageInputController::perceptualBlueWeight() const
{
    return qRound(settings_.perceptualBlueWeight * 100.0);
}

void ImageInputController::accept(imageio::ImageLoadResult result, QString sourceName)
{
    if (!result) {
        errorMessage_ = result.error;
        statusMessage_.clear();
        emit statusChanged();
        return;
    }

    image_ = std::make_shared<core::RgbImage>(std::move(*result.image));
    sourceStrokeActive_ = false;
    drawingLayer_.reset();
    clearDrawingHistory();
    screenImage_.reset();
    screenImageExportResult_.reset();
    screenImageEdited_ = false;
    clearScreenImageHistory();
    horizontalOffset_ = 0;
    verticalOffset_ = 0;
    sourceName_ = std::move(sourceName);
    sourceDetails_ = tr("%1 — %2×%3 — %4%5")
        .arg(result.metadata.formatName)
        .arg(image_->width())
        .arg(image_->height())
        .arg(result.metadata.colorSpace)
        .arg(result.metadata.animated
                 ? tr(" — first of %1 frames").arg(result.metadata.frameCount)
                 : QString{});
    refreshSourcePreview();
    refreshSourceColorChoices();
    if (sourcePreview_.isEmpty()) {
        errorMessage_ = tr("The decoded image could not be prepared for display.");
        emit statusChanged();
        return;
    }
    errorMessage_.clear();
    statusMessage_ = tr("Image loaded. Preparing preview…");
    convertedPreview_.clear();
    conversionDetails_.clear();
    result_.reset();
    emit statusChanged();
    emit conversionChanged();
    scheduleConversion();
}

void ImageInputController::openUrl(const QUrl& url)
{
    if (!url.isLocalFile()) {
        errorMessage_ = tr("Only local image files can be opened.");
        emit statusChanged();
        return;
    }
    const QString path = url.toLocalFile();
    auto result = imageio::loadImageFile(path);
    if (result) sourcePath_ = QFileInfo(path).absoluteFilePath();
    accept(std::move(result), QFileInfo(path).fileName());
}

void ImageInputController::newScreenImage()
{
    const auto& geometry = core::displayMode(settings_.mode).geometry;
    auto blank = core::RgbImage::createTightlyPacked(
        geometry.width, geometry.height, core::PixelFormat::Rgb888);
    if (!blank) {
        errorMessage_ = tr("A blank Screen Image could not be created.");
        emit statusChanged();
        return;
    }
    std::fill(blank->bytes().begin(), blank->bytes().end(), std::uint8_t{0});

    jobs_.cancelCurrent();
    debounceTimer_.stop();
    conversionPending_ = false;
    busy_ = true;
    statusMessage_ = tr("Creating blank Screen Image…");
    emit conversionChanged();
    emit statusChanged();

    const core::ConversionRequest request{
        .source = std::make_shared<core::RgbImage>(*blank),
        .settings = screenImageConversionSettings(settings_),
        .generation = 0,
        .cancellation = {},
    };
    auto converted = runConversion(
        request, core::ScalingFilter::None, core::ImageFillMode::Fit,
        0, 0, core::RgbColor{}, workingPalette_, false, true, {});
    busy_ = false;
    if (!converted.succeeded() || !converted.preview || !converted.target) {
        errorMessage_ = tr("The blank Screen Image could not be initialized for the active conversion mode.");
        for (const auto& diagnostic : converted.diagnostics) {
            if (diagnostic.severity == core::DiagnosticSeverity::Error) {
                errorMessage_ = QString::fromStdString(diagnostic.message);
                break;
            }
        }
        statusMessage_.clear();
        emit conversionChanged();
        emit statusChanged();
        return;
    }

    image_.reset();
    framedSource_.reset();
    sourceStrokeActive_ = false;
    drawingLayer_.reset();
    clearDrawingHistory();
    sourcePath_.clear();
    sourceName_ = tr("Untitled");
    sourceDetails_.clear();
    sourcePreview_.clear();
    horizontalOffset_ = 0;
    verticalOffset_ = 0;

    clearScreenImageHistory();
    screenImage_ = std::make_shared<core::RgbImage>(*converted.preview);
    convertedPreview_ = dataUrl(*screenImage_);
    converted.preview = *screenImage_;
    result_ = std::move(converted);
    screenImageExportResult_.reset();
    screenImageEdited_ = false;
    refreshSourceColorChoices();
    const bool backgroundChanged = screenImageBackgroundColor_.red != 0
        || screenImageBackgroundColor_.green != 0
        || screenImageBackgroundColor_.blue != 0;
    screenImageBackgroundColor_ = {};

    const std::size_t byteCount = std::accumulate(
        result_->target->tables.begin(), result_->target->tables.end(), std::size_t{},
        [](std::size_t total, const core::TargetMemoryTable& table) {
            return total + table.bytes.size();
        });
    conversionDetails_ = formatConversionDetails(
        *result_->target, *result_->preview, byteCount);
    errorMessage_.clear();
    statusMessage_ = tr("Blank Screen Image created.");
    updatePaletteInspection();
    updateExportSummary();
    emit sourceChanged();
    emit sourceColorsChanged();
    if (backgroundChanged) emit screenImageColorsChanged();
    emit conversionChanged();
    emit exportChanged();
    emit statusChanged();
}

void ImageInputController::reloadSource()
{
    if (sourcePath_.isEmpty()) return;
    openUrl(QUrl::fromLocalFile(sourcePath_));
}

void ImageInputController::pasteClipboard()
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr || clipboard->mimeData() == nullptr
        || !clipboard->mimeData()->hasImage()) {
        errorMessage_ = tr("The clipboard does not contain an image.");
        emit statusChanged();
        return;
    }
    auto result = imageio::loadClipboardImage(clipboard->image());
    if (result) sourcePath_.clear();
    accept(std::move(result), tr("Clipboard image"));
}

void ImageInputController::ensureDrawingLayer()
{
    const auto& geometry = core::displayMode(settings_.mode).geometry;
    const std::uint32_t width = framedSource_ ? framedSource_->width() : geometry.width;
    const std::uint32_t height = framedSource_ ? framedSource_->height() : geometry.height;
    if (drawingLayer_ && drawingLayer_->width() == width
        && drawingLayer_->height() == height) {
        return;
    }
    if (drawingLayer_) {
        const core::ImageTransformOptions options{
            .targetWidth = width,
            .targetHeight = height,
            .filter = core::ScalingFilter::None,
            .fillMode = core::ImageFillMode::Fit,
            .backgroundAlpha = 0,
        };
        auto transformed = core::transformImage(*drawingLayer_, options);
        if (transformed) {
            drawingLayer_ = std::make_shared<core::RgbImage>(
                std::move(*transformed.image));
            return;
        }
    }
    auto layer = core::RgbImage::createTightlyPacked(
        width, height, core::PixelFormat::Rgba8888);
    if (!layer) return;
    std::fill(layer->bytes().begin(), layer->bytes().end(), 0);
    drawingLayer_ = std::make_shared<core::RgbImage>(std::move(*layer));
}

void ImageInputController::compositeDrawingLayer(core::RgbImage& canvas) const
{
    if (!drawingLayer_ || drawingLayer_->width() != canvas.width()
        || drawingLayer_->height() != canvas.height()) {
        return;
    }
    const std::size_t canvasChannels = core::bytesPerPixel(canvas.pixelFormat());
    for (std::uint32_t y = 0; y < canvas.height(); ++y) {
        auto canvasRow = canvas.row(y);
        const auto layerRow = drawingLayer_->row(y);
        for (std::uint32_t x = 0; x < canvas.width(); ++x) {
            const std::size_t canvasOffset = static_cast<std::size_t>(x) * canvasChannels;
            const std::size_t layerOffset = static_cast<std::size_t>(x) * 4U;
            const double layerAlpha = layerRow[layerOffset + 3U] / 255.0;
            if (layerAlpha <= 0.0) continue;
            if (layerAlpha >= 1.0) {
                std::copy_n(layerRow.begin() + static_cast<std::ptrdiff_t>(layerOffset),
                            3, canvasRow.begin()
                                + static_cast<std::ptrdiff_t>(canvasOffset));
                if (canvasChannels == 4U) canvasRow[canvasOffset + 3U] = 255;
                continue;
            }

            const double canvasAlpha = canvasChannels == 4U
                ? canvasRow[canvasOffset + 3U] / 255.0 : 1.0;
            const double outputAlpha = layerAlpha
                + canvasAlpha * (1.0 - layerAlpha);
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const double premultiplied = layerRow[layerOffset + channel] * layerAlpha
                    + canvasRow[canvasOffset + channel] * canvasAlpha
                        * (1.0 - layerAlpha);
                canvasRow[canvasOffset + channel] = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(std::lround(
                                   outputAlpha > 0.0
                                       ? premultiplied / outputAlpha : 0.0)),
                               0, 255));
            }
            if (canvasChannels == 4U) {
                canvasRow[canvasOffset + 3U] = static_cast<std::uint8_t>(
                    std::clamp(static_cast<int>(std::lround(outputAlpha * 255.0)),
                               0, 255));
            }
        }
    }
}

void ImageInputController::refreshSourcePreview()
{
    framedSource_.reset();
    sourcePreview_.clear();
    if (image_) {
        const auto& geometry = core::displayMode(settings_.mode).geometry;
        const core::ImageTransformOptions options{
            .targetWidth = powerPaintFraming_ ? 240U : geometry.width,
            .targetHeight = powerPaintFraming_ ? 160U : geometry.height,
            .filter = scalingFilter_,
            .fillMode = fillMode_,
            .horizontalOffset = horizontalOffset_,
            .verticalOffset = verticalOffset_,
            .backgroundRed = backgroundColor_.red,
            .backgroundGreen = backgroundColor_.green,
            .backgroundBlue = backgroundColor_.blue,
        };
        auto transformed = core::transformImage(*image_, options);
        if (transformed) {
            if (powerPaintFraming_) {
                auto padded = core::RgbImage::createTightlyPacked(
                    geometry.width, geometry.height, transformed.image->pixelFormat());
                if (padded) {
                    std::fill(padded->bytes().begin(), padded->bytes().end(), 0);
                    if (padded->pixelFormat() == core::PixelFormat::Rgba8888) {
                        for (std::size_t offset = 3; offset < padded->bytes().size(); offset += 4) {
                            padded->bytes()[offset] = 255;
                        }
                    }
                    for (std::uint32_t y = 0; y < 160; ++y) {
                        const auto sourceRow = transformed.image->row(y);
                        auto destinationRow = padded->row(y);
                        std::copy_n(sourceRow.begin(), transformed.image->minimumRowBytes(),
                                    destinationRow.begin());
                    }
                    transformed.image = std::move(padded);
                }
            }
            if (drawingLayer_) ensureDrawingLayer();
            compositeDrawingLayer(*transformed.image);
            framedSource_ = std::move(*transformed.image);
            sourcePreview_ = dataUrl(*framedSource_);
        }
    }
    emit sourceChanged();
}

void ImageInputController::scheduleConversion()
{
    jobs_.cancelCurrent();
    debounceTimer_.stop();
    if (!image_) return;
    if (!autoUpdate_) {
        busy_ = false;
        conversionPending_ = true;
        statusMessage_ = tr("Changes are ready. Select Update to convert.");
        emit conversionChanged();
        emit statusChanged();
        return;
    }
    conversionPending_ = false;
    busy_ = true;
    statusMessage_ = tr("Updating preview…");
    emit conversionChanged();
    emit statusChanged();
    debounceTimer_.start();
}

void ImageInputController::startConversion()
{
    if (!image_) return;
    if (!framedSource_) refreshSourcePreview();
    if (!framedSource_) return;
    auto preparedSource = std::make_shared<core::RgbImage>(*framedSource_);
    auto request = jobs_.begin(std::move(preparedSource), settings_);
    const auto scalingFilter = scalingFilter_;
    const auto fillMode = fillMode_;
    const int horizontalOffset = horizontalOffset_;
    const int verticalOffset = verticalOffset_;
    const auto backgroundColor = backgroundColor_;
    const auto workingPalette = workingPalette_;
    const bool powerPaintFraming = powerPaintFraming_;
    QPointer<ImageInputController> guarded(this);
    core::ConversionProgressCallback progress;
    if (livePreview_) {
        if (auto emptyPreview = core::RgbImage::createTightlyPacked(
                framedSource_->width(), framedSource_->height(),
                framedSource_->pixelFormat())) {
            convertedPreview_ = dataUrl(*emptyPreview);
            statusMessage_ = tr("Converting… 0%");
            emit conversionChanged();
            emit statusChanged();
        }
        const std::uint64_t generation = request.generation;
        progress = [guarded, generation](const core::RgbImage& preview,
                                         std::uint32_t completedRows,
                                         std::uint32_t totalRows) {
            const QString previewUrl = dataUrl(preview);
            const int percent = totalRows == 0
                ? 0
                : static_cast<int>(completedRows * 100U / totalRows);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [guarded, generation, previewUrl, percent] {
                    if (!guarded || !guarded->livePreview_ || !guarded->busy_
                        || !guarded->jobs_.isCurrent(generation)) {
                        return;
                    }
                    guarded->convertedPreview_ = previewUrl;
                    guarded->statusMessage_ = tr("Converting… %1%").arg(percent);
                    emit guarded->conversionChanged();
                    emit guarded->statusChanged();
                },
                Qt::QueuedConnection);
            // A converter can produce every partial frame before Qt reaches its
            // next render pass. Pace live-preview delivery to the display cadence
            // so fast target modes visibly publish their completed rows too.
            std::this_thread::sleep_for(livePreviewFrameInterval);
        };
    }
    QThreadPool::globalInstance()->start(
        [guarded, request = std::move(request), scalingFilter, fillMode, horizontalOffset,
         verticalOffset, backgroundColor, workingPalette, powerPaintFraming,
         progress = std::move(progress)]() mutable {
            auto result = runConversion(request,
                                        scalingFilter,
                                        fillMode,
                                        horizontalOffset,
                                        verticalOffset,
                                        backgroundColor,
                                        std::move(workingPalette),
                                        powerPaintFraming,
                                        true,
                                        std::move(progress));
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [guarded, request = std::move(request), result = std::move(result)]() mutable {
                    if (guarded) guarded->publishConversion(request, std::move(result));
                },
                Qt::QueuedConnection);
        });
}

void ImageInputController::publishConversion(const core::ConversionRequest& request,
                                             core::ConversionResult result)
{
    result = jobs_.finalize(request, std::move(result));
    if (!jobs_.accepts(result)) return;

    busy_ = false;
    if (!result.succeeded() || !result.preview || !result.target) {
        errorMessage_ = tr("Conversion failed.");
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.severity == core::DiagnosticSeverity::Error) {
                errorMessage_ = QString::fromStdString(diagnostic.message);
                break;
            }
        }
        statusMessage_.clear();
        result_.reset();
        screenImage_.reset();
        screenImageExportResult_.reset();
        screenImageEdited_ = false;
        clearScreenImageHistory();
        convertedPreview_.clear();
        conversionDetails_.clear();
    } else {
        screenImage_ = std::make_shared<core::RgbImage>(*result.preview);
        screenImageExportResult_.reset();
        screenImageEdited_ = false;
        clearScreenImageHistory();
        convertedPreview_ = dataUrl(*screenImage_);
        const std::size_t byteCount = std::accumulate(
            result.target->tables.begin(), result.target->tables.end(), std::size_t{},
            [](std::size_t total, const core::TargetMemoryTable& table) {
                return total + table.bytes.size();
            });
        conversionDetails_ = formatConversionDetails(
            *result.target, *result.preview, byteCount);
        errorMessage_.clear();
        statusMessage_ = tr("Preview is current.");
        result_ = std::move(result);
        result_->preview = *screenImage_;
        updatePaletteInspection();
        updateExportSummary();
    }
    emit conversionChanged();
    emit exportChanged();
    emit statusChanged();
}

void ImageInputController::updatePaletteInspection()
{
    paletteColors_.clear();
    palettePreview_.clear();
    if (!hasConversion()) return;

    std::vector<core::RgbColor> colors;
    const auto& target = *result_->target;
    const auto scanline = std::ranges::find(
        target.tables, core::TargetTableRole::ScanlinePalettes,
        &core::TargetMemoryTable::role);
    if (scanline != target.tables.end() && scanline->bytes.size() == 6144U) {
        QImage visualization(16, 192, QImage::Format_RGB888);
        for (int row = 0; row < 192; ++row) {
            const auto rowColors = decodeF18Palette(
                std::span(scanline->bytes).subspan(static_cast<std::size_t>(row) * 32U, 32U));
            if (row == 0) colors = rowColors;
            for (int column = 0; column < 16; ++column) {
                const auto& color = rowColors[static_cast<std::size_t>(column)];
                visualization.setPixelColor(column, row,
                                            QColor(color.red, color.green, color.blue));
            }
        }
        palettePreview_ = dataUrl(visualization);
    } else {
        const auto fixed = std::ranges::find(
            target.tables, core::TargetTableRole::Palette, &core::TargetMemoryTable::role);
        if (fixed != target.tables.end()) {
            colors = decodeTargetPalette(target, fixed->bytes);
        } else if (target.palette) {
            colors.assign(target.palette->colors().begin(), target.palette->colors().end());
        }
    }
    for (const auto& color : colors) paletteColors_.push_back(colorName(color));
}

QString ImageInputController::exportBaseName() const
{
    QString base = QFileInfo(sourceName_).completeBaseName();
    if (base.isEmpty()) base = QStringLiteral("image");
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return base;
}

formats::GeneratedFileManifest ImageInputController::exportManifest() const
{
    if (!hasConversion()) {
        formats::GeneratedFileManifest manifest;
        manifest.format = exportFormatForIndex(exportFormat_);
        manifest.error = formats::ExportError::MissingTarget;
        manifest.message = "Complete a conversion before exporting.";
        return manifest;
    }
    const std::string baseName = exportBaseName().toStdString();
    const auto format = exportFormatForIndex(exportFormat_);
    const core::TargetMemoryImage* exportTarget = &*result_->target;
    if (screenImageEdited_ && screenImage_ && format != formats::ExportFormat::Png) {
        if (!screenImageExportResult_) {
            const auto exportSettings = screenImageConversionSettings(settings_);
            const core::ConversionRequest request{
                .source = std::make_shared<core::RgbImage>(*screenImage_),
                .settings = exportSettings,
                .generation = 0,
                .cancellation = {},
            };
            screenImageExportResult_ = runConversion(
                request, core::ScalingFilter::None, core::ImageFillMode::Fit,
                0, 0, screenImageBackgroundColor_, workingPalette_, false,
                screenImage_->width() == core::displayMode(exportSettings.mode).geometry.width
                    && screenImage_->height()
                        == core::displayMode(exportSettings.mode).geometry.height,
                {});
        }
        if (!screenImageExportResult_->succeeded()
            || !screenImageExportResult_->target) {
            formats::GeneratedFileManifest manifest;
            manifest.format = format;
            manifest.error = formats::ExportError::MissingTarget;
            manifest.message = "The edited Screen Image could not be rebuilt as target data.";
            return manifest;
        }
        exportTarget = &*screenImageExportResult_->target;
    }
    const formats::ExportRequest request{
        .format = format,
        .baseName = baseName,
        .target = exportTarget,
        .preview = &*result_->preview,
    };
    return format == formats::ExportFormat::Png ? imageio::generatePngExport(request)
                                                 : formats::generateExport(request);
}

void ImageInputController::updateExportSummary()
{
    if (!hasConversion()) {
        outputSummary_ = tr("Load an image to see the generated files.");
        emit exportChanged();
        return;
    }
    const auto manifest = exportManifest();
    if (!manifest) {
        outputSummary_ = tr("Unavailable: %1").arg(QString::fromStdString(manifest.message));
        emit exportChanged();
        return;
    }
    QStringList lines;
    std::size_t total = 0;
    for (const auto& file : manifest.files) {
        total += file.bytes.size();
        lines.push_back(tr("%1 — %2 bytes")
                            .arg(QString::fromStdString(file.fileName))
                            .arg(file.bytes.size()));
    }
    lines.push_back(tr("Total: %1 file(s), %2 bytes").arg(manifest.files.size()).arg(total));
    outputSummary_ = lines.join(QLatin1Char('\n'));
    emit exportChanged();
}

void ImageInputController::exportToDirectory(const QUrl& directory)
{
    if (!directory.isLocalFile()) {
        errorMessage_ = tr("Choose a local export folder.");
        emit statusChanged();
        return;
    }
    auto manifest = exportManifest();
    if (!manifest) {
        errorMessage_ = QString::fromStdString(manifest.message);
        emit statusChanged();
        return;
    }
    const QString path = directory.toLocalFile();
    const auto written = imageio::writeExportManifest(path, manifest);
    if (written.status == imageio::ExportWriteStatus::WouldOverwrite) {
        pendingExportDirectory_ = path;
        pendingManifest_ = std::move(manifest);
        overwriteMessage_ = tr("The following files already exist:\n%1")
                                .arg(written.conflicts.join(QLatin1Char('\n')));
        emit exportChanged();
        return;
    }
    if (!written) {
        errorMessage_ = written.error;
    } else {
        errorMessage_.clear();
        statusMessage_ = tr("Exported %1 file(s).").arg(written.paths.size());
    }
    emit statusChanged();
}

void ImageInputController::confirmOverwrite()
{
    if (!pendingManifest_) return;
    const auto written = imageio::writeExportManifest(
        pendingExportDirectory_, *pendingManifest_, true);
    overwriteMessage_.clear();
    pendingManifest_.reset();
    pendingExportDirectory_.clear();
    if (!written) {
        errorMessage_ = written.error;
    } else {
        errorMessage_.clear();
        statusMessage_ = tr("Replaced %1 file(s).").arg(written.paths.size());
    }
    emit exportChanged();
    emit statusChanged();
}

void ImageInputController::cancelOverwrite()
{
    overwriteMessage_.clear();
    pendingManifest_.reset();
    pendingExportDirectory_.clear();
    statusMessage_ = tr("Export cancelled; no files were changed.");
    emit exportChanged();
    emit statusChanged();
}

void ImageInputController::updateConversion()
{
    if (!image_) return;
    jobs_.cancelCurrent();
    debounceTimer_.stop();
    conversionPending_ = false;
    busy_ = true;
    statusMessage_ = tr("Updating preview…");
    emit conversionChanged();
    emit statusChanged();
    startConversion();
}

void ImageInputController::nudgeSource(int horizontal, int vertical)
{
    const int newHorizontal = std::clamp(horizontalOffset_ + horizontal, -256, 256);
    const int newVertical = std::clamp(verticalOffset_ + vertical, -192, 192);
    if (newHorizontal == horizontalOffset_ && newVertical == verticalOffset_) return;
    recordUndo();
    horizontalOffset_ = newHorizontal;
    verticalOffset_ = newVertical;
    settingsWereChanged(true);
}

void ImageInputController::centerSource()
{
    if (horizontalOffset_ == 0 && verticalOffset_ == 0) return;
    recordUndo();
    horizontalOffset_ = 0;
    verticalOffset_ = 0;
    settingsWereChanged(true);
}

void ImageInputController::pickBackgroundColor(double normalizedX, double normalizedY)
{
    if (!framedSource_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    const auto x = std::min<std::uint32_t>(
        framedSource_->width() - 1,
        static_cast<std::uint32_t>(normalizedX * framedSource_->width()));
    const auto y = std::min<std::uint32_t>(
        framedSource_->height() - 1,
        static_cast<std::uint32_t>(normalizedY * framedSource_->height()));
    const auto row = framedSource_->row(y);
    const std::size_t offset = static_cast<std::size_t>(x)
        * core::bytesPerPixel(framedSource_->pixelFormat());
    const QColor picked(row[offset], row[offset + 1], row[offset + 2]);
    const auto selected = nearestBackgroundColor(picked, workingPalette_);
    setBackgroundColor(QColor(selected.red, selected.green, selected.blue));
}

void ImageInputController::pickColor(double normalizedX, double normalizedY, bool foreground)
{
    if (!framedSource_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    const auto x = std::min<std::uint32_t>(
        framedSource_->width() - 1,
        static_cast<std::uint32_t>(normalizedX * framedSource_->width()));
    const auto y = std::min<std::uint32_t>(
        framedSource_->height() - 1,
        static_cast<std::uint32_t>(normalizedY * framedSource_->height()));
    const auto row = framedSource_->row(y);
    const std::size_t offset = static_cast<std::size_t>(x)
        * core::bytesPerPixel(framedSource_->pixelFormat());
    if (foreground) {
        setForegroundColor(QColor(row[offset], row[offset + 1], row[offset + 2]));
    } else {
        setBackgroundColor(QColor(row[offset], row[offset + 1], row[offset + 2]));
    }
}

void ImageInputController::swapSourceColors(double normalizedX, double normalizedY)
{
    if (!image_ || !framedSource_) return;
    if (sourceStrokeActive_) endSourceStroke();
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    const auto pickedX = std::min<std::uint32_t>(
        framedSource_->width() - 1,
        static_cast<std::uint32_t>(normalizedX * framedSource_->width()));
    const auto pickedY = std::min<std::uint32_t>(
        framedSource_->height() - 1,
        static_cast<std::uint32_t>(normalizedY * framedSource_->height()));
    const std::size_t sourceChannels = core::bytesPerPixel(framedSource_->pixelFormat());
    const auto pickedRow = framedSource_->row(pickedY);
    const std::size_t pickedOffset = static_cast<std::size_t>(pickedX) * sourceChannels;
    const core::RgbColor picked{pickedRow[pickedOffset], pickedRow[pickedOffset + 1U],
                                pickedRow[pickedOffset + 2U]};
    const core::RgbColor selected = foregroundColor_;
    if (picked == selected) return;

    jobs_.cancelCurrent();
    debounceTimer_.stop();
    beginDrawingTransaction();
    if (!drawingLayer_) return;
    sourceStrokeTouched_ = false;
    const int firstX = hasSourceSelection() ? sourceSelectionX_ : 0;
    const int firstY = hasSourceSelection() ? sourceSelectionY_ : 0;
    const int lastX = hasSourceSelection()
        ? sourceSelectionX_ + sourceSelectionWidth_
        : static_cast<int>(framedSource_->width());
    const int lastY = hasSourceSelection()
        ? sourceSelectionY_ + sourceSelectionHeight_
        : static_cast<int>(framedSource_->height());
    for (int y = firstY; y < lastY; ++y) {
        const auto sourceRow = framedSource_->row(static_cast<std::uint32_t>(y));
        auto layerRow = drawingLayer_->row(static_cast<std::uint32_t>(y));
        for (int x = firstX; x < lastX; ++x) {
            const std::size_t sourceOffset = static_cast<std::size_t>(x) * sourceChannels;
            const core::RgbColor color{sourceRow[sourceOffset],
                                       sourceRow[sourceOffset + 1U],
                                       sourceRow[sourceOffset + 2U]};
            const core::RgbColor replacement = color == selected ? picked
                : (color == picked ? selected : color);
            if (replacement == color) continue;
            const std::size_t layerOffset = static_cast<std::size_t>(x) * 4U;
            layerRow[layerOffset] = replacement.red;
            layerRow[layerOffset + 1U] = replacement.green;
            layerRow[layerOffset + 2U] = replacement.blue;
            layerRow[layerOffset + 3U] = 255U;
            sourceStrokeTouched_ = true;
        }
    }
    commitDrawingTransaction(sourceStrokeTouched_);
    sourceStrokeTouched_ = false;
    foregroundColor_ = picked;
    saveSettings();
    emit settingsChanged();
    refreshSourcePreview();
    refreshSourceColorChoices();
    scheduleConversion();
}

void ImageInputController::beginSourceStroke(double normalizedX,
                                             double normalizedY,
                                             int diameter,
                                             bool eraser,
                                             bool hardEdges,
                                             bool squareBrush)
{
    if (!image_) return;
    if (sourceStrokeActive_) endSourceStroke();
    jobs_.cancelCurrent();
    debounceTimer_.stop();
    beginDrawingTransaction();
    sourceStrokeActive_ = true;
    sourceStrokeTouched_ = false;
    sourceStrokeX_ = std::clamp(normalizedX, 0.0, 1.0);
    sourceStrokeY_ = std::clamp(normalizedY, 0.0, 1.0);
    sourceStrokeDiameter_ = std::clamp(diameter, 1, 64);
    sourceStrokeEraser_ = eraser;
    sourceStrokeHardEdges_ = hardEdges;
    sourceStrokeSquareBrush_ = squareBrush;
    drawSourceStrokeSegment(sourceStrokeX_, sourceStrokeY_, sourceStrokeX_, sourceStrokeY_);
}

void ImageInputController::continueSourceStroke(double normalizedX, double normalizedY)
{
    if (!sourceStrokeActive_ || !image_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    drawSourceStrokeSegment(sourceStrokeX_, sourceStrokeY_, normalizedX, normalizedY);
    sourceStrokeX_ = normalizedX;
    sourceStrokeY_ = normalizedY;
}

void ImageInputController::continueSourceRay(double normalizedX, double normalizedY)
{
    if (!sourceStrokeActive_ || !image_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    drawSourceStrokeSegment(sourceStrokeX_, sourceStrokeY_, normalizedX, normalizedY);
}

void ImageInputController::endSourceStroke()
{
    if (!sourceStrokeActive_) return;
    sourceStrokeActive_ = false;
    commitDrawingTransaction(sourceStrokeTouched_);
    sourceStrokeTouched_ = false;
    refreshSourcePreview();
    refreshSourceColorChoices();
    scheduleConversion();
}

void ImageInputController::drawSourceShape(double fromNormalizedX,
                                           double fromNormalizedY,
                                           double toNormalizedX,
                                           double toNormalizedY,
                                           int diameter,
                                           bool ellipse,
                                           bool hardEdges,
                                           bool fillBackground,
                                           bool squareBrush)
{
    if (!image_) return;
    if (sourceStrokeActive_) endSourceStroke();
    jobs_.cancelCurrent();
    debounceTimer_.stop();
    beginDrawingTransaction();
    sourceStrokeTouched_ = false;
    sourceStrokeDiameter_ = std::clamp(diameter, 1, 64);
    sourceStrokeEraser_ = false;
    sourceStrokeHardEdges_ = hardEdges;
    sourceStrokeSquareBrush_ = squareBrush;
    fromNormalizedX = std::clamp(fromNormalizedX, 0.0, 1.0);
    fromNormalizedY = std::clamp(fromNormalizedY, 0.0, 1.0);
    toNormalizedX = std::clamp(toNormalizedX, 0.0, 1.0);
    toNormalizedY = std::clamp(toNormalizedY, 0.0, 1.0);

    if (fillBackground) {
        // Paint the complete interior before the outline.  Contracting a separately
        // painted fill can leave a one-pixel seam where pixel-centre rounding differs
        // from the stroke rasterizer; the outline safely covers this full fill.
        fillSourceShape(fromNormalizedX, fromNormalizedY,
                        toNormalizedX, toNormalizedY, ellipse, 0.0);
    }

    if (ellipse) {
        const double centerX = (fromNormalizedX + toNormalizedX) / 2.0;
        const double centerY = (fromNormalizedY + toNormalizedY) / 2.0;
        const double radiusX = std::abs(toNormalizedX - fromNormalizedX) / 2.0;
        const double radiusY = std::abs(toNormalizedY - fromNormalizedY) / 2.0;
        const double pixelRadiusX = radiusX * drawingLayer_->width();
        const double pixelRadiusY = radiusY * drawingLayer_->height();
        constexpr double pi = 3.14159265358979323846;
        const double perimeter = pi * (3.0 * (pixelRadiusX + pixelRadiusY)
            - std::sqrt(std::max(0.0,
                (3.0 * pixelRadiusX + pixelRadiusY)
                    * (pixelRadiusX + 3.0 * pixelRadiusY))));
        const int segmentCount = std::clamp(
            static_cast<int>(std::ceil(perimeter * 2.0)), 32, 4096);
        double previousX = centerX + radiusX;
        double previousY = centerY;
        for (int segment = 1; segment <= segmentCount; ++segment) {
            const double angle = 2.0 * pi * segment / segmentCount;
            const double nextX = centerX + std::cos(angle) * radiusX;
            const double nextY = centerY + std::sin(angle) * radiusY;
            drawSourceStrokeSegment(previousX, previousY, nextX, nextY);
            previousX = nextX;
            previousY = nextY;
        }
    } else {
        drawSourceStrokeSegment(fromNormalizedX, fromNormalizedY,
                                toNormalizedX, fromNormalizedY);
        drawSourceStrokeSegment(toNormalizedX, fromNormalizedY,
                                toNormalizedX, toNormalizedY);
        drawSourceStrokeSegment(toNormalizedX, toNormalizedY,
                                fromNormalizedX, toNormalizedY);
        drawSourceStrokeSegment(fromNormalizedX, toNormalizedY,
                                fromNormalizedX, fromNormalizedY);
    }

    commitDrawingTransaction(sourceStrokeTouched_);
    sourceStrokeTouched_ = false;
    refreshSourcePreview();
    refreshSourceColorChoices();
    scheduleConversion();
}

void ImageInputController::drawSourceLine(double fromNormalizedX,
                                          double fromNormalizedY,
                                          double toNormalizedX,
                                          double toNormalizedY,
                                          int diameter,
                                          bool hardEdges,
                                          bool squareBrush)
{
    if (!image_) return;
    if (sourceStrokeActive_) endSourceStroke();
    jobs_.cancelCurrent();
    debounceTimer_.stop();
    beginDrawingTransaction();
    sourceStrokeTouched_ = false;
    sourceStrokeDiameter_ = std::clamp(diameter, 1, 64);
    sourceStrokeEraser_ = false;
    sourceStrokeHardEdges_ = hardEdges;
    sourceStrokeSquareBrush_ = squareBrush;
    drawSourceStrokeSegment(std::clamp(fromNormalizedX, 0.0, 1.0),
                            std::clamp(fromNormalizedY, 0.0, 1.0),
                            std::clamp(toNormalizedX, 0.0, 1.0),
                            std::clamp(toNormalizedY, 0.0, 1.0));
    commitDrawingTransaction(sourceStrokeTouched_);
    sourceStrokeTouched_ = false;
    refreshSourcePreview();
    refreshSourceColorChoices();
    scheduleConversion();
}

void ImageInputController::beginDrawingTransaction()
{
    if (!image_) return;
    ensureDrawingLayer();
    if (!drawingLayer_) return;
    drawingBeforeImage_ = drawingLayer_;
    drawingLayer_ = std::make_shared<core::RgbImage>(*drawingLayer_);
}

void ImageInputController::commitDrawingTransaction(bool changed)
{
    if (!drawingBeforeImage_) return;
    if (changed) {
        drawingUndoStack_.push_back(std::move(drawingBeforeImage_));
        trimDrawingHistory(drawingUndoStack_);
        drawingRedoStack_.clear();
        emit drawingHistoryChanged();
    } else {
        drawingLayer_ = std::move(drawingBeforeImage_);
    }
}

void ImageInputController::undoDrawing()
{
    cancelSourceFloating();
    if (sourceStrokeActive_) endSourceStroke();
    if (!drawingLayer_ || drawingUndoStack_.empty()) return;
    drawingRedoStack_.push_back(drawingLayer_);
    trimDrawingHistory(drawingRedoStack_);
    drawingLayer_ = std::move(drawingUndoStack_.back());
    drawingUndoStack_.pop_back();
    refreshAfterDrawingHistoryChange();
}

void ImageInputController::redoDrawing()
{
    cancelSourceFloating();
    if (sourceStrokeActive_) endSourceStroke();
    if (!drawingLayer_ || drawingRedoStack_.empty()) return;
    drawingUndoStack_.push_back(drawingLayer_);
    trimDrawingHistory(drawingUndoStack_);
    drawingLayer_ = std::move(drawingRedoStack_.back());
    drawingRedoStack_.pop_back();
    refreshAfterDrawingHistoryChange();
}

void ImageInputController::clearDrawingHistory()
{
    const bool hadHistory = !drawingUndoStack_.empty() || !drawingRedoStack_.empty();
    drawingBeforeImage_.reset();
    drawingUndoStack_.clear();
    drawingRedoStack_.clear();
    resetSourceSelectionState();
    if (hadHistory) emit drawingHistoryChanged();
}

void ImageInputController::refreshAfterDrawingHistoryChange()
{
    sourceStrokeActive_ = false;
    sourceStrokeTouched_ = false;
    drawingBeforeImage_.reset();
    refreshSourcePreview();
    refreshSourceColorChoices();
    scheduleConversion();
    emit drawingHistoryChanged();
}

bool ImageInputController::workingPaletteEditable() const
{
    return core::hasOption(core::displayMode(settings_.mode).options,
        core::ModeOption::WorkingPalette);
}

bool ImageInputController::paletteSelectionAvailable() const
{
    return core::hasOption(core::displayMode(settings_.mode).options,
        core::ModeOption::PaletteSelection);
}

bool ImageInputController::scanlinePaletteSettingsAvailable() const
{
    return core::hasOption(core::displayMode(settings_.mode).options,
        core::ModeOption::ScanlinePalette);
}

bool ImageInputController::multicolorFlickerAvailable() const
{
    return core::hasOption(core::displayMode(settings_.mode).options,
        core::ModeOption::MulticolorFlickerLimit);
}

void ImageInputController::resetSourceSelectionState()
{
    const bool hadSelection = hasSourceSelection();
    const bool hadFloating = sourceFloating();
    sourceSelectionX_ = 0;
    sourceSelectionY_ = 0;
    sourceSelectionWidth_ = 0;
    sourceSelectionHeight_ = 0;
    sourceFloatingImage_.reset();
    sourceFloatingPreview_.clear();
    sourceFloatingMove_ = false;
    sourceFloatingTopLeft_ = false;
    sourceFloatingSourceX_ = 0;
    sourceFloatingSourceY_ = 0;
    if (hadSelection) emit sourceSelectionChanged();
    if (hadFloating) emit sourceFloatingChanged();
}

std::shared_ptr<core::RgbImage> ImageInputController::copySourceRegion(
    int x, int y, int width, int height) const
{
    if (!framedSource_ || width <= 0 || height <= 0 || x < 0 || y < 0
        || x + width > static_cast<int>(framedSource_->width())
        || y + height > static_cast<int>(framedSource_->height())) {
        return {};
    }
    auto region = core::RgbImage::createTightlyPacked(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        framedSource_->pixelFormat());
    if (!region) return {};
    const std::size_t channels = core::bytesPerPixel(framedSource_->pixelFormat());
    const std::size_t rowBytes = static_cast<std::size_t>(width) * channels;
    for (int rowIndex = 0; rowIndex < height; ++rowIndex) {
        const auto sourceRow = framedSource_->row(
            static_cast<std::uint32_t>(y + rowIndex));
        auto destinationRow = region->row(static_cast<std::uint32_t>(rowIndex));
        std::copy_n(sourceRow.begin() + static_cast<std::ptrdiff_t>(x) * channels,
                    rowBytes, destinationRow.begin());
    }
    return std::make_shared<core::RgbImage>(std::move(*region));
}

void ImageInputController::beginSourceFloating(
    std::shared_ptr<core::RgbImage> image, bool movingSelection, bool topLeftAnchor)
{
    if (!image) return;
    sourceFloatingImage_ = std::move(image);
    sourceFloatingPreview_ = dataUrl(*sourceFloatingImage_);
    sourceFloatingMove_ = movingSelection;
    sourceFloatingTopLeft_ = topLeftAnchor;
    sourceFloatingSourceX_ = movingSelection ? sourceSelectionX_ : 0;
    sourceFloatingSourceY_ = movingSelection ? sourceSelectionY_ : 0;
    errorMessage_.clear();
    statusMessage_ = movingSelection
        ? tr("Move source selection ready. Click the source image to place it; Escape cancels.")
        : tr("Source placement ready. Click the source image to place it; Escape cancels.");
    emit sourceFloatingChanged();
    emit statusChanged();
}

void ImageInputController::replaceDrawingLayerWithCanvas(
    const core::RgbImage& canvas)
{
    auto layer = core::RgbImage::createTightlyPacked(
        canvas.width(), canvas.height(), core::PixelFormat::Rgba8888);
    if (!layer) return;
    const std::size_t sourceChannels = core::bytesPerPixel(canvas.pixelFormat());
    for (std::uint32_t y = 0; y < canvas.height(); ++y) {
        const auto sourceRow = canvas.row(y);
        auto destinationRow = layer->row(y);
        for (std::uint32_t x = 0; x < canvas.width(); ++x) {
            const std::size_t sourceOffset = static_cast<std::size_t>(x) * sourceChannels;
            const std::size_t destinationOffset = static_cast<std::size_t>(x) * 4U;
            std::copy_n(sourceRow.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                        3, destinationRow.begin()
                            + static_cast<std::ptrdiff_t>(destinationOffset));
            destinationRow[destinationOffset + 3U] = 255U;
        }
    }
    drawingLayer_ = std::make_shared<core::RgbImage>(std::move(*layer));
}

void ImageInputController::refreshAfterSourceCanvasEdit()
{
    refreshSourcePreview();
    refreshSourceColorChoices();
    scheduleConversion();
}

void ImageInputController::mirrorSourceImage()
{
    if (!framedSource_) return;
    if (sourceStrokeActive_) endSourceStroke();
    core::RgbImage canvas = *framedSource_;
    const std::size_t channels = core::bytesPerPixel(canvas.pixelFormat());
    for (std::uint32_t y = 0; y < canvas.height(); ++y) {
        auto row = canvas.row(y);
        for (std::uint32_t x = 0; x < canvas.width() / 2U; ++x) {
            const std::uint32_t opposite = canvas.width() - 1U - x;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                std::swap(row[static_cast<std::size_t>(x) * channels + channel],
                          row[static_cast<std::size_t>(opposite) * channels + channel]);
            }
        }
    }
    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(canvas);
    commitDrawingTransaction(true);
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::flipSourceImage()
{
    if (!framedSource_) return;
    if (sourceStrokeActive_) endSourceStroke();
    core::RgbImage canvas = *framedSource_;
    for (std::uint32_t y = 0; y < canvas.height() / 2U; ++y) {
        auto top = canvas.row(y);
        auto bottom = canvas.row(canvas.height() - 1U - y);
        std::swap_ranges(top.begin(), top.end(), bottom.begin());
    }
    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(canvas);
    commitDrawingTransaction(true);
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::invertSourceImage()
{
    if (!framedSource_) return;
    if (sourceStrokeActive_) endSourceStroke();
    core::RgbImage canvas = *framedSource_;
    const std::size_t channels = core::bytesPerPixel(canvas.pixelFormat());
    const int firstX = hasSourceSelection() ? sourceSelectionX_ : 0;
    const int firstY = hasSourceSelection() ? sourceSelectionY_ : 0;
    const int lastX = hasSourceSelection()
        ? sourceSelectionX_ + sourceSelectionWidth_ : static_cast<int>(canvas.width());
    const int lastY = hasSourceSelection()
        ? sourceSelectionY_ + sourceSelectionHeight_ : static_cast<int>(canvas.height());
    for (int y = firstY; y < lastY; ++y) {
        auto row = canvas.row(static_cast<std::uint32_t>(y));
        for (int x = firstX; x < lastX; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            row[offset] = static_cast<std::uint8_t>(255U - row[offset]);
            row[offset + 1U] = static_cast<std::uint8_t>(255U - row[offset + 1U]);
            row[offset + 2U] = static_cast<std::uint8_t>(255U - row[offset + 2U]);
        }
    }
    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(canvas);
    commitDrawingTransaction(true);
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::removeSourceImageColor()
{
    if (!framedSource_) return;
    if (sourceStrokeActive_) endSourceStroke();
    core::RgbImage canvas = *framedSource_;
    bool changed = false;
    const std::size_t channels = core::bytesPerPixel(canvas.pixelFormat());
    const int firstX = hasSourceSelection() ? sourceSelectionX_ : 0;
    const int firstY = hasSourceSelection() ? sourceSelectionY_ : 0;
    const int lastX = hasSourceSelection()
        ? sourceSelectionX_ + sourceSelectionWidth_ : static_cast<int>(canvas.width());
    const int lastY = hasSourceSelection()
        ? sourceSelectionY_ + sourceSelectionHeight_ : static_cast<int>(canvas.height());
    for (int y = firstY; y < lastY; ++y) {
        auto row = canvas.row(static_cast<std::uint32_t>(y));
        for (int x = firstX; x < lastX; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            const auto gray = static_cast<std::uint8_t>((
                77U * row[offset] + 150U * row[offset + 1U]
                + 29U * row[offset + 2U] + 128U) >> 8U);
            changed = changed || row[offset] != gray
                || row[offset + 1U] != gray || row[offset + 2U] != gray;
            row[offset] = gray;
            row[offset + 1U] = gray;
            row[offset + 2U] = gray;
        }
    }
    if (!changed) return;
    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(canvas);
    commitDrawingTransaction(true);
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::clearSourceImage()
{
    if (!framedSource_) return;
    core::RgbImage canvas = *framedSource_;
    const std::size_t channels = core::bytesPerPixel(canvas.pixelFormat());
    for (std::uint32_t y = 0; y < canvas.height(); ++y) {
        auto row = canvas.row(y);
        for (std::uint32_t x = 0; x < canvas.width(); ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            row[offset] = backgroundColor_.red;
            row[offset + 1U] = backgroundColor_.green;
            row[offset + 2U] = backgroundColor_.blue;
            if (channels == 4U) row[offset + 3U] = 255U;
        }
    }
    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(canvas);
    commitDrawingTransaction(true);
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::setSourceSelection(double fromNormalizedX,
                                              double fromNormalizedY,
                                              double toNormalizedX,
                                              double toNormalizedY)
{
    if (!framedSource_) return;
    cancelSourceFloating();
    const int canvasWidth = static_cast<int>(framedSource_->width());
    const int canvasHeight = static_cast<int>(framedSource_->height());
    const auto pixel = [](double normalized, int extent) {
        return std::clamp(static_cast<int>(std::floor(
                              std::clamp(normalized, 0.0, 1.0) * extent)),
                          0, extent - 1);
    };
    const int firstX = pixel(fromNormalizedX, canvasWidth);
    const int firstY = pixel(fromNormalizedY, canvasHeight);
    const int secondX = pixel(toNormalizedX, canvasWidth);
    const int secondY = pixel(toNormalizedY, canvasHeight);
    sourceSelectionX_ = std::min(firstX, secondX);
    sourceSelectionY_ = std::min(firstY, secondY);
    sourceSelectionWidth_ = std::abs(secondX - firstX) + 1;
    sourceSelectionHeight_ = std::abs(secondY - firstY) + 1;
    statusMessage_ = tr("Selected %1×%2 source pixels.")
                         .arg(sourceSelectionWidth_)
                         .arg(sourceSelectionHeight_);
    emit sourceSelectionChanged();
    emit statusChanged();
}

void ImageInputController::clearSourceSelection()
{
    const bool hadSelection = hasSourceSelection();
    cancelSourceFloating();
    sourceSelectionX_ = 0;
    sourceSelectionY_ = 0;
    sourceSelectionWidth_ = 0;
    sourceSelectionHeight_ = 0;
    if (hadSelection) emit sourceSelectionChanged();
}

void ImageInputController::beginMoveSourceSelection()
{
    if (!framedSource_ || !hasSourceSelection()) {
        errorMessage_ = tr("Select an area of the source image before moving it.");
        emit statusChanged();
        return;
    }
    auto region = copySourceRegion(sourceSelectionX_, sourceSelectionY_,
                                   sourceSelectionWidth_, sourceSelectionHeight_);
    if (!region) {
        errorMessage_ = tr("The selected source-image area could not be moved.");
        emit statusChanged();
        return;
    }
    beginSourceFloating(std::move(region), true);
}

void ImageInputController::placeSourceFloating(double normalizedCenterX,
                                               double normalizedCenterY)
{
    if (!framedSource_ || !sourceFloatingImage_) return;
    const int canvasWidth = static_cast<int>(framedSource_->width());
    const int canvasHeight = static_cast<int>(framedSource_->height());
    const int floatingWidth = static_cast<int>(sourceFloatingImage_->width());
    const int floatingHeight = static_cast<int>(sourceFloatingImage_->height());
    const double anchorX = std::clamp(normalizedCenterX, 0.0, 1.0) * canvasWidth;
    const double anchorY = std::clamp(normalizedCenterY, 0.0, 1.0) * canvasHeight;
    const int destinationX = std::clamp(
        qRound(anchorX - (sourceFloatingTopLeft_ ? 0.0 : floatingWidth / 2.0)),
        0, std::max(0, canvasWidth - floatingWidth));
    const int destinationY = std::clamp(
        qRound(anchorY - (sourceFloatingTopLeft_ ? 0.0 : floatingHeight / 2.0)),
        0, std::max(0, canvasHeight - floatingHeight));
    if (sourceFloatingMove_ && destinationX == sourceFloatingSourceX_
        && destinationY == sourceFloatingSourceY_) {
        cancelSourceFloating();
        return;
    }

    core::RgbImage canvas = *framedSource_;
    const auto floatingImage = sourceFloatingImage_;
    const bool movingSelection = sourceFloatingMove_;
    const std::size_t destinationChannels = core::bytesPerPixel(canvas.pixelFormat());
    if (movingSelection) {
        for (int y = 0; y < floatingHeight; ++y) {
            auto row = canvas.row(static_cast<std::uint32_t>(sourceFloatingSourceY_ + y));
            for (int x = 0; x < floatingWidth; ++x) {
                const std::size_t offset = static_cast<std::size_t>(
                    sourceFloatingSourceX_ + x) * destinationChannels;
                row[offset] = backgroundColor_.red;
                row[offset + 1U] = backgroundColor_.green;
                row[offset + 2U] = backgroundColor_.blue;
                if (destinationChannels == 4U) row[offset + 3U] = 255U;
            }
        }
    }
    const std::size_t sourceChannels = core::bytesPerPixel(floatingImage->pixelFormat());
    for (int y = 0; y < floatingHeight; ++y) {
        const auto sourceRow = floatingImage->row(static_cast<std::uint32_t>(y));
        auto destinationRow = canvas.row(static_cast<std::uint32_t>(destinationY + y));
        for (int x = 0; x < floatingWidth; ++x) {
            const std::size_t sourceOffset = static_cast<std::size_t>(x) * sourceChannels;
            const std::size_t destinationOffset = static_cast<std::size_t>(
                destinationX + x) * destinationChannels;
            const int alpha = sourceChannels == 4U ? sourceRow[sourceOffset + 3U] : 255;
            if (alpha == 0) continue;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                destinationRow[destinationOffset + channel] = alpha == 255
                    ? sourceRow[sourceOffset + channel]
                    : static_cast<std::uint8_t>((sourceRow[sourceOffset + channel] * alpha
                        + destinationRow[destinationOffset + channel] * (255 - alpha)
                        + 127) / 255);
            }
            if (destinationChannels == 4U) destinationRow[destinationOffset + 3U] = 255U;
        }
    }

    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(canvas);
    commitDrawingTransaction(true);
    sourceSelectionX_ = destinationX;
    sourceSelectionY_ = destinationY;
    sourceSelectionWidth_ = floatingWidth;
    sourceSelectionHeight_ = floatingHeight;
    sourceFloatingImage_.reset();
    sourceFloatingPreview_.clear();
    sourceFloatingMove_ = false;
    sourceFloatingTopLeft_ = false;
    sourceFloatingSourceX_ = 0;
    sourceFloatingSourceY_ = 0;
    emit sourceFloatingChanged();
    emit sourceSelectionChanged();
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::cancelSourceFloating()
{
    if (!sourceFloatingImage_) return;
    sourceFloatingImage_.reset();
    sourceFloatingPreview_.clear();
    sourceFloatingMove_ = false;
    sourceFloatingTopLeft_ = false;
    sourceFloatingSourceX_ = 0;
    sourceFloatingSourceY_ = 0;
    statusMessage_ = tr("Source-image placement canceled.");
    emit sourceFloatingChanged();
    emit statusChanged();
}

void ImageInputController::paintDrawingPixel(int x,
                                             int y,
                                             core::RgbColor color,
                                             double coverage)
{
    if (!drawingLayer_) return;
    const int drawableWidth = powerPaintFraming_
        ? 240 : static_cast<int>(drawingLayer_->width());
    const int drawableHeight = powerPaintFraming_
        ? 160 : static_cast<int>(drawingLayer_->height());
    if (x < 0 || y < 0 || x >= drawableWidth || y >= drawableHeight) return;
    coverage = std::clamp(coverage, 0.0, 1.0);
    if (coverage <= 0.0) return;

    auto row = drawingLayer_->row(static_cast<std::uint32_t>(y));
    const std::size_t offset = static_cast<std::size_t>(x) * 4U;
    const std::array target{color.red, color.green, color.blue};
    bool changed = false;
    if (coverage >= 1.0) {
        for (std::size_t channel = 0; channel < target.size(); ++channel) {
            changed |= row[offset + channel] != target[channel];
            row[offset + channel] = target[channel];
        }
        changed |= row[offset + 3U] != 255U;
        row[offset + 3U] = 255U;
    } else {
        const double previousAlpha = row[offset + 3U] / 255.0;
        const double outputAlpha = coverage
            + previousAlpha * (1.0 - coverage);
        for (std::size_t channel = 0; channel < target.size(); ++channel) {
            const double premultiplied = target[channel] * coverage
                + row[offset + channel] * previousAlpha * (1.0 - coverage);
            const auto value = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(
                    outputAlpha > 0.0 ? premultiplied / outputAlpha : 0.0)),
                0, 255));
            changed |= row[offset + channel] != value;
            row[offset + channel] = value;
        }
        const auto alpha = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(outputAlpha * 255.0)), 0, 255));
        changed |= row[offset + 3U] != alpha;
        row[offset + 3U] = alpha;
    }
    sourceStrokeTouched_ |= changed;
}

void ImageInputController::fillSourceShape(double fromNormalizedX,
                                           double fromNormalizedY,
                                           double toNormalizedX,
                                           double toNormalizedY,
                                           bool ellipse,
                                           double inset)
{
    if (!drawingLayer_) return;
    const double previewWidth = drawingLayer_->width();
    const double previewHeight = drawingLayer_->height();
    const double fromX = fromNormalizedX * previewWidth;
    const double fromY = fromNormalizedY * previewHeight;
    const double toX = toNormalizedX * previewWidth;
    const double toY = toNormalizedY * previewHeight;
    const double left = std::min(fromX, toX);
    const double right = std::max(fromX, toX);
    const double top = std::min(fromY, toY);
    const double bottom = std::max(fromY, toY);

    if (ellipse) {
        const double centerX = (left + right) / 2.0;
        const double centerY = (top + bottom) / 2.0;
        const double radiusX = (right - left) / 2.0 - inset;
        const double radiusY = (bottom - top) / 2.0 - inset;
        if (radiusX <= 0.0 || radiusY <= 0.0) return;
        const int firstY = static_cast<int>(std::floor(centerY - radiusY));
        const int lastY = static_cast<int>(std::ceil(centerY + radiusY));
        for (int y = firstY; y <= lastY; ++y) {
            const double pixelY = y + 0.5;
            const double normalizedDistance = (pixelY - centerY) / radiusY;
            if (std::abs(normalizedDistance) > 1.0) continue;
            const double halfWidth = radiusX
                * std::sqrt(std::max(0.0,
                                     1.0 - normalizedDistance * normalizedDistance));
            const int firstX = static_cast<int>(std::floor(centerX - halfWidth));
            const int lastX = static_cast<int>(std::ceil(centerX + halfWidth));
            for (int x = firstX; x <= lastX; ++x) {
                if (x + 0.5 >= centerX - halfWidth
                    && x + 0.5 <= centerX + halfWidth) {
                    paintDrawingPixel(x, y, backgroundColor_, 1.0);
                }
            }
        }
        return;
    }

    const double innerLeft = left + inset;
    const double innerRight = right - inset;
    const int firstY = static_cast<int>(std::floor(top + inset));
    const int lastY = static_cast<int>(std::ceil(bottom - inset));
    if (innerLeft > innerRight || firstY > lastY) return;
    for (int y = firstY; y <= lastY; ++y) {
        if (y + 0.5 < top + inset || y + 0.5 > bottom - inset) continue;
        const int firstX = static_cast<int>(std::floor(innerLeft));
        const int lastX = static_cast<int>(std::ceil(innerRight));
        for (int x = firstX; x <= lastX; ++x) {
            if (x + 0.5 >= innerLeft && x + 0.5 <= innerRight) {
                paintDrawingPixel(x, y, backgroundColor_, 1.0);
            }
        }
    }
}

void ImageInputController::drawSourceStrokeSegment(double fromNormalizedX,
                                                   double fromNormalizedY,
                                                   double toNormalizedX,
                                                   double toNormalizedY)
{
    if (!drawingLayer_) return;
    const double previewWidth = drawingLayer_->width();
    const double previewHeight = drawingLayer_->height();
    const auto coordinate = [this](double normalized, double extent) {
        const double value = std::clamp(normalized, 0.0, 1.0) * extent;
        if (!sourceStrokeHardEdges_) return value;
        const double pixelOffset = sourceStrokeDiameter_ % 2 == 0 ? 0.0 : 0.5;
        return std::clamp(std::floor(value) + pixelOffset,
                          pixelOffset, extent - 1.0 + pixelOffset);
    };
    const double fromX = coordinate(fromNormalizedX, previewWidth);
    const double fromY = coordinate(fromNormalizedY, previewHeight);
    const double toX = coordinate(toNormalizedX, previewWidth);
    const double toY = coordinate(toNormalizedY, previewHeight);
    const double brushRadius = sourceStrokeDiameter_ / 2.0;
    const double feather = sourceStrokeHardEdges_ ? 0.0 : 1.0;
    const double extent = brushRadius + feather;
    const auto paintColor = sourceStrokeEraser_ ? backgroundColor_ : foregroundColor_;
    const double deltaX = toX - fromX;
    const double deltaY = toY - fromY;
    const double lengthSquared = deltaX * deltaX + deltaY * deltaY;
    const auto brushDistance = [&](double pixelX, double pixelY) {
        if (!sourceStrokeSquareBrush_) {
            double nearestAmount = 0.0;
            if (lengthSquared > 0.0) {
                nearestAmount = std::clamp(
                    ((pixelX - fromX) * deltaX + (pixelY - fromY) * deltaY)
                        / lengthSquared,
                    0.0, 1.0);
            }
            return std::hypot(pixelX - (fromX + nearestAmount * deltaX),
                              pixelY - (fromY + nearestAmount * deltaY));
        }

        // The square brush is the line segment swept by an axis-aligned square.
        // The minimum Chebyshev distance occurs at an endpoint, a coordinate
        // crossing, or where the two absolute coordinate distances are equal.
        const double offsetX = pixelX - fromX;
        const double offsetY = pixelY - fromY;
        double distance = std::min(std::max(std::abs(offsetX), std::abs(offsetY)),
                                   std::max(std::abs(pixelX - toX),
                                            std::abs(pixelY - toY)));
        const auto consider = [&](double amount) {
            amount = std::clamp(amount, 0.0, 1.0);
            distance = std::min(distance,
                std::max(std::abs(offsetX - amount * deltaX),
                         std::abs(offsetY - amount * deltaY)));
        };
        if (deltaX != 0.0) consider(offsetX / deltaX);
        if (deltaY != 0.0) consider(offsetY / deltaY);
        for (const double sign : {-1.0, 1.0}) {
            const double denominator = deltaX - sign * deltaY;
            if (denominator != 0.0)
                consider((offsetX - sign * offsetY) / denominator);
        }
        return distance;
    };
    const int minimumX = static_cast<int>(std::floor(std::min(fromX, toX) - extent));
    const int maximumX = static_cast<int>(std::ceil(std::max(fromX, toX) + extent));
    const int minimumY = static_cast<int>(std::floor(std::min(fromY, toY) - extent));
    const int maximumY = static_cast<int>(std::ceil(std::max(fromY, toY) + extent));
    bool coveredPixel = false;
    for (int y = minimumY; y <= maximumY; ++y) {
        for (int x = minimumX; x <= maximumX; ++x) {
            const double distance = brushDistance(x + 0.5, y + 0.5);
            const double coverage = sourceStrokeHardEdges_
                ? (distance <= brushRadius ? 1.0 : 0.0)
                : std::clamp(brushRadius + 1.0 - distance, 0.0, 1.0);
            if (coverage <= 0.0) continue;
            coveredPixel = true;
            paintDrawingPixel(x, y, paintColor, coverage);
        }
    }
    if (!coveredPixel) {
        paintDrawingPixel(static_cast<int>(std::floor(fromX)),
                          static_cast<int>(std::floor(fromY)),
                          paintColor, 1.0);
    }
}

void ImageInputController::pickScreenImageColor(double normalizedX,
                                                double normalizedY,
                                                bool foreground)
{
    if (!screenImage_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    const auto x = std::min<std::uint32_t>(
        screenImage_->width() - 1,
        static_cast<std::uint32_t>(normalizedX * screenImage_->width()));
    const auto y = std::min<std::uint32_t>(
        screenImage_->height() - 1,
        static_cast<std::uint32_t>(normalizedY * screenImage_->height()));
    const auto row = screenImage_->row(y);
    const std::size_t offset = static_cast<std::size_t>(x)
        * core::bytesPerPixel(screenImage_->pixelFormat());
    const QColor color(row[offset], row[offset + 1], row[offset + 2]);
    if (foreground) setScreenImageForegroundColor(color);
    else setScreenImageBackgroundColor(color);
}

void ImageInputController::swapScreenImageColors(double normalizedX,
                                                 double normalizedY)
{
    if (!screenImage_) return;
    if (screenImageStrokeActive_) endScreenImageStroke();
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    const auto pickedX = std::min<std::uint32_t>(
        screenImage_->width() - 1,
        static_cast<std::uint32_t>(normalizedX * screenImage_->width()));
    const auto pickedY = std::min<std::uint32_t>(
        screenImage_->height() - 1,
        static_cast<std::uint32_t>(normalizedY * screenImage_->height()));
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    const auto pickedRow = screenImage_->row(pickedY);
    const std::size_t pickedOffset = static_cast<std::size_t>(pickedX) * channels;
    const core::RgbColor picked{pickedRow[pickedOffset], pickedRow[pickedOffset + 1U],
                                pickedRow[pickedOffset + 2U]};
    const core::RgbColor selected = screenImageForegroundColor_;
    if (picked == selected) return;

    beginScreenImageTransaction();
    bool changed = false;
    const int firstX = hasScreenImageSelection() ? screenImageSelectionX_ : 0;
    const int firstY = hasScreenImageSelection() ? screenImageSelectionY_ : 0;
    const int lastX = hasScreenImageSelection()
        ? screenImageSelectionX_ + screenImageSelectionWidth_
        : static_cast<int>(screenImage_->width());
    const int lastY = hasScreenImageSelection()
        ? screenImageSelectionY_ + screenImageSelectionHeight_
        : static_cast<int>(screenImage_->height());
    for (int y = firstY; y < lastY; ++y) {
        auto row = screenImage_->row(static_cast<std::uint32_t>(y));
        for (int x = firstX; x < lastX; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            const core::RgbColor color{row[offset], row[offset + 1U], row[offset + 2U]};
            const core::RgbColor replacement = color == selected ? picked
                : (color == picked ? selected : color);
            if (replacement == color) continue;
            row[offset] = replacement.red;
            row[offset + 1U] = replacement.green;
            row[offset + 2U] = replacement.blue;
            changed = true;
        }
    }
    commitScreenImageTransaction(changed);
    screenImageForegroundColor_ = picked;
    emit screenImageColorsChanged();
    if (changed) refreshScreenImage();
}

void ImageInputController::beginScreenImageStroke(double normalizedX,
                                                  double normalizedY,
                                                  int diameter,
                                                  bool eraser,
                                                  bool hardEdges,
                                                  bool squareBrush)
{
    if (!screenImage_) return;
    if (screenImageStrokeActive_) endScreenImageStroke();
    beginScreenImageTransaction();
    screenImageStrokeActive_ = true;
    screenImageStrokeTouched_ = false;
    screenImageStrokeX_ = std::clamp(normalizedX, 0.0, 1.0);
    screenImageStrokeY_ = std::clamp(normalizedY, 0.0, 1.0);
    screenImageStrokeDiameter_ = std::clamp(diameter, 1, 64);
    screenImageStrokeEraser_ = eraser;
    screenImageStrokeHardEdges_ = hardEdges;
    screenImageStrokeSquareBrush_ = squareBrush;
    drawScreenImageStrokeSegment(screenImageStrokeX_, screenImageStrokeY_,
                                 screenImageStrokeX_, screenImageStrokeY_);
}

void ImageInputController::continueScreenImageStroke(double normalizedX,
                                                     double normalizedY)
{
    if (!screenImageStrokeActive_ || !screenImage_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    drawScreenImageStrokeSegment(screenImageStrokeX_, screenImageStrokeY_,
                                 normalizedX, normalizedY);
    screenImageStrokeX_ = normalizedX;
    screenImageStrokeY_ = normalizedY;
}

void ImageInputController::continueScreenImageRay(double normalizedX,
                                                  double normalizedY)
{
    if (!screenImageStrokeActive_ || !screenImage_) return;
    normalizedX = std::clamp(normalizedX, 0.0, 1.0);
    normalizedY = std::clamp(normalizedY, 0.0, 1.0);
    drawScreenImageStrokeSegment(screenImageStrokeX_, screenImageStrokeY_,
                                 normalizedX, normalizedY);
}

void ImageInputController::endScreenImageStroke()
{
    if (!screenImageStrokeActive_) return;
    screenImageStrokeActive_ = false;
    commitScreenImageTransaction(screenImageStrokeTouched_);
    screenImageStrokeTouched_ = false;
    refreshScreenImage();
}

void ImageInputController::drawScreenImageShape(double fromNormalizedX,
                                                double fromNormalizedY,
                                                double toNormalizedX,
                                                double toNormalizedY,
                                                int diameter,
                                                bool ellipse,
                                                bool hardEdges,
                                                bool fillBackground,
                                                bool squareBrush)
{
    if (!screenImage_) return;
    if (screenImageStrokeActive_) endScreenImageStroke();
    beginScreenImageTransaction();
    screenImageStrokeTouched_ = false;
    screenImageStrokeDiameter_ = std::clamp(diameter, 1, 64);
    screenImageStrokeEraser_ = false;
    screenImageStrokeHardEdges_ = hardEdges;
    screenImageStrokeSquareBrush_ = squareBrush;
    fromNormalizedX = std::clamp(fromNormalizedX, 0.0, 1.0);
    fromNormalizedY = std::clamp(fromNormalizedY, 0.0, 1.0);
    toNormalizedX = std::clamp(toNormalizedX, 0.0, 1.0);
    toNormalizedY = std::clamp(toNormalizedY, 0.0, 1.0);

    if (fillBackground) {
        fillScreenImageShape(fromNormalizedX, fromNormalizedY,
                             toNormalizedX, toNormalizedY, ellipse, 0.0);
    }

    if (ellipse) {
        const double centerX = (fromNormalizedX + toNormalizedX) / 2.0;
        const double centerY = (fromNormalizedY + toNormalizedY) / 2.0;
        const double radiusX = std::abs(toNormalizedX - fromNormalizedX) / 2.0;
        const double radiusY = std::abs(toNormalizedY - fromNormalizedY) / 2.0;
        const double pixelRadiusX = radiusX * screenImage_->width();
        const double pixelRadiusY = radiusY * screenImage_->height();
        constexpr double pi = 3.14159265358979323846;
        const double perimeter = pi * (3.0 * (pixelRadiusX + pixelRadiusY)
            - std::sqrt(std::max(0.0,
                (3.0 * pixelRadiusX + pixelRadiusY)
                    * (pixelRadiusX + 3.0 * pixelRadiusY))));
        const int segmentCount = std::clamp(
            static_cast<int>(std::ceil(perimeter * 2.0)), 32, 4096);
        double previousX = centerX + radiusX;
        double previousY = centerY;
        for (int segment = 1; segment <= segmentCount; ++segment) {
            const double angle = 2.0 * pi * segment / segmentCount;
            const double nextX = centerX + std::cos(angle) * radiusX;
            const double nextY = centerY + std::sin(angle) * radiusY;
            drawScreenImageStrokeSegment(previousX, previousY, nextX, nextY);
            previousX = nextX;
            previousY = nextY;
        }
    } else {
        drawScreenImageStrokeSegment(fromNormalizedX, fromNormalizedY,
                                     toNormalizedX, fromNormalizedY);
        drawScreenImageStrokeSegment(toNormalizedX, fromNormalizedY,
                                     toNormalizedX, toNormalizedY);
        drawScreenImageStrokeSegment(toNormalizedX, toNormalizedY,
                                     fromNormalizedX, toNormalizedY);
        drawScreenImageStrokeSegment(fromNormalizedX, toNormalizedY,
                                     fromNormalizedX, fromNormalizedY);
    }

    commitScreenImageTransaction(screenImageStrokeTouched_);
    screenImageStrokeTouched_ = false;
    refreshScreenImage();
}

void ImageInputController::drawScreenImageLine(double fromNormalizedX,
                                               double fromNormalizedY,
                                               double toNormalizedX,
                                               double toNormalizedY,
                                               int diameter,
                                               bool hardEdges,
                                               bool squareBrush)
{
    if (!screenImage_) return;
    if (screenImageStrokeActive_) endScreenImageStroke();
    beginScreenImageTransaction();
    screenImageStrokeTouched_ = false;
    screenImageStrokeDiameter_ = std::clamp(diameter, 1, 64);
    screenImageStrokeEraser_ = false;
    screenImageStrokeHardEdges_ = hardEdges;
    screenImageStrokeSquareBrush_ = squareBrush;
    drawScreenImageStrokeSegment(std::clamp(fromNormalizedX, 0.0, 1.0),
                                 std::clamp(fromNormalizedY, 0.0, 1.0),
                                 std::clamp(toNormalizedX, 0.0, 1.0),
                                 std::clamp(toNormalizedY, 0.0, 1.0));
    commitScreenImageTransaction(screenImageStrokeTouched_);
    screenImageStrokeTouched_ = false;
    refreshScreenImage();
}

void ImageInputController::beginScreenImageTransaction()
{
    if (!screenImage_) return;
    screenImageBefore_ = screenImage_;
    screenImage_ = std::make_shared<core::RgbImage>(*screenImage_);
}

void ImageInputController::commitScreenImageTransaction(bool changed)
{
    if (!screenImageBefore_) return;
    if (changed) {
        screenImageUndoStack_.push_back(std::move(screenImageBefore_));
        trimDrawingHistory(screenImageUndoStack_);
        screenImageRedoStack_.clear();
        emit screenImageHistoryChanged();
    } else {
        screenImage_ = std::move(screenImageBefore_);
    }
}

void ImageInputController::clearScreenImageHistory()
{
    const bool hadHistory = !screenImageUndoStack_.empty()
        || !screenImageRedoStack_.empty();
    screenImageBefore_.reset();
    screenImageStrokeActive_ = false;
    screenImageStrokeTouched_ = false;
    screenImageUndoStack_.clear();
    screenImageRedoStack_.clear();
    resetScreenImageSelectionState();
    if (hadHistory) emit screenImageHistoryChanged();
}

void ImageInputController::resetScreenImageSelectionState()
{
    const bool hadSelection = hasScreenImageSelection();
    const bool hadFloating = screenImageFloating();
    screenImageSelectionX_ = 0;
    screenImageSelectionY_ = 0;
    screenImageSelectionWidth_ = 0;
    screenImageSelectionHeight_ = 0;
    screenImageFloatingImage_.reset();
    screenImageFloatingPreview_.clear();
    screenImageFloatingMove_ = false;
    screenImageFloatingTopLeft_ = false;
    screenImageFloatingSourceX_ = 0;
    screenImageFloatingSourceY_ = 0;
    if (hadSelection) emit screenImageSelectionChanged();
    if (hadFloating) emit screenImageFloatingChanged();
}

std::shared_ptr<core::RgbImage> ImageInputController::copyScreenImageRegion(
    int x, int y, int width, int height) const
{
    if (!screenImage_ || width <= 0 || height <= 0 || x < 0 || y < 0
        || x + width > static_cast<int>(screenImage_->width())
        || y + height > static_cast<int>(screenImage_->height())) {
        return {};
    }
    auto region = core::RgbImage::createTightlyPacked(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        screenImage_->pixelFormat());
    if (!region) return {};
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    const std::size_t rowBytes = static_cast<std::size_t>(width) * channels;
    for (int rowIndex = 0; rowIndex < height; ++rowIndex) {
        const auto sourceRow = screenImage_->row(
            static_cast<std::uint32_t>(y + rowIndex));
        auto destinationRow = region->row(static_cast<std::uint32_t>(rowIndex));
        std::copy_n(sourceRow.begin() + static_cast<std::ptrdiff_t>(x) * channels,
                    rowBytes, destinationRow.begin());
    }
    return std::make_shared<core::RgbImage>(std::move(*region));
}

void ImageInputController::beginScreenImageFloating(
    std::shared_ptr<core::RgbImage> image, bool movingSelection, bool topLeftAnchor)
{
    if (!image) return;
    screenImageFloatingImage_ = std::move(image);
    screenImageFloatingPreview_ = dataUrl(*screenImageFloatingImage_);
    screenImageFloatingMove_ = movingSelection;
    screenImageFloatingTopLeft_ = topLeftAnchor;
    if (movingSelection) {
        screenImageFloatingSourceX_ = screenImageSelectionX_;
        screenImageFloatingSourceY_ = screenImageSelectionY_;
    } else {
        screenImageFloatingSourceX_ = 0;
        screenImageFloatingSourceY_ = 0;
    }
    errorMessage_.clear();
    statusMessage_ = movingSelection
        ? tr("Move selection ready. Click the Screen Image to place it; Escape cancels.")
        : tr("Pasted selection ready. Click the Screen Image to place it; Escape cancels.");
    emit screenImageFloatingChanged();
    emit statusChanged();
}

void ImageInputController::refreshScreenImage()
{
    if (!screenImage_) return;
    convertedPreview_ = dataUrl(*screenImage_);
    if (result_) result_->preview = *screenImage_;
    if (!image_) refreshSourceColorChoices();
    screenImageEdited_ = true;
    screenImageExportResult_.reset();
    statusMessage_ = tr("Screen Image edited.");
    outputSummary_ = tr("Screen Image edited — target data will be rebuilt when exported.");
    emit conversionChanged();
    emit exportChanged();
    emit statusChanged();
}

void ImageInputController::paintScreenImagePixel(int x,
                                                 int y,
                                                 core::RgbColor color,
                                                 double coverage)
{
    if (!screenImage_ || x < 0 || y < 0
        || x >= static_cast<int>(screenImage_->width())
        || y >= static_cast<int>(screenImage_->height())) {
        return;
    }
    coverage = std::clamp(coverage, 0.0, 1.0);
    if (coverage <= 0.0) return;
    auto row = screenImage_->row(static_cast<std::uint32_t>(y));
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    const std::size_t offset = static_cast<std::size_t>(x) * channels;
    const std::array target{color.red, color.green, color.blue};
    bool changed = false;
    for (std::size_t channel = 0; channel < target.size(); ++channel) {
        const auto value = coverage >= 1.0
            ? target[channel]
            : static_cast<std::uint8_t>(std::clamp(
                  static_cast<int>(std::lround(target[channel] * coverage
                      + row[offset + channel] * (1.0 - coverage))), 0, 255));
        changed |= row[offset + channel] != value;
        row[offset + channel] = value;
    }
    if (channels == 4U) {
        changed |= row[offset + 3U] != 255U;
        row[offset + 3U] = 255U;
    }
    screenImageStrokeTouched_ |= changed;
}

void ImageInputController::fillScreenImageShape(double fromNormalizedX,
                                                double fromNormalizedY,
                                                double toNormalizedX,
                                                double toNormalizedY,
                                                bool ellipse,
                                                double inset)
{
    if (!screenImage_) return;
    const double previewWidth = screenImage_->width();
    const double previewHeight = screenImage_->height();
    const double fromX = fromNormalizedX * previewWidth;
    const double fromY = fromNormalizedY * previewHeight;
    const double toX = toNormalizedX * previewWidth;
    const double toY = toNormalizedY * previewHeight;
    const double left = std::min(fromX, toX);
    const double right = std::max(fromX, toX);
    const double top = std::min(fromY, toY);
    const double bottom = std::max(fromY, toY);

    if (ellipse) {
        const double centerX = (left + right) / 2.0;
        const double centerY = (top + bottom) / 2.0;
        const double radiusX = (right - left) / 2.0 - inset;
        const double radiusY = (bottom - top) / 2.0 - inset;
        if (radiusX <= 0.0 || radiusY <= 0.0) return;
        for (int y = static_cast<int>(std::floor(centerY - radiusY));
             y <= static_cast<int>(std::ceil(centerY + radiusY)); ++y) {
            const double normalizedDistance = (y + 0.5 - centerY) / radiusY;
            if (std::abs(normalizedDistance) > 1.0) continue;
            const double halfWidth = radiusX * std::sqrt(std::max(
                0.0, 1.0 - normalizedDistance * normalizedDistance));
            for (int x = static_cast<int>(std::floor(centerX - halfWidth));
                 x <= static_cast<int>(std::ceil(centerX + halfWidth)); ++x) {
                if (x + 0.5 >= centerX - halfWidth
                    && x + 0.5 <= centerX + halfWidth) {
                    paintScreenImagePixel(x, y, screenImageBackgroundColor_, 1.0);
                }
            }
        }
        return;
    }

    const double innerLeft = left + inset;
    const double innerRight = right - inset;
    const int firstY = static_cast<int>(std::floor(top + inset));
    const int lastY = static_cast<int>(std::ceil(bottom - inset));
    if (innerLeft > innerRight || firstY > lastY) return;
    for (int y = firstY; y <= lastY; ++y) {
        if (y + 0.5 < top + inset || y + 0.5 > bottom - inset) continue;
        for (int x = static_cast<int>(std::floor(innerLeft));
             x <= static_cast<int>(std::ceil(innerRight)); ++x) {
            if (x + 0.5 >= innerLeft && x + 0.5 <= innerRight) {
                paintScreenImagePixel(x, y, screenImageBackgroundColor_, 1.0);
            }
        }
    }
}

void ImageInputController::drawScreenImageStrokeSegment(double fromNormalizedX,
                                                        double fromNormalizedY,
                                                        double toNormalizedX,
                                                        double toNormalizedY)
{
    if (!screenImage_) return;
    const double previewWidth = screenImage_->width();
    const double previewHeight = screenImage_->height();
    const auto coordinate = [this](double normalized, double extent) {
        const double value = std::clamp(normalized, 0.0, 1.0) * extent;
        if (!screenImageStrokeHardEdges_) return value;
        const double pixelOffset = screenImageStrokeDiameter_ % 2 == 0 ? 0.0 : 0.5;
        return std::clamp(std::floor(value) + pixelOffset,
                          pixelOffset, extent - 1.0 + pixelOffset);
    };
    const double fromX = coordinate(fromNormalizedX, previewWidth);
    const double fromY = coordinate(fromNormalizedY, previewHeight);
    const double toX = coordinate(toNormalizedX, previewWidth);
    const double toY = coordinate(toNormalizedY, previewHeight);
    const double brushRadius = screenImageStrokeDiameter_ / 2.0;
    const double feather = screenImageStrokeHardEdges_ ? 0.0 : 1.0;
    const double extent = brushRadius + feather;
    const auto paintColor = screenImageStrokeEraser_
        ? screenImageBackgroundColor_ : screenImageForegroundColor_;
    const double deltaX = toX - fromX;
    const double deltaY = toY - fromY;
    const double lengthSquared = deltaX * deltaX + deltaY * deltaY;
    const auto brushDistance = [&](double pixelX, double pixelY) {
        if (!screenImageStrokeSquareBrush_) {
            double nearestAmount = 0.0;
            if (lengthSquared > 0.0) {
                nearestAmount = std::clamp(
                    ((pixelX - fromX) * deltaX + (pixelY - fromY) * deltaY)
                        / lengthSquared,
                    0.0, 1.0);
            }
            return std::hypot(pixelX - (fromX + nearestAmount * deltaX),
                              pixelY - (fromY + nearestAmount * deltaY));
        }

        const double offsetX = pixelX - fromX;
        const double offsetY = pixelY - fromY;
        double distance = std::min(std::max(std::abs(offsetX), std::abs(offsetY)),
                                   std::max(std::abs(pixelX - toX),
                                            std::abs(pixelY - toY)));
        const auto consider = [&](double amount) {
            amount = std::clamp(amount, 0.0, 1.0);
            distance = std::min(distance,
                std::max(std::abs(offsetX - amount * deltaX),
                         std::abs(offsetY - amount * deltaY)));
        };
        if (deltaX != 0.0) consider(offsetX / deltaX);
        if (deltaY != 0.0) consider(offsetY / deltaY);
        for (const double sign : {-1.0, 1.0}) {
            const double denominator = deltaX - sign * deltaY;
            if (denominator != 0.0)
                consider((offsetX - sign * offsetY) / denominator);
        }
        return distance;
    };
    bool coveredPixel = false;
    for (int y = static_cast<int>(std::floor(std::min(fromY, toY) - extent));
         y <= static_cast<int>(std::ceil(std::max(fromY, toY) + extent)); ++y) {
        for (int x = static_cast<int>(std::floor(std::min(fromX, toX) - extent));
             x <= static_cast<int>(std::ceil(std::max(fromX, toX) + extent)); ++x) {
            const double distance = brushDistance(x + 0.5, y + 0.5);
            const double coverage = screenImageStrokeHardEdges_
                ? (distance <= brushRadius ? 1.0 : 0.0)
                : std::clamp(brushRadius + 1.0 - distance, 0.0, 1.0);
            if (coverage <= 0.0) continue;
            coveredPixel = true;
            paintScreenImagePixel(x, y, paintColor, coverage);
        }
    }
    if (!coveredPixel) {
        paintScreenImagePixel(static_cast<int>(std::floor(fromX)),
                              static_cast<int>(std::floor(fromY)),
                              paintColor, 1.0);
    }
}

void ImageInputController::nudgeScreenImage(int horizontal, int vertical)
{
    if (!screenImage_ || (horizontal == 0 && vertical == 0)) return;
    beginScreenImageTransaction();
    const core::RgbImage source = *screenImageBefore_;
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    const std::array fill{screenImageBackgroundColor_.red,
                          screenImageBackgroundColor_.green,
                          screenImageBackgroundColor_.blue};
    for (std::uint32_t y = 0; y < screenImage_->height(); ++y) {
        auto destinationRow = screenImage_->row(y);
        for (std::uint32_t x = 0; x < screenImage_->width(); ++x) {
            const int sourceX = static_cast<int>(x) - horizontal;
            const int sourceY = static_cast<int>(y) - vertical;
            const std::size_t destinationOffset = static_cast<std::size_t>(x) * channels;
            if (sourceX >= 0 && sourceY >= 0
                && sourceX < static_cast<int>(source.width())
                && sourceY < static_cast<int>(source.height())) {
                const auto sourceRow = source.row(static_cast<std::uint32_t>(sourceY));
                const std::size_t sourceOffset = static_cast<std::size_t>(sourceX) * channels;
                std::copy_n(sourceRow.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                            channels, destinationRow.begin()
                                + static_cast<std::ptrdiff_t>(destinationOffset));
            } else {
                std::copy(fill.begin(), fill.end(), destinationRow.begin()
                    + static_cast<std::ptrdiff_t>(destinationOffset));
                if (channels == 4U) destinationRow[destinationOffset + 3U] = 255;
            }
        }
    }
    commitScreenImageTransaction(true);
    refreshScreenImage();
}

void ImageInputController::mirrorScreenImage()
{
    if (!screenImage_) return;
    beginScreenImageTransaction();
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    for (std::uint32_t y = 0; y < screenImage_->height(); ++y) {
        auto row = screenImage_->row(y);
        for (std::uint32_t x = 0; x < screenImage_->width() / 2U; ++x) {
            const std::uint32_t opposite = screenImage_->width() - 1U - x;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                std::swap(row[static_cast<std::size_t>(x) * channels + channel],
                          row[static_cast<std::size_t>(opposite) * channels + channel]);
            }
        }
    }
    commitScreenImageTransaction(true);
    refreshScreenImage();
}

void ImageInputController::flipScreenImage()
{
    if (!screenImage_) return;
    beginScreenImageTransaction();
    for (std::uint32_t y = 0; y < screenImage_->height() / 2U; ++y) {
        auto top = screenImage_->row(y);
        auto bottom = screenImage_->row(screenImage_->height() - 1U - y);
        std::swap_ranges(top.begin(), top.end(), bottom.begin());
    }
    commitScreenImageTransaction(true);
    refreshScreenImage();
}

void ImageInputController::invertScreenImage()
{
    if (!screenImage_) return;
    if (screenImageStrokeActive_) endScreenImageStroke();
    beginScreenImageTransaction();
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    const int firstX = hasScreenImageSelection() ? screenImageSelectionX_ : 0;
    const int firstY = hasScreenImageSelection() ? screenImageSelectionY_ : 0;
    const int lastX = hasScreenImageSelection()
        ? screenImageSelectionX_ + screenImageSelectionWidth_
        : static_cast<int>(screenImage_->width());
    const int lastY = hasScreenImageSelection()
        ? screenImageSelectionY_ + screenImageSelectionHeight_
        : static_cast<int>(screenImage_->height());
    for (int y = firstY; y < lastY; ++y) {
        auto row = screenImage_->row(static_cast<std::uint32_t>(y));
        for (int x = firstX; x < lastX; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            row[offset] = static_cast<std::uint8_t>(255U - row[offset]);
            row[offset + 1U] = static_cast<std::uint8_t>(255U - row[offset + 1U]);
            row[offset + 2U] = static_cast<std::uint8_t>(255U - row[offset + 2U]);
        }
    }
    commitScreenImageTransaction(true);
    refreshScreenImage();
}

void ImageInputController::removeScreenImageColor()
{
    if (!screenImage_) return;
    if (screenImageStrokeActive_) endScreenImageStroke();
    beginScreenImageTransaction();
    bool changed = false;
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    const int firstX = hasScreenImageSelection() ? screenImageSelectionX_ : 0;
    const int firstY = hasScreenImageSelection() ? screenImageSelectionY_ : 0;
    const int lastX = hasScreenImageSelection()
        ? screenImageSelectionX_ + screenImageSelectionWidth_
        : static_cast<int>(screenImage_->width());
    const int lastY = hasScreenImageSelection()
        ? screenImageSelectionY_ + screenImageSelectionHeight_
        : static_cast<int>(screenImage_->height());
    for (int y = firstY; y < lastY; ++y) {
        auto row = screenImage_->row(static_cast<std::uint32_t>(y));
        for (int x = firstX; x < lastX; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            const auto gray = static_cast<std::uint8_t>((
                77U * row[offset] + 150U * row[offset + 1U]
                + 29U * row[offset + 2U] + 128U) >> 8U);
            changed = changed || row[offset] != gray
                || row[offset + 1U] != gray || row[offset + 2U] != gray;
            row[offset] = gray;
            row[offset + 1U] = gray;
            row[offset + 2U] = gray;
        }
    }
    commitScreenImageTransaction(changed);
    if (changed) refreshScreenImage();
}

void ImageInputController::clearScreenImage()
{
    if (!screenImage_) return;
    beginScreenImageTransaction();
    const std::size_t channels = core::bytesPerPixel(screenImage_->pixelFormat());
    for (std::uint32_t y = 0; y < screenImage_->height(); ++y) {
        auto row = screenImage_->row(y);
        for (std::uint32_t x = 0; x < screenImage_->width(); ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * channels;
            row[offset] = screenImageBackgroundColor_.red;
            row[offset + 1U] = screenImageBackgroundColor_.green;
            row[offset + 2U] = screenImageBackgroundColor_.blue;
            if (channels == 4U) row[offset + 3U] = 255;
        }
    }
    commitScreenImageTransaction(true);
    refreshScreenImage();
}

void ImageInputController::setScreenImageSelection(double fromNormalizedX,
                                                   double fromNormalizedY,
                                                   double toNormalizedX,
                                                   double toNormalizedY)
{
    if (!screenImage_) return;
    cancelScreenImageFloating();
    const int canvasWidth = static_cast<int>(screenImage_->width());
    const int canvasHeight = static_cast<int>(screenImage_->height());
    const auto pixelX = [canvasWidth](double normalized) {
        return std::clamp(static_cast<int>(std::floor(
                              std::clamp(normalized, 0.0, 1.0) * canvasWidth)),
                          0, canvasWidth - 1);
    };
    const auto pixelY = [canvasHeight](double normalized) {
        return std::clamp(static_cast<int>(std::floor(
                              std::clamp(normalized, 0.0, 1.0) * canvasHeight)),
                          0, canvasHeight - 1);
    };
    const int firstX = pixelX(fromNormalizedX);
    const int firstY = pixelY(fromNormalizedY);
    const int secondX = pixelX(toNormalizedX);
    const int secondY = pixelY(toNormalizedY);
    screenImageSelectionX_ = std::min(firstX, secondX);
    screenImageSelectionY_ = std::min(firstY, secondY);
    screenImageSelectionWidth_ = std::abs(secondX - firstX) + 1;
    screenImageSelectionHeight_ = std::abs(secondY - firstY) + 1;
    statusMessage_ = tr("Selected %1×%2 pixels.")
                         .arg(screenImageSelectionWidth_)
                         .arg(screenImageSelectionHeight_);
    emit screenImageSelectionChanged();
    emit statusChanged();
}

void ImageInputController::clearScreenImageSelection()
{
    const bool hadSelection = hasScreenImageSelection();
    cancelScreenImageFloating();
    screenImageSelectionX_ = 0;
    screenImageSelectionY_ = 0;
    screenImageSelectionWidth_ = 0;
    screenImageSelectionHeight_ = 0;
    if (hadSelection) emit screenImageSelectionChanged();
}

void ImageInputController::beginMoveScreenImageSelection()
{
    if (!screenImage_ || !hasScreenImageSelection()) {
        errorMessage_ = tr("Select an area of the Screen Image before moving it.");
        emit statusChanged();
        return;
    }
    auto region = copyScreenImageRegion(
        screenImageSelectionX_, screenImageSelectionY_,
        screenImageSelectionWidth_, screenImageSelectionHeight_);
    if (!region) {
        errorMessage_ = tr("The selected Screen Image area could not be moved.");
        emit statusChanged();
        return;
    }
    beginScreenImageFloating(std::move(region), true);
}

void ImageInputController::placeScreenImageFloating(double normalizedCenterX,
                                                    double normalizedCenterY)
{
    if (!screenImage_ || !screenImageFloatingImage_) return;
    const int canvasWidth = static_cast<int>(screenImage_->width());
    const int canvasHeight = static_cast<int>(screenImage_->height());
    const int floatingWidth = static_cast<int>(screenImageFloatingImage_->width());
    const int floatingHeight = static_cast<int>(screenImageFloatingImage_->height());
    const double anchorX = std::clamp(normalizedCenterX, 0.0, 1.0) * canvasWidth;
    const double anchorY = std::clamp(normalizedCenterY, 0.0, 1.0) * canvasHeight;
    const int destinationX = std::clamp(
        qRound(anchorX - (screenImageFloatingTopLeft_ ? 0.0 : floatingWidth / 2.0)),
        0, std::max(0, canvasWidth - floatingWidth));
    const int destinationY = std::clamp(
        qRound(anchorY - (screenImageFloatingTopLeft_ ? 0.0 : floatingHeight / 2.0)),
        0, std::max(0, canvasHeight - floatingHeight));

    if (screenImageFloatingMove_
        && destinationX == screenImageFloatingSourceX_
        && destinationY == screenImageFloatingSourceY_) {
        cancelScreenImageFloating();
        return;
    }

    const auto floatingImage = screenImageFloatingImage_;
    const bool movingSelection = screenImageFloatingMove_;
    beginScreenImageTransaction();
    const std::size_t destinationChannels =
        core::bytesPerPixel(screenImage_->pixelFormat());
    if (movingSelection) {
        for (int y = 0; y < floatingHeight; ++y) {
            auto destinationRow = screenImage_->row(
                static_cast<std::uint32_t>(screenImageFloatingSourceY_ + y));
            for (int x = 0; x < floatingWidth; ++x) {
                const std::size_t offset = static_cast<std::size_t>(
                    screenImageFloatingSourceX_ + x) * destinationChannels;
                destinationRow[offset] = screenImageBackgroundColor_.red;
                destinationRow[offset + 1U] = screenImageBackgroundColor_.green;
                destinationRow[offset + 2U] = screenImageBackgroundColor_.blue;
                if (destinationChannels == 4U) destinationRow[offset + 3U] = 255;
            }
        }
    }

    const std::size_t sourceChannels =
        core::bytesPerPixel(floatingImage->pixelFormat());
    for (int y = 0; y < floatingHeight; ++y) {
        const auto sourceRow = floatingImage->row(static_cast<std::uint32_t>(y));
        auto destinationRow = screenImage_->row(
            static_cast<std::uint32_t>(destinationY + y));
        for (int x = 0; x < floatingWidth; ++x) {
            const std::size_t sourceOffset = static_cast<std::size_t>(x) * sourceChannels;
            const std::size_t destinationOffset = static_cast<std::size_t>(
                destinationX + x) * destinationChannels;
            const int alpha = sourceChannels == 4U ? sourceRow[sourceOffset + 3U] : 255;
            if (alpha == 0) continue;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                destinationRow[destinationOffset + channel] = alpha == 255
                    ? sourceRow[sourceOffset + channel]
                    : static_cast<std::uint8_t>((
                          sourceRow[sourceOffset + channel] * alpha
                          + destinationRow[destinationOffset + channel] * (255 - alpha)
                          + 127) / 255);
            }
            if (destinationChannels == 4U) {
                destinationRow[destinationOffset + 3U] = 255;
            }
        }
    }
    commitScreenImageTransaction(true);

    screenImageSelectionX_ = destinationX;
    screenImageSelectionY_ = destinationY;
    screenImageSelectionWidth_ = floatingWidth;
    screenImageSelectionHeight_ = floatingHeight;
    screenImageFloatingImage_.reset();
    screenImageFloatingPreview_.clear();
    screenImageFloatingMove_ = false;
    screenImageFloatingTopLeft_ = false;
    screenImageFloatingSourceX_ = 0;
    screenImageFloatingSourceY_ = 0;
    emit screenImageFloatingChanged();
    emit screenImageSelectionChanged();
    refreshScreenImage();
}

void ImageInputController::cancelScreenImageFloating()
{
    if (!screenImageFloatingImage_) return;
    screenImageFloatingImage_.reset();
    screenImageFloatingPreview_.clear();
    screenImageFloatingMove_ = false;
    screenImageFloatingTopLeft_ = false;
    screenImageFloatingSourceX_ = 0;
    screenImageFloatingSourceY_ = 0;
    statusMessage_ = tr("Screen Image placement canceled.");
    emit screenImageFloatingChanged();
    emit statusChanged();
}

void ImageInputController::reloadScreenImageFonts()
{
    QVariantList fonts;
    QStringList families = QFontDatabase::families();
    if (families.isEmpty()) families.push_back(QFont().family());
    families.removeDuplicates();
    families.sort(Qt::CaseInsensitive);
    for (const QString& family : families) {
        const QRawFont rawFont = QRawFont::fromFont(QFont(family));
        const QString badge = !rawFont.fontTable("CFF ").isEmpty()
                || !rawFont.fontTable("CFF2").isEmpty()
            ? QStringLiteral("OT")
            : (!rawFont.fontTable("glyf").isEmpty()
                   ? QStringLiteral("TT") : QStringLiteral("SYS"));
        fonts.push_back(QVariantMap{
            {QStringLiteral("name"), family},
            {QStringLiteral("family"), family},
            {QStringLiteral("key"), QStringLiteral("system:") + family},
            {QStringLiteral("kind"), QStringLiteral("system")},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("preview"), QString{}},
        });
    }
    sourceImageFonts_ = fonts;

    QDirIterator iterator(tiArtistFontsPath_, QDir::Files, QDirIterator::Subdirectories);
    QVariantList artistFonts;
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QString fontError;
        const auto font = loadTiArtistFont(path, &fontError);
        if (!font) continue;
        const QString name = QFileInfo(path).completeBaseName();
        QString sample = name;
        QImage sampleImage = renderTiArtistText(*font, sample, 18, Qt::white);
        bool hasInk = false;
        for (int y = 0; y < sampleImage.height() && !hasInk; ++y) {
            for (int x = 0; x < sampleImage.width(); ++x) {
                if (sampleImage.pixelColor(x, y).alpha() != 0) {
                    hasInk = true;
                    break;
                }
            }
        }
        if (!hasInk) {
            sample.clear();
            QList<QChar> characters = font->glyphs.keys();
            std::sort(characters.begin(), characters.end());
            for (const QChar character : characters) {
                if (!character.isPrint()) continue;
                sample += character;
                if (sample.size() == 12) break;
            }
            sampleImage = renderTiArtistText(*font, sample, 18, Qt::white);
        }
        artistFonts.push_back(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("family"), QString{}},
            {QStringLiteral("key"), QStringLiteral("tia:") + path},
            {QStringLiteral("kind"), QStringLiteral("tiartist")},
            {QStringLiteral("badge"), QStringLiteral("TIA")},
            {QStringLiteral("preview"), dataUrl(sampleImage)},
        });
    }
    std::sort(artistFonts.begin(), artistFonts.end(), [](const QVariant& left,
                                                         const QVariant& right) {
        return QString::localeAwareCompare(
                   left.toMap().value(QStringLiteral("name")).toString(),
                   right.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    for (const QVariant& font : artistFonts) fonts.push_back(font);
    screenImageFonts_ = std::move(fonts);
    emit screenImageFontsChanged();
}

void ImageInputController::openTiArtistFontsFolder()
{
    QDir().mkpath(tiArtistFontsPath_);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(tiArtistFontsPath_))) {
        errorMessage_ = tr("The TI Artist Fonts folder could not be opened.");
        emit statusChanged();
    }
}

std::shared_ptr<core::RgbImage> ImageInputController::renderScreenImageText(
    const QString& text, const QString& fontKey, int pixelSize, QString* error) const
{
    if (text.isEmpty()) {
        if (error) *error = tr("Enter text before placing it on the Screen Image.");
        return {};
    }
    const int maximumWidth = screenImage_
        ? static_cast<int>(screenImage_->width()) : targetWidth();
    const int maximumHeight = screenImage_
        ? static_cast<int>(screenImage_->height()) : targetHeight();
    pixelSize = std::clamp(pixelSize, 1, maximumHeight);
    QImage rendered;
    if (fontKey.startsWith(QStringLiteral("tia:"))) {
        QString fontError;
        const auto font = loadTiArtistFont(fontKey.sliced(4), &fontError);
        if (!font) {
            if (error) *error = fontError;
            return {};
        }
        rendered = renderTiArtistText(
            *font, text, pixelSize, screenImageForegroundColor());
    } else {
        const QString family = fontKey.startsWith(QStringLiteral("system:"))
            ? fontKey.sliced(7) : fontKey;
        QFont font(family);
        font.setPixelSize(pixelSize);
        const QFontMetrics metrics(font);
        const QStringList lines = text.split(QLatin1Char('\n'));
        int width = 1;
        for (const QString& line : lines)
            width = std::max(width, metrics.horizontalAdvance(line));
        const int height = std::max(
            1, static_cast<int>(lines.size()) * metrics.lineSpacing());
        rendered = QImage(width, height, QImage::Format_RGBA8888);
        rendered.fill(Qt::transparent);
        QPainter painter(&rendered);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font);
        painter.setPen(screenImageForegroundColor());
        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            painter.drawText(0, lineIndex * metrics.lineSpacing() + metrics.ascent(),
                             lines[lineIndex]);
        }
    }
    if (rendered.isNull() || rendered.width() > maximumWidth
        || rendered.height() > maximumHeight) {
        if (error) {
            *error = tr("The rendered text exceeds the %1 by %2 Screen Image. "
                        "Reduce the font size or shorten the text.")
                         .arg(maximumWidth)
                         .arg(maximumHeight);
        }
        return {};
    }
    auto image = coreImage(rendered);
    if (!image && error) *error = tr("The text could not be rendered as pixels.");
    return image;
}

std::shared_ptr<core::RgbImage> ImageInputController::renderSourceText(
    const QString& text, const QString& fontKey, int pixelSize, QString* error) const
{
    if (text.isEmpty()) {
        if (error) *error = tr("Enter text before placing it on the source image.");
        return {};
    }
    if (!fontKey.startsWith(QStringLiteral("system:"))) {
        if (error) *error = tr("Source-image text supports installed system fonts only.");
        return {};
    }
    pixelSize = std::clamp(pixelSize, 1, targetHeight());
    QFont font(fontKey.sliced(7));
    font.setPixelSize(pixelSize);
    const QFontMetrics metrics(font);
    const QStringList lines = text.split(QLatin1Char('\n'));
    int width = 1;
    for (const QString& line : lines)
        width = std::max(width, metrics.horizontalAdvance(line));
    const int height = std::max(1, static_cast<int>(lines.size()) * metrics.lineSpacing());
    QImage rendered(width, height, QImage::Format_RGBA8888);
    rendered.fill(Qt::transparent);
    QPainter painter(&rendered);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(foregroundColor());
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        painter.drawText(0, lineIndex * metrics.lineSpacing() + metrics.ascent(),
                         lines[lineIndex]);
    }
    const int maximumWidth = framedSource_
        ? static_cast<int>(framedSource_->width()) : targetWidth();
    const int maximumHeight = framedSource_
        ? static_cast<int>(framedSource_->height()) : targetHeight();
    if (rendered.isNull() || rendered.width() > maximumWidth
        || rendered.height() > maximumHeight) {
        if (error) *error = tr("The rendered text is larger than the source canvas.");
        return {};
    }
    auto image = coreImage(rendered);
    if (!image && error) *error = tr("The source text could not be rendered as pixels.");
    return image;
}

void ImageInputController::prepareScreenImageText(const QString& text,
                                                  const QString& fontKey,
                                                  int pixelSize)
{
    if (!screenImage_) return;
    QString renderError;
    auto rendered = renderScreenImageText(text, fontKey, pixelSize, &renderError);
    if (!rendered) {
        errorMessage_ = renderError;
        emit statusChanged();
        return;
    }
    beginScreenImageFloating(std::move(rendered), false, true);
    statusMessage_ = tr("Text ready. Click its starting point on the Screen Image; Escape cancels.");
    emit statusChanged();
}

void ImageInputController::prepareSourceText(const QString& text,
                                             const QString& fontKey,
                                             int pixelSize)
{
    if (!framedSource_) return;
    QString renderError;
    auto rendered = renderSourceText(text, fontKey, pixelSize, &renderError);
    if (!rendered) {
        errorMessage_ = renderError;
        emit statusChanged();
        return;
    }
    beginSourceFloating(std::move(rendered), false, true);
    statusMessage_ = tr("Text ready. Click its starting point on the source image; Escape cancels.");
    emit statusChanged();
}

void ImageInputController::loadScreenImageClipArt(const QUrl& url)
{
    if (!url.isLocalFile()) {
        errorMessage_ = tr("Only local Slide/ClipArt files can be opened.");
        emit statusChanged();
        return;
    }
    auto loaded = imageio::loadImageFile(url.toLocalFile());
    if (!loaded) {
        errorMessage_ = loaded.error;
        emit statusChanged();
        return;
    }
    screenImageClipArtSourceImage_ = std::make_shared<core::RgbImage>(
        std::move(*loaded.image));
    screenImageClipArtSourcePreview_ = dataUrl(*screenImageClipArtSourceImage_);
    screenImageClipArtSourceName_ = QFileInfo(url.toLocalFile()).fileName();
    errorMessage_.clear();
    statusMessage_ = tr("Slide/ClipArt loaded. Choose its placement options.");
    emit screenImageClipArtChanged();
    emit statusChanged();
}

std::shared_ptr<core::RgbImage> ImageInputController::renderScreenImageClipArt(
    int width, int height, int colorMode, bool transparentBackground,
    bool useSelectedColors, bool sourceColors, QString* error) const
{
    if (!screenImageClipArtSourceImage_) {
        if (error) *error = tr("Choose a Slide/ClipArt image first.");
        return {};
    }
    const int maximumWidth = screenImage_
        ? static_cast<int>(screenImage_->width()) : targetWidth();
    const int maximumHeight = screenImage_
        ? static_cast<int>(screenImage_->height()) : targetHeight();
    width = std::clamp(width, 1, maximumWidth);
    height = std::clamp(height, 1, maximumHeight);
    colorMode = std::clamp(colorMode, 0, 2);
    QImage image = imageio::toQImage(*screenImageClipArtSourceImage_)
                       .convertToFormat(QImage::Format_RGBA8888)
                       .scaled(width, height, Qt::IgnoreAspectRatio,
                               colorMode == 0 ? Qt::SmoothTransformation
                                              : Qt::FastTransformation);
    if (image.isNull()) {
        if (error) *error = tr("The Slide/ClipArt image could not be resized.");
        return {};
    }
    const QColor keyColor = image.pixelColor(0, 0);
    const QColor foreground = sourceColors ? foregroundColor()
                                           : screenImageForegroundColor();
    const QColor background = sourceColors ? backgroundColor()
                                           : screenImageBackgroundColor();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor source = image.pixelColor(x, y);
            const int luma = std::clamp(qRound(
                source.red() * 0.299 + source.green() * 0.587 + source.blue() * 0.114),
                0, 255);
            QColor result = source;
            int alpha = source.alpha();
            if (colorMode == 1) {
                if (useSelectedColors) {
                    if (transparentBackground) {
                        result = foreground;
                        alpha = alpha * (255 - luma) / 255;
                    } else {
                        result = QColor(
                            (foreground.red() * (255 - luma) + background.red() * luma) / 255,
                            (foreground.green() * (255 - luma) + background.green() * luma) / 255,
                            (foreground.blue() * (255 - luma) + background.blue() * luma) / 255);
                    }
                } else if (transparentBackground) {
                    result = Qt::black;
                    alpha = alpha * (255 - luma) / 255;
                } else {
                    result = QColor(luma, luma, luma);
                }
            } else if (colorMode == 2 || useSelectedColors) {
                const bool foregroundPixel = luma < 128;
                result = useSelectedColors
                    ? (foregroundPixel ? foreground : background)
                    : (foregroundPixel ? QColor(Qt::black) : QColor(Qt::white));
                if (transparentBackground && !foregroundPixel) alpha = 0;
            } else if (transparentBackground) {
                const int difference = std::abs(source.red() - keyColor.red())
                    + std::abs(source.green() - keyColor.green())
                    + std::abs(source.blue() - keyColor.blue());
                if (difference <= 12) alpha = 0;
            }
            result.setAlpha(alpha);
            image.setPixelColor(x, y, result);
        }
    }
    auto rendered = coreImage(image);
    if (!rendered && error) *error = tr("The Slide/ClipArt preview could not be prepared.");
    return rendered;
}

QString ImageInputController::screenImageClipArtPreview(
    int width, int height, int colorMode, bool transparentBackground,
    bool useSelectedColors) const
{
    const auto preview = renderScreenImageClipArt(
        width, height, colorMode, transparentBackground, useSelectedColors, false);
    return preview ? dataUrl(*preview) : QString{};
}

void ImageInputController::prepareScreenImageClipArt(
    int width, int height, int colorMode, bool transparentBackground,
    bool useSelectedColors)
{
    if (!screenImage_) return;
    QString renderError;
    auto rendered = renderScreenImageClipArt(
        width, height, colorMode, transparentBackground, useSelectedColors,
        false, &renderError);
    if (!rendered) {
        errorMessage_ = renderError;
        emit statusChanged();
        return;
    }
    beginScreenImageFloating(std::move(rendered), false);
    statusMessage_ = tr("Slide/ClipArt ready. Click the Screen Image to place it; Escape cancels.");
    emit statusChanged();
}

QString ImageInputController::sourceImageClipArtPreview(
    int width, int height, int colorMode, bool transparentBackground,
    bool useSelectedColors) const
{
    const auto preview = renderScreenImageClipArt(
        width, height, colorMode, transparentBackground, useSelectedColors, true);
    return preview ? dataUrl(*preview) : QString{};
}

void ImageInputController::prepareSourceImageClipArt(
    int width, int height, int colorMode, bool transparentBackground,
    bool useSelectedColors)
{
    if (!framedSource_) return;
    QString renderError;
    auto rendered = renderScreenImageClipArt(
        width, height, colorMode, transparentBackground, useSelectedColors,
        true, &renderError);
    if (!rendered) {
        errorMessage_ = renderError;
        emit statusChanged();
        return;
    }
    beginSourceFloating(std::move(rendered), false);
    statusMessage_ = tr("ClipArt ready. Click the source image to place it; Escape cancels.");
    emit statusChanged();
}

void ImageInputController::copySourceImage()
{
    if (!framedSource_) return;
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return;
    if (hasSourceSelection()) {
        const auto region = copySourceRegion(
            sourceSelectionX_, sourceSelectionY_,
            sourceSelectionWidth_, sourceSelectionHeight_);
        if (!region) return;
        auto* mimeData = new QMimeData;
        mimeData->setImageData(imageio::toQImage(*region));
        mimeData->setData(sourceImageSelectionMimeType, QByteArrayLiteral("1"));
        clipboard->setMimeData(mimeData);
        statusMessage_ = tr("Source-image selection copied to the clipboard.");
    } else {
        clipboard->setImage(imageio::toQImage(*framedSource_));
        statusMessage_ = tr("Source image copied to the clipboard.");
    }
    errorMessage_.clear();
    emit statusChanged();
}

void ImageInputController::pasteSourceImage()
{
    if (!framedSource_) return;
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr || clipboard->mimeData() == nullptr
        || !clipboard->mimeData()->hasImage()) {
        errorMessage_ = tr("The clipboard does not contain an image.");
        emit statusChanged();
        return;
    }
    const bool selectionClipboard = clipboard->mimeData()->hasFormat(
        sourceImageSelectionMimeType)
        || clipboard->mimeData()->hasFormat(screenImageSelectionMimeType);
    auto loaded = imageio::loadClipboardImage(clipboard->image());
    if (!loaded) {
        errorMessage_ = loaded.error;
        emit statusChanged();
        return;
    }
    if (selectionClipboard) {
        beginSourceFloating(
            std::make_shared<core::RgbImage>(std::move(*loaded.image)), false);
        return;
    }
    const core::ImageTransformOptions options{
        .targetWidth = framedSource_->width(),
        .targetHeight = framedSource_->height(),
        .filter = core::ScalingFilter::Bilinear,
        .fillMode = core::ImageFillMode::Fit,
        .backgroundRed = backgroundColor_.red,
        .backgroundGreen = backgroundColor_.green,
        .backgroundBlue = backgroundColor_.blue,
    };
    auto transformed = core::transformImage(*loaded.image, options);
    if (!transformed) {
        errorMessage_ = tr("The clipboard image could not be fitted to the source canvas.");
        emit statusChanged();
        return;
    }
    beginDrawingTransaction();
    replaceDrawingLayerWithCanvas(*transformed.image);
    commitDrawingTransaction(true);
    clearSourceSelection();
    errorMessage_.clear();
    refreshAfterSourceCanvasEdit();
}

void ImageInputController::copyScreenImage()
{
    if (!screenImage_) return;
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return;
    if (hasScreenImageSelection()) {
        const auto region = copyScreenImageRegion(
            screenImageSelectionX_, screenImageSelectionY_,
            screenImageSelectionWidth_, screenImageSelectionHeight_);
        if (!region) return;
        auto* mimeData = new QMimeData;
        mimeData->setImageData(imageio::toQImage(*region));
        mimeData->setData(screenImageSelectionMimeType, QByteArrayLiteral("1"));
        clipboard->setMimeData(mimeData);
        statusMessage_ = tr("Screen Image selection copied to the clipboard.");
    } else {
        clipboard->setImage(imageio::toQImage(*screenImage_));
        statusMessage_ = tr("Screen Image copied to the clipboard.");
    }
    errorMessage_.clear();
    emit statusChanged();
}

void ImageInputController::pasteScreenImage()
{
    if (!screenImage_) return;
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr || clipboard->mimeData() == nullptr
        || !clipboard->mimeData()->hasImage()) {
        errorMessage_ = tr("The clipboard does not contain an image.");
        emit statusChanged();
        return;
    }
    const bool selectionClipboard = clipboard->mimeData()->hasFormat(
        screenImageSelectionMimeType);
    auto loaded = imageio::loadClipboardImage(clipboard->image());
    if (!loaded) {
        errorMessage_ = loaded.error;
        emit statusChanged();
        return;
    }
    if (selectionClipboard) {
        beginScreenImageFloating(
            std::make_shared<core::RgbImage>(std::move(*loaded.image)), false);
        return;
    }
    const core::ImageTransformOptions options{
        .targetWidth = screenImage_->width(),
        .targetHeight = screenImage_->height(),
        .filter = core::ScalingFilter::Bilinear,
        .fillMode = core::ImageFillMode::Fit,
        .backgroundRed = screenImageBackgroundColor_.red,
        .backgroundGreen = screenImageBackgroundColor_.green,
        .backgroundBlue = screenImageBackgroundColor_.blue,
    };
    auto transformed = core::transformImage(*loaded.image, options);
    if (!transformed) {
        errorMessage_ = tr("The clipboard image could not be fitted to the Screen Image.");
        emit statusChanged();
        return;
    }
    beginScreenImageTransaction();
    screenImage_ = std::make_shared<core::RgbImage>(std::move(*transformed.image));
    commitScreenImageTransaction(true);
    clearScreenImageSelection();
    errorMessage_.clear();
    refreshScreenImage();
}

void ImageInputController::undoScreenImage()
{
    cancelScreenImageFloating();
    if (screenImageStrokeActive_) endScreenImageStroke();
    if (!screenImage_ || screenImageUndoStack_.empty()) return;
    screenImageRedoStack_.push_back(screenImage_);
    trimDrawingHistory(screenImageRedoStack_);
    screenImage_ = std::move(screenImageUndoStack_.back());
    screenImageUndoStack_.pop_back();
    screenImageBefore_.reset();
    refreshScreenImage();
    emit screenImageHistoryChanged();
}

void ImageInputController::redoScreenImage()
{
    cancelScreenImageFloating();
    if (screenImageStrokeActive_) endScreenImageStroke();
    if (!screenImage_ || screenImageRedoStack_.empty()) return;
    screenImageUndoStack_.push_back(screenImage_);
    trimDrawingHistory(screenImageUndoStack_);
    screenImage_ = std::move(screenImageRedoStack_.back());
    screenImageRedoStack_.pop_back();
    screenImageBefore_.reset();
    refreshScreenImage();
    emit screenImageHistoryChanged();
}

void ImageInputController::applyScreenImageEdits()
{
    cancelScreenImageFloating();
    if (screenImageStrokeActive_) endScreenImageStroke();
    if (!screenImage_ || !screenImageEdited_ || busy_) return;

    const core::ConversionRequest request{
        .source = std::make_shared<core::RgbImage>(*screenImage_),
        .settings = screenImageConversionSettings(settings_),
        .generation = 0,
        .cancellation = {},
    };
    busy_ = true;
    statusMessage_ = tr("Applying chipset rules to Screen Image…");
    emit conversionChanged();
    emit statusChanged();

    auto converted = runConversion(
        request, core::ScalingFilter::None, core::ImageFillMode::Fit,
        0, 0, screenImageBackgroundColor_, workingPalette_, false,
        screenImage_->width() == core::displayMode(settings_.mode).geometry.width
            && screenImage_->height() == core::displayMode(settings_.mode).geometry.height,
        {});
    busy_ = false;
    if (!converted.succeeded() || !converted.preview || !converted.target) {
        errorMessage_ = tr("The Screen Image could not be constrained to the chipset rules.");
        for (const auto& diagnostic : converted.diagnostics) {
            if (diagnostic.severity == core::DiagnosticSeverity::Error) {
                errorMessage_ = QString::fromStdString(diagnostic.message);
                break;
            }
        }
        statusMessage_.clear();
        emit conversionChanged();
        emit statusChanged();
        return;
    }

    screenImageUndoStack_.push_back(screenImage_);
    trimDrawingHistory(screenImageUndoStack_);
    screenImageRedoStack_.clear();
    screenImageBefore_.reset();
    screenImage_ = std::make_shared<core::RgbImage>(*converted.preview);
    convertedPreview_ = dataUrl(*screenImage_);
    converted.preview = *screenImage_;
    result_ = std::move(converted);
    screenImageExportResult_.reset();
    screenImageEdited_ = false;
    if (!image_) refreshSourceColorChoices();

    const std::size_t byteCount = std::accumulate(
        result_->target->tables.begin(), result_->target->tables.end(), std::size_t{},
        [](std::size_t total, const core::TargetMemoryTable& table) {
            return total + table.bytes.size();
        });
    conversionDetails_ = formatConversionDetails(
        *result_->target, *result_->preview, byteCount);
    errorMessage_.clear();
    statusMessage_ = tr("Screen Image drawing applied with chipset rules.");
    updatePaletteInspection();
    updateExportSummary();
    emit screenImageHistoryChanged();
    emit conversionChanged();
    emit exportChanged();
    emit statusChanged();
}

ImageInputController::SettingsSnapshot ImageInputController::snapshot() const
{
    return {settings_,
            scalingFilter_,
            fillMode_,
            horizontalOffset_,
            verticalOffset_,
            foregroundColor_,
            backgroundColor_,
            workingPalette_,
            powerPaintFraming_};
}

void ImageInputController::recordUndo()
{
    if (applyingSnapshot_ || undoCoalesceTimer_.isActive()) {
        undoCoalesceTimer_.start();
        return;
    }
    undoStack_.push_back(snapshot());
    if (undoStack_.size() > 32U) undoStack_.erase(undoStack_.begin());
    undoCoalesceTimer_.start();
}

void ImageInputController::applySnapshot(const SettingsSnapshot& value)
{
    applyingSnapshot_ = true;
    settings_ = value.settings;
    scalingFilter_ = value.scalingFilter;
    fillMode_ = value.fillMode;
    horizontalOffset_ = value.horizontalOffset;
    verticalOffset_ = value.verticalOffset;
    foregroundColor_ = value.foregroundColor;
    backgroundColor_ = value.backgroundColor;
    workingPalette_ = value.workingPalette;
    powerPaintFraming_ = value.powerPaintFraming;
    applyingSnapshot_ = false;
    saveSettings();
    refreshSourcePreview();
    refreshSourceColorChoices();
    emit settingsChanged();
    scheduleConversion();
}

void ImageInputController::undoSettings()
{
    if (undoStack_.empty()) return;
    undoCoalesceTimer_.stop();
    const auto value = undoStack_.back();
    undoStack_.pop_back();
    applySnapshot(value);
}

void ImageInputController::resetSettings()
{
    recordUndo();
    SettingsSnapshot defaults;
    defaults.settings = core::ConversionSettings{};
    defaults.scalingFilter = core::ScalingFilter::Bilinear;
    defaults.fillMode = core::ImageFillMode::Fit;
    defaults.foregroundColor = {255, 255, 255};
    defaults.backgroundColor = {0, 0, 0};
    defaults.workingPalette = defaultWorkingColors();
    defaults.powerPaintFraming = false;
    applySnapshot(defaults);
}

void ImageInputController::applyPreset(int presetIndex)
{
    recordUndo();
    auto value = snapshot();
    switch (presetIndex) {
    case 1: // Pixel art
        value.settings.dither = core::DitherMode::None;
        value.settings.stretchHistogram = false;
        value.settings.maximumColorShiftPercent = 0.0;
        value.scalingFilter = core::ScalingFilter::None;
        break;
    case 2: // Smooth photo
        value.settings.dither = core::DitherMode::Atkinson;
        value.settings.stretchHistogram = true;
        value.settings.maximumColorShiftPercent = 2.0;
        value.scalingFilter = core::ScalingFilter::Blackman;
        break;
    case 3: // Ordered retro
        value.settings.dither = core::DitherMode::Ordered;
        value.settings.stretchHistogram = false;
        value.settings.orderedDitherBrightness = 0;
        value.scalingFilter = core::ScalingFilter::Bilinear;
        break;
    default: // Balanced
        value.settings = core::ConversionSettings{};
        value.scalingFilter = core::ScalingFilter::Bilinear;
        break;
    }
    if (const auto configuration = core::ditherConfiguration(value.settings.dither)) {
        value.settings.errorDistribution = configuration->kernel;
    }
    applySnapshot(value);
}

QJsonObject ImageInputController::recipeSettings() const
{
    QJsonArray palette;
    for (const auto& color : workingPalette_) palette.push_back(colorName(color));
    return {
        {QStringLiteral("targetProfile"),
         QString::fromLatin1(core::targetProfile(settings_.targetProfile).stableId.data(),
                             static_cast<qsizetype>(core::targetProfile(
                                 settings_.targetProfile).stableId.size()))},
        {QStringLiteral("mode"), conversionMode()},
        {QStringLiteral("dither"), ditherMode()},
        {QStringLiteral("scalingFilter"), scalingFilter()},
        {QStringLiteral("fillMode"), fillMode()},
        {QStringLiteral("perceptualColorMatching"), perceptualColorMatching()},
        {QStringLiteral("perceptualRedWeight"), perceptualRedWeight()},
        {QStringLiteral("perceptualGreenWeight"), perceptualGreenWeight()},
        {QStringLiteral("perceptualBlueWeight"), perceptualBlueWeight()},
        {QStringLiteral("stretchHistogram"), stretchHistogram()},
        {QStringLiteral("maximumColorShift"), maximumColorShift()},
        {QStringLiteral("gamma"), gamma()},
        {QStringLiteral("lumaEmphasis"), lumaEmphasis()},
        {QStringLiteral("maximumMulticolorDifference"), maximumMulticolorDifference()},
        {QStringLiteral("orderedBrightness"), orderedBrightness()},
        {QStringLiteral("errorAccumulation"), errorAccumulationMode()},
        {QStringLiteral("orderedDitherMapSize"), orderedDitherMapSize()},
        {QStringLiteral("errorDownLeft"), errorDownLeft()},
        {QStringLiteral("errorDown"), errorDown()},
        {QStringLiteral("errorDownRight"), errorDownRight()},
        {QStringLiteral("errorRight"), errorRight()},
        {QStringLiteral("errorFarRight"), errorFarRight()},
        {QStringLiteral("errorDownTwo"), errorDownTwo()},
        {QStringLiteral("paletteSelection"), paletteSelectionMode()},
        {QStringLiteral("scanlineStaticColorCount"), scanlineStaticColorCount()},
        {QStringLiteral("scanlineRegion1"), scanlineRegion1()},
        {QStringLiteral("scanlineRegion2"), scanlineRegion2()},
        {QStringLiteral("scanlineRegion3"), scanlineRegion3()},
        {QStringLiteral("horizontalOffset"), horizontalOffset()},
        {QStringLiteral("verticalOffset"), verticalOffset()},
        {QStringLiteral("foregroundColor"), foregroundColor().name()},
        {QStringLiteral("backgroundColor"), backgroundColor().name()},
        {QStringLiteral("workingPalette"), palette},
        {QStringLiteral("powerPaintFraming"), powerPaintFraming()},
        {QStringLiteral("autoUpdate"), autoUpdate()},
        {QStringLiteral("livePreview"), livePreview()},
        {QStringLiteral("exportFormat"), exportFormat()},
    };
}

bool ImageInputController::applyRecipeSettings(const QJsonObject& object, QString* error)
{
    if (object.isEmpty()) {
        if (error) *error = QStringLiteral("The recipe does not contain conversion settings.");
        return false;
    }
    SettingsSnapshot value = snapshot();
    value.settings.mode = static_cast<core::ConversionMode>(
        std::clamp(object.value(QStringLiteral("mode")).toInt(conversionMode()), 0,
                   static_cast<int>(core::ConversionMode::SuperNesMode3Background)));
    const std::string requestedProfile = object.value(QStringLiteral("targetProfile"))
        .toString().toStdString();
    value.settings.targetProfile = requestedProfile.empty()
        ? core::primaryTargetProfile(value.settings.mode)
        : core::targetProfileId(requestedProfile).value_or(
              core::primaryTargetProfile(value.settings.mode));
    value.settings.targetProfile = core::effectiveTargetProfile(
        value.settings.targetProfile, value.settings.mode);
    value.settings.dither = static_cast<core::DitherMode>(
        std::clamp(object.value(QStringLiteral("dither")).toInt(ditherMode()), 0, 7));
    value.scalingFilter = static_cast<core::ScalingFilter>(
        std::clamp(object.value(QStringLiteral("scalingFilter")).toInt(scalingFilter()), 0, 5));
    value.fillMode = static_cast<core::ImageFillMode>(
        std::clamp(object.value(QStringLiteral("fillMode")).toInt(fillMode()), 0, 3));
    value.settings.perceptualColorMatching = object.value(
        QStringLiteral("perceptualColorMatching")).toBool(perceptualColorMatching());
    value.settings.perceptualRedWeight = std::clamp(
        object.value(QStringLiteral("perceptualRedWeight")).toInt(perceptualRedWeight()) / 100.0,
        0.0, 1.0);
    value.settings.perceptualGreenWeight = std::clamp(
        object.value(QStringLiteral("perceptualGreenWeight")).toInt(perceptualGreenWeight()) / 100.0,
        0.0, 1.0);
    value.settings.perceptualBlueWeight = std::clamp(
        object.value(QStringLiteral("perceptualBlueWeight")).toInt(perceptualBlueWeight()) / 100.0,
        0.0, 1.0);
    value.settings.stretchHistogram = object.value(
        QStringLiteral("stretchHistogram")).toBool(stretchHistogram());
    value.settings.maximumColorShiftPercent = std::clamp(
        object.value(QStringLiteral("maximumColorShift")).toDouble(maximumColorShift()),
        0.0, 100.0);
    value.settings.gamma = std::clamp(
        object.value(QStringLiteral("gamma")).toDouble(gamma()), 0.1, 5.0);
    value.settings.lumaEmphasis = std::clamp(
        object.value(QStringLiteral("lumaEmphasis")).toDouble(lumaEmphasis()), 0.0, 10.0);
    value.settings.maximumMulticolorDifferencePercent = std::clamp(
        object.value(QStringLiteral("maximumMulticolorDifference"))
            .toInt(maximumMulticolorDifference()), 0, 100);
    value.settings.orderedDitherBrightness = std::clamp(
        object.value(QStringLiteral("orderedBrightness")).toInt(orderedBrightness()), 0, 16);
    value.settings.errorAccumulation = static_cast<core::ErrorAccumulationMode>(
        std::clamp(object.value(QStringLiteral("errorAccumulation"))
                       .toInt(errorAccumulationMode()), 0, 1));
    value.settings.orderedDitherMapSize =
        object.value(QStringLiteral("orderedDitherMapSize")).toInt(orderedDitherMapSize()) == 4
        ? core::OrderedDitherMapSize::FourByFour
        : core::OrderedDitherMapSize::TwoByTwo;
    value.settings.errorDistribution = {
        static_cast<std::uint8_t>(std::clamp(
            object.value(QStringLiteral("errorDownLeft")).toInt(errorDownLeft()), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            object.value(QStringLiteral("errorDown")).toInt(errorDown()), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            object.value(QStringLiteral("errorDownRight")).toInt(errorDownRight()), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            object.value(QStringLiteral("errorRight")).toInt(errorRight()), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            object.value(QStringLiteral("errorFarRight")).toInt(errorFarRight()), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            object.value(QStringLiteral("errorDownTwo")).toInt(errorDownTwo()), 0, 16)),
    };
    value.settings.paletteSelection = static_cast<core::PaletteSelectionMode>(
        std::clamp(object.value(QStringLiteral("paletteSelection"))
                       .toInt(paletteSelectionMode()), 0, 1));
    value.settings.scanlineStaticColorCount = std::clamp(
        object.value(QStringLiteral("scanlineStaticColorCount"))
            .toInt(scanlineStaticColorCount()), 0, 14);
    value.settings.scanlineRegion1 = object.value(
        QStringLiteral("scanlineRegion1")).toBool(scanlineRegion1());
    value.settings.scanlineRegion2 = object.value(
        QStringLiteral("scanlineRegion2")).toBool(scanlineRegion2());
    value.settings.scanlineRegion3 = object.value(
        QStringLiteral("scanlineRegion3")).toBool(scanlineRegion3());
    value.horizontalOffset = std::clamp(
        object.value(QStringLiteral("horizontalOffset")).toInt(horizontalOffset()), -256, 256);
    value.verticalOffset = std::clamp(
        object.value(QStringLiteral("verticalOffset")).toInt(verticalOffset()), -192, 192);
    const QColor foreground(object.value(QStringLiteral("foregroundColor"))
                                .toString(foregroundColor().name()));
    const QColor background(object.value(QStringLiteral("backgroundColor"))
                                .toString(backgroundColor().name()));
    if (!foreground.isValid() || !background.isValid()) {
        if (error) *error = QStringLiteral("The recipe contains an invalid drawing color.");
        return false;
    }
    value.foregroundColor = {static_cast<std::uint8_t>(foreground.red()),
                             static_cast<std::uint8_t>(foreground.green()),
                             static_cast<std::uint8_t>(foreground.blue())};
    value.backgroundColor = {static_cast<std::uint8_t>(background.red()),
                             static_cast<std::uint8_t>(background.green()),
                             static_cast<std::uint8_t>(background.blue())};
    const QJsonArray savedPalette = object.value(QStringLiteral("workingPalette")).toArray();
    if (!savedPalette.isEmpty()) {
        if (savedPalette.size() != 15) {
            if (error) *error = QStringLiteral("The working palette must contain 15 colors.");
            return false;
        }
        std::vector<core::RgbColor> colors;
        colors.reserve(15);
        for (const auto savedColor : savedPalette) {
            const QColor color(savedColor.toString());
            if (!color.isValid()) {
                if (error) *error = QStringLiteral("The working palette contains an invalid color.");
                return false;
            }
            colors.push_back({static_cast<std::uint8_t>(color.red()),
                              static_cast<std::uint8_t>(color.green()),
                              static_cast<std::uint8_t>(color.blue())});
        }
        value.workingPalette = std::move(colors);
    }
    value.powerPaintFraming = object.value(
        QStringLiteral("powerPaintFraming")).toBool(powerPaintFraming());

    autoUpdate_ = object.value(QStringLiteral("autoUpdate")).toBool(autoUpdate_);
    livePreview_ = object.value(QStringLiteral("livePreview")).toBool(livePreview_);
    exportFormat_ = std::clamp(
        object.value(QStringLiteral("exportFormat")).toInt(exportFormat_), 0, 8);
    undoStack_.clear();
    applySnapshot(value);
    emit exportChanged();
    if (error) error->clear();
    return true;
}

void ImageInputController::settingsWereChanged(bool sourceTransformChanged)
{
    saveSettings();
    if (sourceTransformChanged) {
        refreshSourcePreview();
        refreshSourceColorChoices();
    }
    emit settingsChanged();
    scheduleConversion();
}

void ImageInputController::loadSettings()
{
    QSettings persisted;
    persisted.beginGroup(QStringLiteral("conversion"));
    settings_.mode = static_cast<core::ConversionMode>(
        std::clamp(persisted.value(QStringLiteral("mode"), 0).toInt(), 0,
                   static_cast<int>(core::ConversionMode::SuperNesMode3Background)));
    const std::string persistedProfile = persisted.value(
        QStringLiteral("targetProfile")).toString().toStdString();
    settings_.targetProfile = persistedProfile.empty()
        ? core::primaryTargetProfile(settings_.mode)
        : core::targetProfileId(persistedProfile).value_or(
              core::primaryTargetProfile(settings_.mode));
    settings_.targetProfile = core::effectiveTargetProfile(
        settings_.targetProfile, settings_.mode);
    settings_.dither = static_cast<core::DitherMode>(
        std::clamp(persisted.value(QStringLiteral("dither"), 2).toInt(), 0, 7));
    const auto defaultDithering = core::ditherConfiguration(settings_.dither);
    const core::ErrorDistributionKernel defaultKernel = defaultDithering
        ? defaultDithering->kernel
        : core::ErrorDistributionKernel{2, 2, 2, 2, 1, 1};
    settings_.errorDistribution = {
        static_cast<std::uint8_t>(std::clamp(
            persisted.value(QStringLiteral("errorDownLeft"), defaultKernel.downLeft).toInt(),
            0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            persisted.value(QStringLiteral("errorDown"), defaultKernel.down).toInt(), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            persisted.value(QStringLiteral("errorDownRight"), defaultKernel.downRight).toInt(),
            0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            persisted.value(QStringLiteral("errorRight"), defaultKernel.right).toInt(), 0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            persisted.value(QStringLiteral("errorFarRight"), defaultKernel.farRight).toInt(),
            0, 16)),
        static_cast<std::uint8_t>(std::clamp(
            persisted.value(QStringLiteral("errorDownTwo"), defaultKernel.downTwo).toInt(),
            0, 16)),
    };
    scalingFilter_ = static_cast<core::ScalingFilter>(
        std::clamp(persisted.value(QStringLiteral("scalingFilter"), 4).toInt(), 0, 5));
    fillMode_ = static_cast<core::ImageFillMode>(
        std::clamp(persisted.value(QStringLiteral("fillMode"), 0).toInt(), 0, 3));
    settings_.perceptualColorMatching =
        persisted.value(QStringLiteral("perceptual"), false).toBool();
    settings_.perceptualRedWeight = std::clamp(
        persisted.value(QStringLiteral("perceptualRedWeight"), 0.30).toDouble(), 0.0, 1.0);
    settings_.perceptualGreenWeight = std::clamp(
        persisted.value(QStringLiteral("perceptualGreenWeight"), 0.52).toDouble(), 0.0, 1.0);
    settings_.perceptualBlueWeight = std::clamp(
        persisted.value(QStringLiteral("perceptualBlueWeight"), 0.18).toDouble(), 0.0, 1.0);
    settings_.stretchHistogram = persisted.value(QStringLiteral("histogram"), false).toBool();
    settings_.maximumColorShiftPercent =
        std::clamp(persisted.value(QStringLiteral("colorShift"), 1.0).toDouble(), 0.0, 100.0);
    settings_.gamma =
        std::clamp(persisted.value(QStringLiteral("gamma"), 1.0).toDouble(), 0.1, 5.0);
    settings_.lumaEmphasis =
        std::clamp(persisted.value(QStringLiteral("luma"), 1.2).toDouble(), 0.0, 10.0);
    settings_.maximumMulticolorDifferencePercent =
        std::clamp(persisted.value(QStringLiteral("flicker"), 95).toInt(), 0, 100);
    settings_.orderedDitherBrightness =
        std::clamp(persisted.value(QStringLiteral("orderedBrightness"), 0).toInt(), 0, 16);
    settings_.errorAccumulation = static_cast<core::ErrorAccumulationMode>(
        std::clamp(persisted.value(QStringLiteral("errorAccumulation"), 1).toInt(), 0, 1));
    const int persistedOrderedMapSize =
        persisted.value(QStringLiteral("orderedDitherMapSize"), 2).toInt();
    settings_.orderedDitherMapSize = persistedOrderedMapSize == 4
        ? core::OrderedDitherMapSize::FourByFour
        : core::OrderedDitherMapSize::TwoByTwo;
    settings_.paletteSelection = static_cast<core::PaletteSelectionMode>(
        std::clamp(persisted.value(QStringLiteral("paletteSelection"), 0).toInt(), 0, 1));
    settings_.scanlineStaticColorCount = std::clamp(
        persisted.value(QStringLiteral("scanlineStaticColorCount"), 0).toInt(), 0, 14);
    settings_.scanlineRegion1 = persisted.value(QStringLiteral("scanlineRegion1"), true).toBool();
    settings_.scanlineRegion2 = persisted.value(QStringLiteral("scanlineRegion2"), true).toBool();
    settings_.scanlineRegion3 = persisted.value(QStringLiteral("scanlineRegion3"), true).toBool();
    horizontalOffset_ =
        std::clamp(persisted.value(QStringLiteral("horizontalOffset"), 0).toInt(), -256, 256);
    verticalOffset_ =
        std::clamp(persisted.value(QStringLiteral("verticalOffset"), 0).toInt(), -192, 192);
    const QStringList storedPalette =
        persisted.value(QStringLiteral("workingPalette")).toStringList();
    if (storedPalette.size() == 15) {
        std::vector<core::RgbColor> colors;
        colors.reserve(15);
        for (const QString& name : storedPalette) {
            const QColor color(name);
            if (!color.isValid()) {
                colors.clear();
                break;
            }
            colors.push_back({static_cast<std::uint8_t>(color.red()),
                              static_cast<std::uint8_t>(color.green()),
                              static_cast<std::uint8_t>(color.blue())});
        }
        if (colors.size() == 15U) workingPalette_ = std::move(colors);
    }
    const QColor persistedBackground(
        persisted.value(QStringLiteral("backgroundColor"), QStringLiteral("#000000"))
            .toString());
    const QColor persistedForeground(
        persisted.value(QStringLiteral("foregroundColor"), QStringLiteral("#FFFFFF"))
            .toString());
    foregroundColor_ = persistedForeground.isValid()
        ? core::RgbColor{static_cast<std::uint8_t>(persistedForeground.red()),
                         static_cast<std::uint8_t>(persistedForeground.green()),
                         static_cast<std::uint8_t>(persistedForeground.blue())}
        : core::RgbColor{255, 255, 255};
    const QColor loadedBackground = persistedBackground.isValid()
        ? persistedBackground : QColor(Qt::black);
    backgroundColor_ = {static_cast<std::uint8_t>(loadedBackground.red()),
                        static_cast<std::uint8_t>(loadedBackground.green()),
                        static_cast<std::uint8_t>(loadedBackground.blue())};
    powerPaintFraming_ = persisted.value(QStringLiteral("powerPaintFraming"), false).toBool();
    autoUpdate_ = persisted.value(QStringLiteral("autoUpdate"), true).toBool();
    livePreview_ = persisted.value(QStringLiteral("livePreview"), false).toBool();
    exportFormat_ = std::clamp(persisted.value(QStringLiteral("exportFormat"), 0).toInt(), 0, 8);
    persisted.endGroup();
}

void ImageInputController::saveSettings() const
{
    QSettings persisted;
    persisted.beginGroup(QStringLiteral("conversion"));
    const auto& profile = core::targetProfile(settings_.targetProfile);
    persisted.setValue(
        QStringLiteral("targetProfile"),
        QString::fromLatin1(profile.stableId.data(),
                            static_cast<qsizetype>(profile.stableId.size())));
    persisted.setValue(QStringLiteral("mode"), conversionMode());
    persisted.setValue(QStringLiteral("dither"), ditherMode());
    persisted.setValue(QStringLiteral("scalingFilter"), scalingFilter());
    persisted.setValue(QStringLiteral("fillMode"), fillMode());
    persisted.setValue(QStringLiteral("perceptual"), perceptualColorMatching());
    persisted.setValue(QStringLiteral("perceptualRedWeight"), settings_.perceptualRedWeight);
    persisted.setValue(QStringLiteral("perceptualGreenWeight"), settings_.perceptualGreenWeight);
    persisted.setValue(QStringLiteral("perceptualBlueWeight"), settings_.perceptualBlueWeight);
    persisted.setValue(QStringLiteral("histogram"), stretchHistogram());
    persisted.setValue(QStringLiteral("colorShift"), maximumColorShift());
    persisted.setValue(QStringLiteral("gamma"), gamma());
    persisted.setValue(QStringLiteral("luma"), lumaEmphasis());
    persisted.setValue(QStringLiteral("flicker"), maximumMulticolorDifference());
    persisted.setValue(QStringLiteral("orderedBrightness"), orderedBrightness());
    persisted.setValue(QStringLiteral("errorAccumulation"), errorAccumulationMode());
    persisted.setValue(QStringLiteral("orderedDitherMapSize"), orderedDitherMapSize());
    persisted.setValue(QStringLiteral("errorDownLeft"), errorDownLeft());
    persisted.setValue(QStringLiteral("errorDown"), errorDown());
    persisted.setValue(QStringLiteral("errorDownRight"), errorDownRight());
    persisted.setValue(QStringLiteral("errorRight"), errorRight());
    persisted.setValue(QStringLiteral("errorFarRight"), errorFarRight());
    persisted.setValue(QStringLiteral("errorDownTwo"), errorDownTwo());
    persisted.setValue(QStringLiteral("paletteSelection"), paletteSelectionMode());
    persisted.setValue(QStringLiteral("scanlineStaticColorCount"), scanlineStaticColorCount());
    persisted.setValue(QStringLiteral("scanlineRegion1"), scanlineRegion1());
    persisted.setValue(QStringLiteral("scanlineRegion2"), scanlineRegion2());
    persisted.setValue(QStringLiteral("scanlineRegion3"), scanlineRegion3());
    persisted.setValue(QStringLiteral("horizontalOffset"), horizontalOffset());
    persisted.setValue(QStringLiteral("verticalOffset"), verticalOffset());
    persisted.setValue(QStringLiteral("foregroundColor"), foregroundColor().name());
    persisted.setValue(QStringLiteral("backgroundColor"), backgroundColor().name());
    QStringList paletteNames;
    for (const auto& color : workingPalette_) paletteNames.push_back(colorName(color));
    persisted.setValue(QStringLiteral("workingPalette"), paletteNames);
    persisted.setValue(QStringLiteral("powerPaintFraming"), powerPaintFraming_);
    persisted.setValue(QStringLiteral("autoUpdate"), autoUpdate_);
    persisted.setValue(QStringLiteral("livePreview"), livePreview_);
    persisted.setValue(QStringLiteral("exportFormat"), exportFormat_);
    persisted.endGroup();
    persisted.sync();
}

void ImageInputController::setTargetProfile(int value)
{
    value = std::clamp(value, 0,
                       static_cast<int>(core::TargetProfileId::SuperNes));
    const auto requested = static_cast<core::TargetProfileId>(value);
    const auto& profile = core::targetProfile(requested);
    if (profile.status != core::TargetProfileStatus::Implemented
        || requested == settings_.targetProfile) {
        return;
    }
    recordUndo();
    settings_.targetProfile = requested;
    if (!core::supportsConversionMode(requested, settings_.mode)) {
        settings_.mode = core::defaultConversionMode(requested);
    }
    settingsWereChanged(true);
}

void ImageInputController::setConversionMode(int value)
{
    value = std::clamp(value, 0,
                       static_cast<int>(core::ConversionMode::SuperNesMode3Background));
    const auto mode = static_cast<core::ConversionMode>(value);
    const auto effectiveProfile = core::effectiveTargetProfile(
        settings_.targetProfile, mode);
    if (value == conversionMode() && effectiveProfile == settings_.targetProfile) return;
    recordUndo();
    settings_.mode = mode;
    settings_.targetProfile = effectiveProfile;
    settingsWereChanged(true);
}

void ImageInputController::setDitherMode(int value)
{
    value = std::clamp(value, 0, 7);
    if (value == ditherMode()) return;
    recordUndo();
    settings_.dither = static_cast<core::DitherMode>(value);
    if (settings_.dither != core::DitherMode::Custom) {
        const auto configuration = core::ditherConfiguration(settings_.dither);
        if (configuration) settings_.errorDistribution = configuration->kernel;
    }
    settingsWereChanged();
}

void ImageInputController::setScalingFilter(int value)
{
    value = std::clamp(value, 0, 5);
    if (value == scalingFilter()) return;
    recordUndo();
    scalingFilter_ = static_cast<core::ScalingFilter>(value);
    settingsWereChanged(true);
}

void ImageInputController::setFillMode(int value)
{
    value = std::clamp(value, 0, 3);
    if (value == fillMode()) return;
    recordUndo();
    fillMode_ = static_cast<core::ImageFillMode>(value);
    settingsWereChanged(true);
}

void ImageInputController::setExportFormat(int value)
{
    value = std::clamp(value, 0, 8);
    if (value == exportFormat_) return;
    exportFormat_ = value;
    saveSettings();
    updateExportSummary();
}

void ImageInputController::setPerceptualColorMatching(bool value)
{
    if (value == settings_.perceptualColorMatching) return;
    recordUndo();
    settings_.perceptualColorMatching = value;
    settingsWereChanged();
}

void ImageInputController::setStretchHistogram(bool value)
{
    if (value == settings_.stretchHistogram) return;
    recordUndo();
    settings_.stretchHistogram = value;
    settingsWereChanged();
}

void ImageInputController::setPerceptualRedWeight(int value)
{
    value = std::clamp(value, 0, 100);
    if (value == perceptualRedWeight()) return;
    recordUndo();
    settings_.perceptualRedWeight = static_cast<double>(value) / 100.0;
    settingsWereChanged();
}

void ImageInputController::setPerceptualGreenWeight(int value)
{
    value = std::clamp(value, 0, 100);
    if (value == perceptualGreenWeight()) return;
    recordUndo();
    settings_.perceptualGreenWeight = static_cast<double>(value) / 100.0;
    settingsWereChanged();
}

void ImageInputController::setPerceptualBlueWeight(int value)
{
    value = std::clamp(value, 0, 100);
    if (value == perceptualBlueWeight()) return;
    recordUndo();
    settings_.perceptualBlueWeight = static_cast<double>(value) / 100.0;
    settingsWereChanged();
}

void ImageInputController::restorePerceptualWeights()
{
    if (perceptualRedWeight() == 30 && perceptualGreenWeight() == 52
        && perceptualBlueWeight() == 18) return;
    recordUndo();
    settings_.perceptualRedWeight = 0.30;
    settings_.perceptualGreenWeight = 0.52;
    settings_.perceptualBlueWeight = 0.18;
    settingsWereChanged();
}

void ImageInputController::setMaximumColorShift(double value)
{
    value = std::clamp(value, 0.0, 100.0);
    if (qFuzzyCompare(value + 1.0, settings_.maximumColorShiftPercent + 1.0)) return;
    recordUndo();
    settings_.maximumColorShiftPercent = value;
    settingsWereChanged();
}

void ImageInputController::setGamma(double value)
{
    value = std::clamp(value, 0.1, 5.0);
    if (qFuzzyCompare(value, settings_.gamma)) return;
    recordUndo();
    settings_.gamma = value;
    settingsWereChanged();
}

void ImageInputController::setLumaEmphasis(double value)
{
    value = std::clamp(value, 0.0, 10.0);
    if (qFuzzyCompare(value + 1.0, settings_.lumaEmphasis + 1.0)) return;
    recordUndo();
    settings_.lumaEmphasis = value;
    settingsWereChanged();
}

void ImageInputController::setMaximumMulticolorDifference(int value)
{
    value = std::clamp(value, 0, 100);
    if (value == settings_.maximumMulticolorDifferencePercent) return;
    recordUndo();
    settings_.maximumMulticolorDifferencePercent = value;
    settingsWereChanged();
}

void ImageInputController::setOrderedBrightness(int value)
{
    value = std::clamp(value, 0, 16);
    if (value == settings_.orderedDitherBrightness) return;
    recordUndo();
    settings_.orderedDitherBrightness = value;
    settingsWereChanged();
}

void ImageInputController::setErrorAccumulationMode(int value)
{
    value = std::clamp(value, 0, 1);
    if (value == errorAccumulationMode()) return;
    recordUndo();
    settings_.errorAccumulation = static_cast<core::ErrorAccumulationMode>(value);
    settingsWereChanged();
}

void ImageInputController::setOrderedDitherMapSize(int value)
{
    const auto size = value == 4 ? core::OrderedDitherMapSize::FourByFour
                                 : core::OrderedDitherMapSize::TwoByTwo;
    if (size == settings_.orderedDitherMapSize) return;
    recordUndo();
    settings_.orderedDitherMapSize = size;
    settingsWereChanged();
}

void ImageInputController::setErrorDistributionWeight(int index, int value)
{
    value = std::clamp(value, 0, 16);
    std::uint8_t* weight = nullptr;
    switch (index) {
    case 0: weight = &settings_.errorDistribution.downLeft; break;
    case 1: weight = &settings_.errorDistribution.down; break;
    case 2: weight = &settings_.errorDistribution.downRight; break;
    case 3: weight = &settings_.errorDistribution.right; break;
    case 4: weight = &settings_.errorDistribution.farRight; break;
    case 5: weight = &settings_.errorDistribution.downTwo; break;
    default: return;
    }
    if (*weight == value && settings_.dither == core::DitherMode::Custom) return;
    recordUndo();
    *weight = static_cast<std::uint8_t>(value);
    settings_.dither = core::DitherMode::Custom;
    settingsWereChanged();
}

void ImageInputController::setErrorDownLeft(int value)
{
    setErrorDistributionWeight(0, value);
}

void ImageInputController::setErrorDown(int value)
{
    setErrorDistributionWeight(1, value);
}

void ImageInputController::setErrorDownRight(int value)
{
    setErrorDistributionWeight(2, value);
}

void ImageInputController::setErrorRight(int value)
{
    setErrorDistributionWeight(3, value);
}

void ImageInputController::setErrorFarRight(int value)
{
    setErrorDistributionWeight(4, value);
}

void ImageInputController::setErrorDownTwo(int value)
{
    setErrorDistributionWeight(5, value);
}

void ImageInputController::setPaletteSelectionMode(int value)
{
    value = std::clamp(value, 0, 1);
    if (value == paletteSelectionMode()) return;
    recordUndo();
    settings_.paletteSelection = static_cast<core::PaletteSelectionMode>(value);
    settingsWereChanged();
}

void ImageInputController::setScanlineStaticColorCount(int value)
{
    value = std::clamp(value, 0, 14);
    if (value == scanlineStaticColorCount()) return;
    recordUndo();
    settings_.scanlineStaticColorCount = value;
    if (value > 0 && !settings_.scanlineRegion1 && !settings_.scanlineRegion2
        && !settings_.scanlineRegion3) {
        settings_.scanlineRegion1 = true;
    }
    settingsWereChanged();
}

void ImageInputController::setScanlineRegion1(bool value)
{
    if (value == settings_.scanlineRegion1) return;
    recordUndo();
    settings_.scanlineRegion1 = value;
    settingsWereChanged();
}

void ImageInputController::setScanlineRegion2(bool value)
{
    if (value == settings_.scanlineRegion2) return;
    recordUndo();
    settings_.scanlineRegion2 = value;
    settingsWereChanged();
}

void ImageInputController::setScanlineRegion3(bool value)
{
    if (value == settings_.scanlineRegion3) return;
    recordUndo();
    settings_.scanlineRegion3 = value;
    settingsWereChanged();
}

void ImageInputController::setPowerPaintFraming(bool value)
{
    if (value == powerPaintFraming_) return;
    recordUndo();
    powerPaintFraming_ = value;
    settingsWereChanged(true);
}

void ImageInputController::setHorizontalOffset(int value)
{
    value = std::clamp(value, -256, 256);
    if (value == horizontalOffset_) return;
    recordUndo();
    horizontalOffset_ = value;
    settingsWereChanged(true);
}

void ImageInputController::setVerticalOffset(int value)
{
    value = std::clamp(value, -192, 192);
    if (value == verticalOffset_) return;
    recordUndo();
    verticalOffset_ = value;
    settingsWereChanged(true);
}

void ImageInputController::setAutoUpdate(bool value)
{
    if (value == autoUpdate_) return;
    autoUpdate_ = value;
    saveSettings();
    emit settingsChanged();
    if (autoUpdate_) {
        if (conversionPending_) scheduleConversion();
        return;
    }
    const bool updateWasInFlight = busy_ || debounceTimer_.isActive();
    jobs_.cancelCurrent();
    debounceTimer_.stop();
    busy_ = false;
    conversionPending_ = updateWasInFlight;
    if (conversionPending_) {
        statusMessage_ = tr("Automatic updates are off. Select Update to convert.");
    } else if (image_) {
        statusMessage_ = tr("Automatic updates are off.");
    }
    emit conversionChanged();
    emit statusChanged();
}

void ImageInputController::setLivePreview(bool value)
{
    if (value == livePreview_) return;
    const bool conversionWasInFlight = busy_ || debounceTimer_.isActive();
    livePreview_ = value;
    saveSettings();
    emit settingsChanged();
    if (livePreview_ && conversionWasInFlight) scheduleConversion();
}

void ImageInputController::setBackgroundColor(const QColor& value)
{
    if (!value.isValid()) return;
    const core::RgbColor selected{static_cast<std::uint8_t>(value.red()),
                                  static_cast<std::uint8_t>(value.green()),
                                  static_cast<std::uint8_t>(value.blue())};
    if (selected == backgroundColor_) return;
    recordUndo();
    backgroundColor_ = selected;
    settingsWereChanged(true);
}

void ImageInputController::setForegroundColor(const QColor& value)
{
    if (!value.isValid()) return;
    const core::RgbColor selected{static_cast<std::uint8_t>(value.red()),
                                  static_cast<std::uint8_t>(value.green()),
                                  static_cast<std::uint8_t>(value.blue())};
    if (selected == foregroundColor_) return;
    recordUndo();
    foregroundColor_ = selected;
    saveSettings();
    emit settingsChanged();
}

void ImageInputController::setScreenImageBackgroundColor(const QColor& value)
{
    if (!value.isValid()) return;
    const core::RgbColor selected{static_cast<std::uint8_t>(value.red()),
                                  static_cast<std::uint8_t>(value.green()),
                                  static_cast<std::uint8_t>(value.blue())};
    if (selected == screenImageBackgroundColor_) return;
    screenImageBackgroundColor_ = selected;
    emit screenImageColorsChanged();
}

void ImageInputController::setScreenImageForegroundColor(const QColor& value)
{
    if (!value.isValid()) return;
    const core::RgbColor selected{static_cast<std::uint8_t>(value.red()),
                                  static_cast<std::uint8_t>(value.green()),
                                  static_cast<std::uint8_t>(value.blue())};
    if (selected == screenImageForegroundColor_) return;
    screenImageForegroundColor_ = selected;
    emit screenImageColorsChanged();
}

void ImageInputController::setWorkingPaletteColor(int index, const QColor& color)
{
    if (index < 0 || static_cast<std::size_t>(index) >= workingPalette_.size()
        || !color.isValid()) return;
    const core::RgbColor selected{static_cast<std::uint8_t>(color.red()),
                                  static_cast<std::uint8_t>(color.green()),
                                  static_cast<std::uint8_t>(color.blue())};
    if (workingPalette_[static_cast<std::size_t>(index)] == selected) return;
    recordUndo();
    workingPalette_[static_cast<std::size_t>(index)] = selected;
    backgroundColor_ = nearestBackgroundColor(backgroundColor(), workingPalette_);
    settingsWereChanged(true);
}

void ImageInputController::resetWorkingPalette()
{
    const auto defaults = defaultWorkingColors();
    if (workingPalette_ == defaults) return;
    recordUndo();
    workingPalette_ = defaults;
    backgroundColor_ = nearestBackgroundColor(backgroundColor(), workingPalette_);
    settingsWereChanged(true);
}
