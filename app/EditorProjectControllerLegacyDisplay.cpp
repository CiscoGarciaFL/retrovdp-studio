#include "EditorProjectController.hpp"

#include "ImageInputController.hpp"
#include "retrovdp/core/ConversionSettings.hpp"

#include <QDir>
#include <QSaveFile>

#include <algorithm>

namespace {

bool writeFile(const QDir& directory, const QString& name, const QByteArray& bytes)
{
    QSaveFile file(directory.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

qsizetype appleTextOffset(int row, int column)
{
    return static_cast<qsizetype>((row & 7) * 0x80 + (row >> 3) * 40 + column);
}

} // namespace

bool EditorProjectController::exportLegacyDisplayEditorAssets(const QUrl& directoryUrl)
{
    using retrovdp::core::TargetProfileId;
    const auto profile = static_cast<TargetProfileId>(activeTarget());
    if (profile < TargetProfileId::AmstradCpc || profile > TargetProfileId::Trs80Model1_3) {
        setStatus({}, QStringLiteral("The active target is not a display-adapter or character-display target."));
        return false;
    }
    const QString path = directoryUrl.toLocalFile();
    QDir directory(path);
    if (path.isEmpty() || (!directory.exists() && !directory.mkpath(QStringLiteral(".")))) {
        setStatus({}, QStringLiteral("The export directory is not available."));
        return false;
    }

    const int patternCount = characterPatternsPerSet();
    const int patternWidth = characterPatternWidth();
    const int patternHeight = characterPatternHeight();
    QByteArray patterns(patternCount * patternHeight, '\0');
    const auto& set = characterSets_[static_cast<std::size_t>(activeCharacterSet_)];
    for (int patternIndex = 0; patternIndex < patternCount; ++patternIndex) {
        const auto& pattern = set.patterns[static_cast<std::size_t>(patternIndex)];
        for (int row = 0; row < patternHeight; ++row) {
            std::uint8_t value = pattern.bitmap[static_cast<std::size_t>(row)];
            if (patternWidth == 7) value &= 0xfeU;
            patterns[patternIndex * patternHeight + row] = static_cast<char>(value);
        }
    }

    const int columns = characterMapColumns();
    const int rows = characterMapRows();
    QByteArray codes(columns * rows, '\0');
    for (const auto& slot : characterEditorSlots_) {
        if (!slot.loaded) continue;
        const int column = std::clamp(slot.tileX / 8, 0, columns - 1);
        const int row = std::clamp(slot.tileY / 8, 0, rows - 1);
        codes[row * columns + column] = static_cast<char>(
            slot.patternIndex % std::max(1, patternCount));
    }

    QString patternName;
    QString mapName;
    QByteArray map;
    if (profile == TargetProfileId::AmstradCpc) {
        patternName = QStringLiteral("CPC_CHARACTERS.CHR");
        mapName = QStringLiteral("CPC_SCREEN.MAP");
        map = codes;
    } else if (profile == TargetProfileId::IbmCga
               || profile == TargetProfileId::IbmEga) {
        patternName = profile == TargetProfileId::IbmCga
            ? QStringLiteral("CGA_FONT.CHR") : QStringLiteral("EGA_FONT.CHR");
        mapName = profile == TargetProfileId::IbmCga
            ? QStringLiteral("CGA_TEXT.VRAM") : QStringLiteral("EGA_TEXT.VRAM");
        map.resize(codes.size() * 2);
        for (qsizetype index = 0; index < codes.size(); ++index) {
            map[index * 2] = codes[index];
            map[index * 2 + 1] = static_cast<char>(characterForegroundColorIndex_ & 0x0f);
        }
    } else if (profile == TargetProfileId::AppleIIe) {
        patternName = QStringLiteral("APPLE_TEXT_FONT.CHR");
        mapName = QStringLiteral("APPLE_TEXT.PAGE");
        map.resize(1024);
        map.fill(static_cast<char>(0xa0));
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                map[appleTextOffset(row, column)] = static_cast<char>(
                    0x80U | (static_cast<unsigned char>(codes[row * columns + column]) & 0x7fU));
    } else if (profile == TargetProfileId::TimexTs1000) {
        patternName = QStringLiteral("ZX81_GLYPHS.CHR");
        mapName = QStringLiteral("ZX81_DISPLAY.DFILE");
        map.resize(793);
        map[0] = static_cast<char>(0x76);
        for (int row = 0; row < 24; ++row) {
            for (int column = 0; column < 32; ++column)
                map[1 + row * 33 + column] = static_cast<char>(
                    static_cast<unsigned char>(codes[row * 32 + column]) & 0xbfU);
            map[1 + row * 33 + 32] = static_cast<char>(0x76);
        }
    } else {
        patternName = QStringLiteral("TRS80_GLYPHS.CHR");
        mapName = QStringLiteral("TRS80_SCREEN.VRAM");
        map.resize(1024);
        map.fill(' ');
        for (qsizetype index = 0; index < std::min(map.size(), codes.size()); ++index)
            map[index] = static_cast<char>(static_cast<unsigned char>(codes[index]) & 0x7fU);
    }

    if (!writeFile(directory, patternName, patterns)
        || !writeFile(directory, mapName, map)) {
        setStatus({}, QStringLiteral("One or more native character-display files could not be written."));
        return false;
    }
    setStatus(QStringLiteral("Exported %1 patterns and the active %2-cell display map.")
                  .arg(patternCount).arg(columns * rows));
    return true;
}
