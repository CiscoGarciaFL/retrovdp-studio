#include "EditorProjectController.hpp"
#include "ImageInputController.hpp"

#include "retrovdp/core/ConversionSettings.hpp"

#include <QColor>
#include <QDir>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

QString exportBase(QString name, QString fallback)
{
    name = name.trimmed().toUpper();
    for (QChar& character : name) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('_')
            && character != QLatin1Char('-')) character = QLatin1Char('_');
    }
    while (name.contains(QStringLiteral("__")))
        name.replace(QStringLiteral("__"), QStringLiteral("_"));
    return name.isEmpty() ? std::move(fallback) : name;
}

void putLittleWord(QByteArray& bytes, int offset, std::uint16_t value)
{
    bytes[offset] = static_cast<char>(value);
    bytes[offset + 1] = static_cast<char>(value >> 8U);
}

void putBigWord(QByteArray& bytes, int wordAddress, std::uint16_t value)
{
    const int offset = wordAddress * 2;
    bytes[offset] = static_cast<char>(value >> 8U);
    bytes[offset + 1] = static_cast<char>(value);
}

std::uint16_t neoGeoColorWord(const QColor& color)
{
    const std::uint16_t red = static_cast<std::uint16_t>(color.red() * 31 / 255);
    const std::uint16_t green = static_cast<std::uint16_t>(color.green() * 31 / 255);
    const std::uint16_t blue = static_cast<std::uint16_t>(color.blue() * 31 / 255);
    return static_cast<std::uint16_t>(((red >> 1U) << 8U)
        | ((green >> 1U) << 4U) | (blue >> 1U)
        | ((red & 1U) << 14U) | ((green & 1U) << 13U)
        | ((blue & 1U) << 12U));
}

QByteArray neoGeoTile(const std::uint8_t* pixels, int stride, int height = 16)
{
    QByteArray result(height * 8, '\0');
    for (int y = 0; y < height; ++y) {
        for (int half = 0; half < 2; ++half) {
            for (int plane = 0; plane < 4; ++plane) {
                std::uint8_t value = 0;
                for (int x = 0; x < 8; ++x) {
                    if ((pixels[y * stride + half * 8 + x] & (1U << plane)) != 0U)
                        value = static_cast<std::uint8_t>(value | (0x80U >> x));
                }
                result[y * 8 + half * 4 + plane] = static_cast<char>(value);
            }
        }
    }
    return result;
}

QByteArray neoGeoFixTile(const std::uint8_t* pixels, int stride)
{
    QByteArray result(32, '\0');
    for (int y = 0; y < 8; ++y) {
        for (int plane = 0; plane < 4; ++plane) {
            std::uint8_t value = 0;
            for (int x = 0; x < 8; ++x) {
                if ((pixels[y * stride + x] & (1U << plane)) != 0U)
                    value = static_cast<std::uint8_t>(value | (0x80U >> x));
            }
            result[y * 4 + plane] = static_cast<char>(value);
        }
    }
    return result;
}

} // namespace

bool EditorProjectController::exportObjectProcessorEditorAssets(const QUrl& directoryUrl)
{
    const bool lynx = usesAtariLynxEditor();
    const bool neoGeo = usesNeoGeoEditor();
    if (!lynx && !neoGeo) {
        setStatus({}, QStringLiteral(
            "Select an Atari Lynx or Neo Geo object-processor target before exporting native editor assets."));
        return false;
    }
    const QString path = directoryUrl.toLocalFile();
    QDir directory(path);
    if (path.isEmpty() || !directory.exists()) {
        setStatus({}, QStringLiteral("Choose an existing local export folder."));
        return false;
    }
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();

    const QString base = exportBase(projectName_,
        lynx ? QStringLiteral("LYNX") : QStringLiteral("NEOGEO"));
    const auto writeFile = [&](const QString& suffix, const QByteArray& data) {
        QSaveFile file(directory.filePath(base + suffix));
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
            || !file.commit()) {
            setStatus({}, QStringLiteral("Could not write %1: %2")
                              .arg(file.fileName(), file.errorString()));
            return false;
        }
        return true;
    };

    const auto& spriteSet = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)];
    const QVariantList colors = imageInput_->paletteColors();
    if (lynx) {
        QByteArray lineData;
        QByteArray scbs;
        int objectCount = 0;
        for (int index = 0; index < spritePatternsPerSet(); ++index) {
            const auto& placement = spriteSet.placements[static_cast<std::size_t>(index)];
            if (!placement.visible) continue;
            const int encodedSize = normalizedSpriteSize(placement.size);
            const int width = spritePatternWidth(encodedSize);
            const int height = spritePatternHeight(encodedSize);
            const auto& pixels = visibleSpritePixels(
                spritePattern(activeSpriteSet_, index, encodedSize));
            const int dataOffset = lineData.size();
            for (int y = 0; y < height; ++y) {
                lineData.append(static_cast<char>(1 + (width + 1) / 2));
                for (int x = 0; x < width; x += 2) {
                    const std::uint8_t high = pixels[static_cast<std::size_t>(y * width + x)] & 0x0fU;
                    const std::uint8_t low = x + 1 < width
                        ? pixels[static_cast<std::size_t>(y * width + x + 1)] & 0x0fU : 0U;
                    lineData.append(static_cast<char>((high << 4U) | low));
                }
            }
            lineData.append('\0');

            QByteArray scb(23, '\0');
            scb[0] = static_cast<char>(0xc4U);
            scb[1] = static_cast<char>(0xb0U);
            scb[2] = 0;
            putLittleWord(scb, 3, static_cast<std::uint16_t>((objectCount + 1) * 23));
            putLittleWord(scb, 5, static_cast<std::uint16_t>(dataOffset));
            putLittleWord(scb, 7, static_cast<std::uint16_t>(placement.x));
            putLittleWord(scb, 9, static_cast<std::uint16_t>(placement.y));
            putLittleWord(scb, 11, static_cast<std::uint16_t>(
                placement.horizontalScale * 256 / 100));
            putLittleWord(scb, 13, static_cast<std::uint16_t>(
                placement.verticalScale * 256 / 100));
            for (int pair = 0; pair < 8; ++pair)
                scb[15 + pair] = static_cast<char>((pair * 2 << 4) | (pair * 2 + 1));
            scbs.append(scb);
            ++objectCount;
        }
        if (!scbs.isEmpty()) putLittleWord(scbs, scbs.size() - 20, 0U);

        QByteArray palette(32, '\0');
        for (int index = 0; index < 16; ++index) {
            const QColor color = index < colors.size()
                ? colors.at(index).value<QColor>() : QColor(Qt::black);
            palette[index] = static_cast<char>(color.green() * 15 / 255);
            palette[16 + index] = static_cast<char>(
                ((color.blue() * 15 / 255) << 4) | (color.red() * 15 / 255));
        }
        QByteArray registers(16, '\0');
        registers[0] = 'R'; registers[1] = 'V';
        registers[2] = 'D'; registers[3] = 'P';
        registers[4] = static_cast<char>(retrovdp::core::ConversionMode::AtariLynxObjectScene);
        registers[5] = static_cast<char>(objectCount);
        registers[6] = static_cast<char>(160);
        registers[7] = static_cast<char>(102);
        if (!writeFile(QStringLiteral(".SPR"), lineData)
            || !writeFile(QStringLiteral(".SCB"), scbs)
            || !writeFile(QStringLiteral(".PAL"), palette)
            || !writeFile(QStringLiteral(".REG"), registers)) return false;
        setStatus(QStringLiteral(
            "Exported %1 linked Lynx object%2 with line data, SCBs, palette, and metadata to %3")
                      .arg(objectCount).arg(objectCount == 1 ? QString() : QStringLiteral("s"),
                           QDir::toNativeSeparators(directory.absolutePath())));
        return true;
    }

    QByteArray spritePatterns;
    QByteArray vram(0x8600 * 2, '\0');
    int objectCount = 0;
    int nextTile = 0;
    for (int index = 0; index < spritePatternsPerSet(); ++index) {
        const auto& placement = spriteSet.placements[static_cast<std::size_t>(index)];
        if (!placement.visible) continue;
        const int encodedSize = normalizedSpriteSize(placement.size);
        const int height = spritePatternHeight(encodedSize);
        const int tileRows = height / 16;
        const auto& pixels = visibleSpritePixels(
            spritePattern(activeSpriteSet_, index, encodedSize));
        for (int row = 0; row < tileRows; ++row) {
            spritePatterns.append(neoGeoTile(
                pixels.data() + static_cast<std::ptrdiff_t>(row * 16 * 16), 16));
            const int cell = objectCount * 64 + row * 2;
            putBigWord(vram, cell, static_cast<std::uint16_t>(nextTile++));
            putBigWord(vram, cell + 1, static_cast<std::uint16_t>(
                ((placement.palette & 0xff) << 8)
                | (placement.flipX ? 0x0001 : 0)
                | (placement.flipY ? 0x0002 : 0)));
        }
        const std::uint16_t zoom = static_cast<std::uint16_t>(
            ((placement.horizontalScale * 15 / 100) << 8)
            | (placement.verticalScale * 255 / 100));
        putBigWord(vram, 0x8000 + objectCount, zoom);
        putBigWord(vram, 0x8200 + objectCount, static_cast<std::uint16_t>(
            (((496 - placement.y) & 0x01ff) << 7)
            | (placement.chained && objectCount > 0 ? 0x0040 : 0)
            | (tileRows & 0x3f)));
        putBigWord(vram, 0x8400 + objectCount,
                   static_cast<std::uint16_t>((placement.x & 0x01ff) << 7));
        ++objectCount;
    }

    QByteArray fixPatterns(4096 * 32, '\0');
    const auto& characterSet = characterSets_[static_cast<std::size_t>(activeCharacterSet_)];
    for (int patternIndex = 0; patternIndex < 4096; ++patternIndex) {
        const auto& pattern = characterSet.patterns[static_cast<std::size_t>(patternIndex)];
        std::array<std::uint8_t, 64> pixels{};
        for (int y = 0; y < 8; ++y) {
            const int foreground = pattern.colors[static_cast<std::size_t>(y)] >> 4U;
            const int background = pattern.colors[static_cast<std::size_t>(y)] & 0x0fU;
            for (int x = 0; x < 8; ++x) {
                pixels[static_cast<std::size_t>(y * 8 + x)] = pattern.indexedOverride
                    ? pattern.indexedPixels[static_cast<std::size_t>(y * 8 + x)]
                    : static_cast<std::uint8_t>(
                        (pattern.bitmap[static_cast<std::size_t>(y)] & (0x80U >> x))
                            ? foreground : background);
            }
        }
        const QByteArray tile = neoGeoFixTile(pixels.data(), 8);
        std::copy(tile.cbegin(), tile.cend(), fixPatterns.begin() + patternIndex * 32);
    }
    QByteArray fixMap(8192, '\0');
    for (const auto& slot : characterEditorSlots_) {
        if (!slot.loaded || slot.setIndex != activeCharacterSet_) continue;
        const int column = slot.tileX / 8;
        const int row = slot.tileY / 8;
        if (column < 0 || column >= 40 || row < 0 || row >= 28) continue;
        putBigWord(fixMap, column * 32 + row, static_cast<std::uint16_t>(
            ((slot.palette & 0x0f) << 12) | (slot.patternIndex & 0x0fff)));
    }
    QByteArray palette(4096 * 2, '\0');
    putBigWord(palette, 0, 0x8000U);
    for (int index = 1; index < std::min(4095, static_cast<int>(colors.size())); ++index)
        putBigWord(palette, index, neoGeoColorWord(colors.at(index).value<QColor>()));
    putBigWord(palette, 4095, 0x8000U);
    QByteArray registers(16, '\0');
    registers[0] = 'R'; registers[1] = 'V';
    registers[2] = 'D'; registers[3] = 'P';
    registers[4] = static_cast<char>(retrovdp::core::ConversionMode::NeoGeoChainedSprites);
    registers[5] = static_cast<char>(objectCount);
    registers[6] = static_cast<char>(40);
    registers[7] = static_cast<char>(28);
    registers[8] = static_cast<char>(96);
    if (!writeFile(QStringLiteral(".SPR"), spritePatterns)
        || !writeFile(QStringLiteral(".VRAM"), vram)
        || !writeFile(QStringLiteral(".FIX"), fixPatterns)
        || !writeFile(QStringLiteral(".FIXMAP"), fixMap)
        || !writeFile(QStringLiteral(".PAL"), palette)
        || !writeFile(QStringLiteral(".REG"), registers)) return false;
    setStatus(QStringLiteral(
        "Exported %1 Neo Geo object strip%2 with sprite data, VRAM, FIX layer, palette, and metadata to %3")
                  .arg(objectCount).arg(objectCount == 1 ? QString() : QStringLiteral("s"),
                       QDir::toNativeSeparators(directory.absolutePath())));
    return true;
}
