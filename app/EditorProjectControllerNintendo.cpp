#include "EditorProjectController.hpp"
#include "ImageInputController.hpp"

#include "retrovdp/core/ConversionSettings.hpp"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace {

QByteArray planarTile(const std::uint8_t* pixels, int stride, int bitsPerPixel)
{
    QByteArray result(bitsPerPixel * 8, '\0');
    for (int plane = 0; plane < bitsPerPixel; ++plane) {
        for (int y = 0; y < 8; ++y) {
            std::uint8_t value = 0;
            for (int x = 0; x < 8; ++x) {
                if ((pixels[y * stride + x] & (1U << plane)) != 0U)
                    value = static_cast<std::uint8_t>(value | (0x80U >> x));
            }
            const int offset = bitsPerPixel == 2
                ? y * 2 + plane
                : (plane / 2) * 16 + y * 2 + plane % 2;
            result[offset] = static_cast<char>(value);
        }
    }
    return result;
}

QByteArray rgb555Palette(const QVariantList& colors, int count, int start = 0)
{
    QByteArray result(count * 2, '\0');
    for (int index = 0; index < count; ++index) {
        const QColor color = start + index < colors.size()
            ? colors.at(start + index).value<QColor>() : QColor(Qt::black);
        const std::uint16_t word = static_cast<std::uint16_t>(
            (color.red() * 31 / 255)
            | ((color.green() * 31 / 255) << 5)
            | ((color.blue() * 31 / 255) << 10));
        result[index * 2] = static_cast<char>(word);
        result[index * 2 + 1] = static_cast<char>(word >> 8U);
    }
    return result;
}

QString exportBase(QString name)
{
    name = name.trimmed().toUpper();
    for (QChar& character : name) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('_')
            && character != QLatin1Char('-')) character = QLatin1Char('_');
    }
    while (name.contains(QStringLiteral("__")))
        name.replace(QStringLiteral("__"), QStringLiteral("_"));
    return name.isEmpty() ? QStringLiteral("NINTENDO") : name;
}

} // namespace

bool EditorProjectController::exportNintendoEditorAssets(const QUrl& directoryUrl)
{
    const bool gameBoy = usesGameBoyEditor();
    const bool gameBoyColor = usesGameBoyColorEditor();
    const bool superNes = usesSuperNesEditor();
    if (!gameBoy && !gameBoyColor && !superNes) {
        setStatus({}, QStringLiteral("Select a Nintendo PPU target before exporting native editor assets."));
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

    const int characterBits = superNes
        ? (imageInput_->conversionMode()
               == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode0Background)
               ? 2
               : imageInput_->conversionMode()
                   == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode3Background)
                   ? 8 : 4)
        : 2;
    const int characterCount = superNes && characterBits == 8
        ? 896 : characterPatternsPerSet();
    QByteArray characters;
    characters.reserve(characterCount * characterBits * 8);
    const auto& characterSet = characterSets_[static_cast<std::size_t>(activeCharacterSet_)];
    for (int patternIndex = 0; patternIndex < characterCount; ++patternIndex) {
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
        characters.append(planarTile(pixels.data(), 8, characterBits));
    }

    QByteArray map(superNes ? 2048 : 1024, '\0');
    QByteArray attributes(gameBoyColor ? 1024 : 0, '\0');
    std::vector<bool> occupied(1024);
    bool requiresUnsignedTileAddressing = false;
    bool requiresSignedTileAddressing = false;
    for (const auto& slot : characterEditorSlots_) {
        if (!slot.loaded) continue;
        if (slot.setIndex != activeCharacterSet_) {
            setStatus({}, QStringLiteral("The Nintendo map references more than one pattern set."));
            return false;
        }
        if (slot.patternIndex >= characterCount) {
            setStatus({}, QStringLiteral(
                "The active Super NES Mode 3 VRAM layout can address patterns 0-895 while retaining its tile map."));
            return false;
        }
        const int column = slot.tileX / 8;
        const int row = slot.tileY / 8;
        if (column < 0 || column >= 32 || row < 0 || row >= 32) continue;
        const int cell = row * 32 + column;
        if (occupied[static_cast<std::size_t>(cell)]) {
            setStatus({}, QStringLiteral("Two character tiles occupy the same Nintendo map cell."));
            return false;
        }
        occupied[static_cast<std::size_t>(cell)] = true;
        if (superNes) {
            const std::uint16_t word = static_cast<std::uint16_t>(
                (slot.patternIndex & 0x03ff) | ((slot.palette & 7) << 10)
                | (slot.priority ? 0x2000 : 0) | (slot.flipX ? 0x4000 : 0)
                | (slot.flipY ? 0x8000 : 0));
            map[cell * 2] = static_cast<char>(word);
            map[cell * 2 + 1] = static_cast<char>(word >> 8U);
        } else {
            const int localPattern = gameBoyColor
                ? slot.patternIndex % 384 : slot.patternIndex;
            requiresUnsignedTileAddressing |= localPattern < 128;
            requiresSignedTileAddressing |= localPattern >= 256;
            if (requiresUnsignedTileAddressing && requiresSignedTileAddressing) {
                setStatus({}, QStringLiteral(
                    "The GB/GBC map mixes tiles that require unsigned (0-127) and signed (256-383) addressing. "
                    "Tiles 128-255 work in either mode; move one conflicting range before export."));
                return false;
            }
            map[cell] = static_cast<char>(localPattern & 0xff);
            if (gameBoyColor) {
                attributes[cell] = static_cast<char>(
                    (slot.palette & 7) | ((slot.patternIndex / 384) != 0 ? 0x08 : 0)
                    | (slot.flipX ? 0x20 : 0) | (slot.flipY ? 0x40 : 0)
                    | (slot.priority ? 0x80 : 0));
            }
        }
    }

    const auto& spriteSet = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)];
    const int spriteCount = spritePatternsPerSet();
    QByteArray spritePatterns(superNes ? 512 * 32 : 0, '\0');
    QByteArray oam(superNes ? 544 : 160, '\0');
    int snesLarge = 16;
    int snesSizeCode = 0;
    if (superNes) {
        std::vector<int> usedSizes;
        for (int index = 0; index < spriteCount; ++index) {
            if (!spriteSet.placements[static_cast<std::size_t>(index)].visible) continue;
            const int encoded = normalizedSpriteSize(
                spriteSet.placements[static_cast<std::size_t>(index)].size);
            const int size = encoded / 100;
            if (std::ranges::find(usedSizes, size) == usedSizes.end()) usedSizes.push_back(size);
        }
        std::ranges::sort(usedSizes);
        constexpr std::array pairs{
            std::pair{8, 16}, std::pair{8, 32}, std::pair{8, 64},
            std::pair{16, 32}, std::pair{16, 64}, std::pair{32, 64}};
        bool found = false;
        for (int code = 0; code < static_cast<int>(pairs.size()); ++code) {
            const auto [small, large] = pairs[static_cast<std::size_t>(code)];
            if (std::ranges::all_of(usedSizes, [small, large](int size) {
                    return size == small || size == large;
                })) {
                snesLarge = large;
                snesSizeCode = code;
                found = true;
                break;
            }
        }
        if (!found) {
            setStatus({}, QStringLiteral("Visible Super NES sprites must fit one hardware small/large size pair."));
            return false;
        }
    }

    int nextTile = 0;
    int snesCursorX = 0;
    int snesCursorY = 0;
    int snesShelfHeight = 0;
    for (int index = 0; index < spriteCount; ++index) {
        const auto& placement = spriteSet.placements[static_cast<std::size_t>(index)];
        const int encodedSize = normalizedSpriteSize(placement.size);
        const int width = superNes ? encodedSize / 100 : 8;
        const int height = superNes ? encodedSize % 100 : (spriteGlobalSize_ >= 16 ? 16 : 8);
        const auto& pattern = spritePattern(activeSpriteSet_, index, encodedSize);
        const auto& pixels = visibleSpritePixels(pattern);
        int firstTile = nextTile;
        if (superNes) {
            const int tileWidth = width / 8;
            const int tileHeight = height / 8;
            if (snesCursorX + tileWidth > 16) {
                snesCursorY += snesShelfHeight;
                snesCursorX = 0;
                snesShelfHeight = 0;
            }
            if (snesCursorY + tileHeight > 32) {
                setStatus({}, QStringLiteral(
                    "The authored Super NES sprite patterns do not fit the 16x32-tile OAM name space."));
                return false;
            }
            firstTile = snesCursorY * 16 + snesCursorX;
            snesCursorX += tileWidth;
            snesShelfHeight = std::max(snesShelfHeight, tileHeight);
        }
        for (int tileY = 0; tileY < height; tileY += 8) {
            for (int tileX = 0; tileX < width; tileX += 8) {
                std::array<std::uint8_t, 64> tile{};
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        tile[static_cast<std::size_t>(y * 8 + x)] = pixels[
                            static_cast<std::size_t>((tileY + y) * width + tileX + x)];
                const QByteArray encoded = planarTile(tile.data(), 8, superNes ? 4 : 2);
                if (superNes) {
                    const int tileIndex = firstTile + (tileY / 8) * 16 + tileX / 8;
                    std::copy(encoded.cbegin(), encoded.cend(),
                              spritePatterns.begin() + tileIndex * 32);
                } else {
                    spritePatterns.append(encoded);
                    ++nextTile;
                }
            }
        }
        if (superNes) {
            const int offset = index * 4;
            oam[offset] = static_cast<char>(placement.x & 0xff);
            oam[offset + 1] = static_cast<char>(
                placement.visible ? placement.y & 0xff : 0xf0);
            oam[offset + 2] = static_cast<char>(firstTile & 0xff);
            oam[offset + 3] = static_cast<char>(
                ((firstTile >> 8) & 1) | ((placement.palette & 7) << 1)
                | (placement.priority ? 0x30 : 0)
                | (placement.flipX ? 0x40 : 0) | (placement.flipY ? 0x80 : 0));
            const int highOffset = 512 + index / 4;
            const int shift = (index % 4) * 2;
            const int highBits = ((placement.x >> 8) & 1)
                | ((width == snesLarge ? 1 : 0) << 1);
            oam[highOffset] = static_cast<char>(
                static_cast<unsigned char>(oam[highOffset]) | (highBits << shift));
        } else {
            const int offset = index * 4;
            oam[offset] = static_cast<char>(
                placement.visible ? (placement.y + 16) & 0xff : 0);
            oam[offset + 1] = static_cast<char>((placement.x + 8) & 0xff);
            oam[offset + 2] = static_cast<char>(firstTile & 0xff);
            oam[offset + 3] = static_cast<char>(
                (placement.priority ? 0x80 : 0) | (placement.flipY ? 0x40 : 0)
                | (placement.flipX ? 0x20 : 0)
                | (gameBoyColor ? placement.palette & 7 : (placement.palette & 1) << 4));
        }
    }

    const QVariantList colors = imageInput_->paletteColors();
    QByteArray palette = gameBoy ? QByteArray::fromHex("e4e4e4")
        : rgb555Palette(colors, superNes ? 256 : 64);
    QByteArray registers(superNes ? 64 : 12, '\0');
    if (superNes) {
        registers[0x01] = static_cast<char>(snesSizeCode << 5);
        registers[0x05] = static_cast<char>(imageInput_->conversionMode()
            == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode0Background)
                ? 0 : imageInput_->conversionMode()
                    == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode3Background)
                    ? 3 : 1);
        registers[0x07] = 0;
        registers[0x0b] = 0x01;
        registers[0x2c] = 0x11;
    } else {
        const std::uint8_t tileAddressing = requiresSignedTileAddressing ? 0x00U : 0x10U;
        registers[0] = static_cast<char>(
            0x81U | tileAddressing | (spriteGlobalSize_ >= 16 ? 0x04U : 0U));
    }

    const QString base = exportBase(projectName_);
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
    if (!writeFile(QStringLiteral(".CHR"), characters)
        || !writeFile(QStringLiteral(".MAP"), map)
        || (gameBoyColor && !writeFile(QStringLiteral(".ATTR"), attributes))
        || !writeFile(QStringLiteral(".PAL"), palette)
        || !writeFile(QStringLiteral(".SPR"), spritePatterns)
        || !writeFile(QStringLiteral(".OAM"), oam)
        || !writeFile(QStringLiteral(".REG"), registers)) return false;

    setStatus(QStringLiteral("Exported native Nintendo character, map, palette, sprite, OAM, and register assets to %1")
                  .arg(QDir::toNativeSeparators(directory.absolutePath())));
    return true;
}
