#include "EditorProjectController.hpp"

#include "ImageInputController.hpp"

#include "retrovdp/imageio/ImageLoader.hpp"

#include <QBuffer>
#include <QColor>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace {

constexpr std::uint8_t defaultCharacterColor = 0xf1;

QString workspaceName(int mode)
{
    switch (mode) {
    case 1: return QStringLiteral("character");
    case 2: return QStringLiteral("sprite");
    default: return QStringLiteral("screen-image");
    }
}

int workspaceValue(const QString& name)
{
    if (name == QStringLiteral("character")) return 1;
    if (name == QStringLiteral("sprite")) return 2;
    return 0;
}

QString previewName(int target)
{
    switch (target) {
    case 1: return QStringLiteral("f18a");
    case 2: return QStringLiteral("v9938");
    case 3: return QStringLiteral("v9958");
    case 4: return QStringLiteral("sega-sms-vdp");
    case 5: return QStringLiteral("sega-genesis-vdp");
    case 6: return QStringLiteral("huc6270");
    case 7: return QStringLiteral("vic-ii");
    case 8: return QStringLiteral("vic");
    case 9: return QStringLiteral("game-boy-ppu");
    case 10: return QStringLiteral("game-boy-color-ppu");
    case 11: return QStringLiteral("super-nes-ppu");
    default: return QStringLiteral("tms9918a");
    }
}

int snapCharacterTileCoordinate(int value, int maximum)
{
    return std::clamp(((value + 4) / 8) * 8, 0, maximum);
}

bool sameCharacterPattern(const auto& left, const auto& right)
{
    return left.bitmap == right.bitmap && left.colors == right.colors
        && left.indexedPixels == right.indexedPixels
        && left.indexedOverride == right.indexedOverride;
}

std::uint8_t reverseCharacterBits(std::uint8_t value)
{
    value = static_cast<std::uint8_t>(((value & 0x55U) << 1U)
                                      | ((value & 0xaaU) >> 1U));
    value = static_cast<std::uint8_t>(((value & 0x33U) << 2U)
                                      | ((value & 0xccU) >> 2U));
    return static_cast<std::uint8_t>((value << 4U) | (value >> 4U));
}

} // namespace

EditorProjectController::EditorProjectController(ImageInputController* imageInput,
                                                 QObject* parent)
    : QObject(parent), imageInput_(imageInput)
{
    for (int index = 0; index < static_cast<int>(characterSets_.size()); ++index) {
        characterSets_[static_cast<std::size_t>(index)] = makeCharacterSet(index + 1);
    }
    characterEditorSlots_.push_back({true, 0, 0, 0, 0});
    spriteSets_.push_back(makeSpriteSet(1));
    spriteEditorSlots_.push_back({false, 0, 0, 8});
    connect(imageInput_, &ImageInputController::settingsChanged,
            this, &EditorProjectController::projectChanged);
    connect(imageInput_, &ImageInputController::conversionChanged,
            this, &EditorProjectController::projectChanged);
    connect(imageInput_, &ImageInputController::screenImageSelectionChanged,
            this, &EditorProjectController::projectChanged);
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged,
                this, &EditorProjectController::projectChanged);
    }
}

QString imageDataUrl(const QImage& image)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,")
        + QString::fromLatin1(encoded.toBase64());
}

int colorDistanceSquared(const QColor& left, const QColor& right)
{
    const int red = left.red() - right.red();
    const int green = left.green() - right.green();
    const int blue = left.blue() - right.blue();
    return red * red + green * green + blue * blue;
}

int EditorProjectController::activeTarget() const
{
    return imageInput_->targetProfile();
}

QVariantList EditorProjectController::supportedTargets() const
{
    QVariantList result;
    const auto append = [&result](retrovdp::core::TargetProfileId id) {
        const auto& profile = retrovdp::core::targetProfile(id);
        result.push_back(QVariantMap{
            {QStringLiteral("name"), QString::fromLatin1(profile.displayName)},
            {QStringLiteral("value"), static_cast<int>(id)},
        });
    };
    if (tms9918aEnabled_) append(retrovdp::core::TargetProfileId::Tms9918A);
    if (f18aEnabled_) append(retrovdp::core::TargetProfileId::F18A);
    if (v9938Enabled_) append(retrovdp::core::TargetProfileId::V9938);
    if (v9958Enabled_) append(retrovdp::core::TargetProfileId::V9958);
    if (segaSmsEnabled_) append(retrovdp::core::TargetProfileId::SegaMasterSystem);
    if (segaGenesisEnabled_) append(retrovdp::core::TargetProfileId::SegaGenesis);
    if (huc6270Enabled_) append(retrovdp::core::TargetProfileId::HuC6270);
    if (vicIiEnabled_) append(retrovdp::core::TargetProfileId::VicII);
    if (vicEnabled_) append(retrovdp::core::TargetProfileId::Vic);
    if (gameBoyEnabled_) append(retrovdp::core::TargetProfileId::GameBoy);
    if (gameBoyColorEnabled_) append(retrovdp::core::TargetProfileId::GameBoyColor);
    if (superNesEnabled_) append(retrovdp::core::TargetProfileId::SuperNes);
    return result;
}

QVariantMap EditorProjectController::activeTargetInfo() const
{
    const auto& profile = retrovdp::core::targetProfile(
        static_cast<retrovdp::core::TargetProfileId>(activeTarget()));
    const auto has = [&profile](retrovdp::core::TargetCapability capability) {
        return retrovdp::core::hasCapability(profile.capabilities, capability);
    };
    const bool fixedPalette = has(retrovdp::core::TargetCapability::FixedPalette);
    const bool programmablePalette =
        has(retrovdp::core::TargetCapability::ProgrammablePalette);
    QString paletteDescription;
    if (fixedPalette && programmablePalette)
        paletteDescription = QStringLiteral("Fixed and programmable");
    else if (programmablePalette)
        paletteDescription = QStringLiteral("Programmable");
    else
        paletteDescription = QStringLiteral("Fixed");

    QVariantList spriteSizes;
    if (has(retrovdp::core::TargetCapability::Sprites)) {
        if (usesGenesisMode5Editor()) {
            for (int height = 8; height <= 32; height += 8) {
                for (int width = 8; width <= 32; width += 8)
                    spriteSizes.push_back(width * 100 + height);
            }
        } else if (usesHuC6270Editor()) {
            for (const int height : {16, 32, 64}) {
                for (const int width : {16, 32})
                    spriteSizes.push_back(width * 100 + height);
            }
        } else if (usesVicIIEditor()) {
            spriteSizes.push_back(2421);
        } else if (usesSuperNesEditor()) {
            for (const int size : {8, 16, 32, 64}) spriteSizes.push_back(size);
        } else {
            spriteSizes.push_back(static_cast<int>(profile.sprites.minimumPixelSize));
            if (profile.sprites.maximumPixelSize != profile.sprites.minimumPixelSize)
                spriteSizes.push_back(static_cast<int>(profile.sprites.maximumPixelSize));
        }
    }

    return {
        {QStringLiteral("name"), QString::fromLatin1(profile.displayName)},
        {QStringLiteral("id"), QString::fromStdString(profile.stableId.value())},
        {QStringLiteral("implemented"),
         profile.status == retrovdp::core::TargetProfileStatus::Implemented},
        {QStringLiteral("vramKiB"), static_cast<int>(profile.nominalVramBytes / 1024U)},
        {QStringLiteral("paletteDescription"), paletteDescription},
        {QStringLiteral("programmablePalette"), programmablePalette},
        {QStringLiteral("enhancedColor"),
         has(retrovdp::core::TargetCapability::EnhancedColor)},
        {QStringLiteral("conversionModeCount"),
         static_cast<int>(profile.conversionModes.size())},
        {QStringLiteral("characterPatterns"),
         has(retrovdp::core::TargetCapability::CharacterPatterns)},
        {QStringLiteral("characterPixelWidth"),
         static_cast<int>(profile.characterPatterns.pixelWidth)},
        {QStringLiteral("characterPixelHeight"),
         static_cast<int>(profile.characterPatterns.pixelHeight)},
        {QStringLiteral("characterPatternsPerSet"),
         static_cast<int>(profile.characterPatterns.patternsPerSet)},
        {QStringLiteral("characterSetCount"),
         static_cast<int>(profile.characterPatterns.setCount)},
        {QStringLiteral("characterMapColumns"),
         characterMapColumns()},
        {QStringLiteral("characterMapRows"),
         characterMapRows()},
        {QStringLiteral("characterColorDepth"),
         activeCharacterColorDepth()},
        {QStringLiteral("characterInterpretation"),
         usesSmsMode4Editor() ? QStringLiteral("Mode 4 planar tiles")
         : usesGenesisMode5Editor() ? QStringLiteral("Mode V packed 4bpp tiles")
         : usesHuC6270Editor() ? QStringLiteral("HuC6270 4-plane background tiles")
         : usesVicIIEditor() ? QStringLiteral("VIC-II high-resolution and multicolor characters")
         : usesGameBoyEditor() ? QStringLiteral("Game Boy 2-plane tiles")
         : usesGameBoyColorEditor() ? QStringLiteral("Game Boy Color banked 2-plane tiles")
         : usesSuperNesEditor() ? QStringLiteral("Super NES mode-selected planar tiles")
         : activeTarget() == static_cast<int>(retrovdp::core::TargetProfileId::Vic)
             ? QStringLiteral("VIC high-resolution and multicolor characters")
             : QStringLiteral("Target-compatible patterns")},
        {QStringLiteral("characterPaletteBankCount"),
         usesGenesisMode5Editor() ? 4 : (usesHuC6270Editor() ? 16
         : (usesGameBoyColorEditor() ? 8
         : (usesSuperNesEditor() && activeCharacterColorDepth() < 8 ? 8 : 1)))},
        {QStringLiteral("sprites"),
         has(retrovdp::core::TargetCapability::Sprites)},
        {QStringLiteral("spriteSizes"), spriteSizes},
        {QStringLiteral("spriteMinimumSize"),
         static_cast<int>(profile.sprites.minimumPixelSize)},
        {QStringLiteral("spriteMaximumSize"),
         static_cast<int>(profile.sprites.maximumPixelSize)},
        {QStringLiteral("spritePatternsPerSet"),
         spritePatternsPerSet()},
        {QStringLiteral("spriteMaximumVisible"),
         spritePatternsPerSet()},
        {QStringLiteral("spriteMaximumColorDepth"),
         static_cast<int>(profile.sprites.maximumColorDepth)},
        {QStringLiteral("spriteUsesGlobalSize"), profile.sprites.usesGlobalSize},
        {QStringLiteral("spritePerItemSize"), usesPerSpriteSizeEditor()},
        {QStringLiteral("spriteInterpretation"),
         usesSmsMode4Editor()
             ? QStringLiteral("Mode 4 sprites; 8x8 or 8x16, 4bpp sprite palette")
             : usesGenesisMode5Editor()
                 ? QStringLiteral("Mode V sprites; independent 8/16/24/32 width and height, 4bpp palette")
             : usesHuC6270Editor()
                 ? QStringLiteral("HuC6270 sprites; 16/32 width by 16/32/64 height, 4bpp palette")
             : usesVicIIEditor()
                 ? QStringLiteral("VIC-II 24x21 sprites; high-resolution or multicolor")
             : usesGameBoyEditor()
                 ? QStringLiteral("Game Boy OAM; global 8x8 or 8x16, 2bpp OBJ palettes")
             : usesGameBoyColorEditor()
                 ? QStringLiteral("Game Boy Color OAM; global 8x8 or 8x16, color OBJ palettes")
             : usesSuperNesEditor()
                 ? QStringLiteral("Super NES OAM; selected small/large size pair, 4bpp OBJ palettes")
             : QStringLiteral("Target-compatible sprites")},
        {QStringLiteral("spriteMaximumPerScanline"),
         usesSmsMode4Editor() ? 8 : (usesGenesisMode5Editor()
             ? (imageInput_->targetWidth() == 320 ? 20 : 16)
             : (usesHuC6270Editor() ? 16 : (usesVicIIEditor() ? 8
             : (usesGameBoyEditor() || usesGameBoyColorEditor() ? 10
             : (usesSuperNesEditor() ? 32 : 0)))))},
        {QStringLiteral("spriteSupportsFlipAttributes"),
         usesGenesisMode5Editor() || usesHuC6270Editor()
             || usesGameBoyEditor() || usesGameBoyColorEditor() || usesSuperNesEditor()},
        {QStringLiteral("spritePaletteBankCount"),
         usesGenesisMode5Editor() ? 4 : (usesHuC6270Editor() ? 16
         : (usesGameBoyEditor() ? 2
         : (usesGameBoyColorEditor() || usesSuperNesEditor() ? 8 : 1)))},
    };
}

bool EditorProjectController::activeTargetHasCapability(std::uint32_t capability) const
{
    const auto requested = static_cast<retrovdp::core::TargetCapability>(capability);
    return retrovdp::core::hasCapability(
        retrovdp::core::targetProfile(
            static_cast<retrovdp::core::TargetProfileId>(activeTarget())).capabilities,
        requested);
}

bool EditorProjectController::screenImageModeAvailable() const
{
    return activeTargetHasCapability(static_cast<std::uint32_t>(
        retrovdp::core::TargetCapability::BitmapConversion));
}

bool EditorProjectController::characterModeAvailable() const
{
    return activeTargetHasCapability(static_cast<std::uint32_t>(
        retrovdp::core::TargetCapability::CharacterPatterns));
}

bool EditorProjectController::spriteModeAvailable() const
{
    return activeTargetHasCapability(static_cast<std::uint32_t>(
        retrovdp::core::TargetCapability::Sprites));
}

bool EditorProjectController::targetEnabled(int value) const
{
    return (value == 0 && tms9918aEnabled_) || (value == 1 && f18aEnabled_)
        || (value == 2 && v9938Enabled_) || (value == 3 && v9958Enabled_)
        || (value == 4 && segaSmsEnabled_) || (value == 5 && segaGenesisEnabled_)
        || (value == 6 && huc6270Enabled_) || (value == 7 && vicIiEnabled_)
        || (value == 8 && vicEnabled_) || (value == 9 && gameBoyEnabled_)
        || (value == 10 && gameBoyColorEnabled_) || (value == 11 && superNesEnabled_);
}

void EditorProjectController::setProjectName(const QString& value)
{
    const QString normalized = value.trimmed().isEmpty()
        ? QStringLiteral("Untitled Project") : value.trimmed();
    if (projectName_ == normalized) return;
    projectName_ = normalized;
    emit projectChanged();
}

void EditorProjectController::setTms9918aEnabled(bool value)
{
    if (!value && !f18aEnabled_) return;
    if (tms9918aEnabled_ == value) return;
    tms9918aEnabled_ = value;
    if (!targetEnabled(activeTarget())) setActiveTarget(1);
    emit projectChanged();
}

QVariantList EditorProjectController::characterSetNames() const
{
    QVariantList result;
    const int count = std::min(characterSetCount(),
                               static_cast<int>(characterSets_.size()));
    for (int index = 0; index < count; ++index)
        result.push_back(characterSets_[static_cast<std::size_t>(index)].name);
    return result;
}

bool EditorProjectController::usesSmsMode4Editor() const
{
    return activeTarget()
        == static_cast<int>(retrovdp::core::TargetProfileId::SegaMasterSystem);
}

bool EditorProjectController::usesGenesisMode5Editor() const
{
    return activeTarget()
        == static_cast<int>(retrovdp::core::TargetProfileId::SegaGenesis);
}

bool EditorProjectController::usesHuC6270Editor() const
{
    return activeTarget()
        == static_cast<int>(retrovdp::core::TargetProfileId::HuC6270);
}

bool EditorProjectController::usesVicIIEditor() const
{
    return activeTarget()
        == static_cast<int>(retrovdp::core::TargetProfileId::VicII);
}

bool EditorProjectController::usesGameBoyEditor() const
{
    return activeTarget() == static_cast<int>(retrovdp::core::TargetProfileId::GameBoy);
}

bool EditorProjectController::usesGameBoyColorEditor() const
{
    return activeTarget() == static_cast<int>(retrovdp::core::TargetProfileId::GameBoyColor);
}

bool EditorProjectController::usesSuperNesEditor() const
{
    return activeTarget() == static_cast<int>(retrovdp::core::TargetProfileId::SuperNes);
}

bool EditorProjectController::usesCompoundSpriteEditor() const
{
    return usesGenesisMode5Editor() || usesHuC6270Editor() || usesVicIIEditor()
        || usesSuperNesEditor();
}

bool EditorProjectController::usesIndexed4BppEditor() const
{
    return usesSmsMode4Editor() || usesGenesisMode5Editor() || usesHuC6270Editor()
        || usesGameBoyEditor() || usesGameBoyColorEditor() || usesSuperNesEditor();
}

bool EditorProjectController::usesIndexedSpriteEditor() const
{
    return usesIndexed4BppEditor();
}

int EditorProjectController::activeCharacterColorDepth() const
{
    if (usesGameBoyEditor() || usesGameBoyColorEditor()) return 2;
    if (usesSuperNesEditor()) {
        if (imageInput_->conversionMode()
            == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode0Background))
            return 2;
        if (imageInput_->conversionMode()
            == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode3Background))
            return 8;
        return 4;
    }
    return usesIndexed4BppEditor() ? 4 : 1;
}

bool EditorProjectController::usesPerSpriteSizeEditor() const
{
    return editScope_ == 1
        && retrovdp::core::targetProfile(
            static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
               .sprites.supportsPerSpriteSize;
}

QVariantList EditorProjectController::characterPaletteColors() const
{
    if (usesGameBoyEditor()) {
        return {QColor(224, 248, 208), QColor(136, 192, 112),
                QColor(52, 104, 86), QColor(8, 24, 32)};
    }
    if (usesGameBoyColorEditor() || usesSuperNesEditor()) {
        const QVariantList palette = imageInput_->paletteColors();
        const int colorsPerBank = usesSuperNesEditor()
            && imageInput_->conversionMode()
                == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode3Background)
            ? 256 : (usesSuperNesEditor()
                && imageInput_->conversionMode()
                    == static_cast<int>(retrovdp::core::ConversionMode::SuperNesMode1Background)
                ? 16 : 4);
        const int start = characterPaletteBank_ * colorsPerBank;
        if (palette.size() >= start + colorsPerBank)
            return palette.mid(start, colorsPerBank);
        QVariantList fallback;
        fallback.reserve(colorsPerBank);
        for (int index = 0; index < colorsPerBank; ++index) {
            const int level = colorsPerBank == 1 ? 0 : index * 255 / (colorsPerBank - 1);
            fallback.push_back(QColor(level, level, level));
        }
        return fallback;
    }
    if (usesGenesisMode5Editor() || usesHuC6270Editor()) {
        const QVariantList palette = imageInput_->paletteColors();
        const int start = characterPaletteBank_ * 16;
        if (palette.size() >= start + 16) return palette.mid(start, 16);
        QVariantList fallback;
        fallback.reserve(16);
        fallback.push_back(QColor(0, 0, 0, 0));
        for (int index = 1; index < 16; ++index) {
            const int level = index * 255 / 15;
            fallback.push_back(QColor(level, level, level));
        }
        return fallback;
    }
    if (usesSmsMode4Editor()) {
        const QVariantList palette = imageInput_->paletteColors();
        if (palette.size() >= 16) return palette.mid(0, 16);
        return {
            QColor(0, 0, 0), QColor(170, 0, 0), QColor(0, 170, 0),
            QColor(170, 170, 0), QColor(0, 0, 170), QColor(170, 0, 170),
            QColor(0, 170, 170), QColor(170, 170, 170), QColor(85, 85, 85),
            QColor(255, 85, 85), QColor(85, 255, 85), QColor(255, 255, 85),
            QColor(85, 85, 255), QColor(255, 85, 255), QColor(85, 255, 255),
            QColor(255, 255, 255),
        };
    }
    if (usesVicIIEditor()) {
        return {
            QColor(0, 0, 0), QColor(255, 255, 255), QColor(136, 0, 0),
            QColor(170, 255, 238), QColor(204, 68, 204), QColor(0, 204, 85),
            QColor(0, 0, 170), QColor(238, 238, 119), QColor(221, 136, 85),
            QColor(102, 68, 0), QColor(255, 119, 119), QColor(51, 51, 51),
            QColor(119, 119, 119), QColor(170, 255, 102), QColor(0, 136, 255),
            QColor(187, 187, 187),
        };
    }
    if (activeTarget() == static_cast<int>(retrovdp::core::TargetProfileId::Vic)) {
        return {
            QColor(0, 0, 0), QColor(255, 255, 255), QColor(182, 31, 33),
            QColor(77, 240, 255), QColor(180, 65, 223), QColor(58, 209, 79),
            QColor(43, 43, 216), QColor(255, 255, 79), QColor(216, 143, 34),
            QColor(255, 194, 91), QColor(255, 116, 118), QColor(148, 255, 255),
            QColor(255, 137, 255), QColor(138, 255, 159), QColor(128, 128, 255),
            QColor(255, 255, 191),
        };
    }
    // Hardware color order, including transparent color zero. The baseline
    // palette is fixed; an F18A edit scope uses the current programmable
    // working palette mapped back to hardware color indexes.
    if (editScope_ == 1 && f18aEnabled_) {
        const QVariantList working = imageInput_->workingPaletteColors();
        if (working.size() == 15) {
            static constexpr std::array<int, 16> workingIndex{
                -1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 2, 0};
            QVariantList result;
            result.reserve(16);
            for (const int index : workingIndex) {
                result.push_back(index < 0
                                     ? QVariant::fromValue(QColor(0, 0, 0, 0))
                                     : working.at(index));
            }
            return result;
        }
    }

    return {
        QColor(0, 0, 0, 0), QColor(0, 0, 0), QColor(32, 200, 64),
        QColor(88, 216, 120), QColor(80, 80, 232), QColor(120, 112, 248),
        QColor(208, 80, 72), QColor(64, 232, 240), QColor(248, 80, 80),
        QColor(248, 120, 120), QColor(208, 192, 80), QColor(224, 200, 128),
        QColor(32, 176, 56), QColor(200, 88, 184), QColor(200, 200, 200),
        QColor(248, 248, 248),
    };
}

QVariantList EditorProjectController::spritePaletteColors() const
{
    if (usesGameBoyEditor()) return characterPaletteColors();
    if (usesGameBoyColorEditor()) {
        const QVariantList palette = imageInput_->paletteColors();
        const int start = 32 + activeSpritePaletteBank() * 4;
        if (palette.size() >= start + 4) return palette.mid(start, 4);
        return {QColor(0, 0, 0, 0), QColor(85, 85, 85),
                QColor(170, 170, 170), QColor(255, 255, 255)};
    }
    if (usesSuperNesEditor()) {
        const QVariantList palette = imageInput_->paletteColors();
        const int start = 128 + activeSpritePaletteBank() * 16;
        if (palette.size() >= start + 16) return palette.mid(start, 16);
        QVariantList fallback;
        fallback.reserve(16);
        fallback.push_back(QColor(0, 0, 0, 0));
        for (int index = 1; index < 16; ++index) {
            const int level = index * 255 / 15;
            fallback.push_back(QColor(level, level, level));
        }
        return fallback;
    }
    if (usesGenesisMode5Editor() || usesHuC6270Editor()) {
        const QVariantList palette = imageInput_->paletteColors();
        const int start = activeSpritePaletteBank() * 16;
        if (palette.size() >= start + 16) return palette.mid(start, 16);
        return characterPaletteColors();
    }
    if (usesSmsMode4Editor()) {
        const QVariantList palette = imageInput_->paletteColors();
        if (palette.size() >= 32) return palette.mid(16, 16);
        return characterPaletteColors();
    }
    return characterPaletteColors();
}

void EditorProjectController::ensureIndexedCharacterOverride(
    CharacterPattern& pattern) const
{
    if (pattern.indexedOverride) return;
    for (int row = 0; row < 8; ++row) {
        const int foreground = (pattern.colors[static_cast<std::size_t>(row)] >> 4U)
            & 0x0f;
        const int background = pattern.colors[static_cast<std::size_t>(row)] & 0x0f;
        for (int column = 0; column < 8; ++column) {
            const bool set = (pattern.bitmap[static_cast<std::size_t>(row)]
                              & (0x80U >> column)) != 0;
            pattern.indexedPixels[static_cast<std::size_t>(row * 8 + column)] =
                static_cast<std::uint8_t>(set ? foreground : background);
        }
    }
    pattern.indexedOverride = true;
}

void EditorProjectController::setCharacterPaletteBank(int value)
{
    value = std::clamp(value, 0, usesGenesisMode5Editor() ? 3
                              : (usesHuC6270Editor() ? 15
                              : (usesGameBoyColorEditor()
                              || (usesSuperNesEditor() && activeCharacterColorDepth() < 8)
                                  ? 7 : 0)));
    if (characterPaletteBank_ == value) return;
    characterPaletteBank_ = value;
    ++characterRevision_;
    emit projectChanged();
}

int EditorProjectController::activeCharacterTilePalette() const
{
    if (characterEditorSlots_.empty()) return 0;
    return characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)].palette;
}

bool EditorProjectController::activeCharacterTileFlipX() const
{
    return !characterEditorSlots_.empty()
        && characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)].flipX;
}

bool EditorProjectController::activeCharacterTileFlipY() const
{
    return !characterEditorSlots_.empty()
        && characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)].flipY;
}

bool EditorProjectController::activeCharacterTilePriority() const
{
    return !characterEditorSlots_.empty()
        && characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)].priority;
}

void EditorProjectController::setActiveCharacterPlane(int value)
{
    if (!usesGenesisMode5Editor()) value = 0;
    value = std::clamp(value, 0, 2);
    if (activeCharacterPlane_ == value) return;
    if (characterPanActive_) finishCharacterPan();
    activeCharacterPlane_ = value;
    const auto match = std::find_if(
        characterEditorSlots_.begin(), characterEditorSlots_.end(),
        [value](const CharacterEditorSlot& slot) { return slot.plane == value; });
    if (match != characterEditorSlots_.end()) {
        activeCharacterEditor_ = static_cast<int>(
            std::distance(characterEditorSlots_.begin(), match));
        if (match->loaded) {
            activeCharacterSet_ = match->setIndex;
            activeCharacterPattern_ = match->patternIndex;
        }
    } else {
        characterEditorSlots_.push_back(
            {false, activeCharacterSet_, activeCharacterPattern_, 0, 0, value});
        activeCharacterEditor_ = static_cast<int>(characterEditorSlots_.size()) - 1;
    }
    emit projectChanged();
}

void EditorProjectController::setGenesisCompositePreview(bool value)
{
    if (genesisCompositePreview_ == value) return;
    genesisCompositePreview_ = value;
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterTilePalette(int value)
{
    if (!usesGenesisMode5Editor() || characterEditorSlots_.empty()) return;
    value = std::clamp(value, 0, 3);
    auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (slot.palette == value && characterPaletteBank_ == value) return;
    slot.palette = value;
    characterPaletteBank_ = value;
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterTileFlipX(bool value)
{
    if (!usesGenesisMode5Editor() || characterEditorSlots_.empty()) return;
    auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (slot.flipX == value) return;
    slot.flipX = value;
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterTileFlipY(bool value)
{
    if (!usesGenesisMode5Editor() || characterEditorSlots_.empty()) return;
    auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (slot.flipY == value) return;
    slot.flipY = value;
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterTilePriority(bool value)
{
    if (!usesGenesisMode5Editor() || characterEditorSlots_.empty()) return;
    auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (slot.priority == value) return;
    slot.priority = value;
    emit projectChanged();
}

QVariantList EditorProjectController::characterEditorSlots() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(characterEditorSlots_.size()));
    for (int index = 0; index < static_cast<int>(characterEditorSlots_.size()); ++index) {
        const auto& slot = characterEditorSlots_[static_cast<std::size_t>(index)];
        result.push_back(QVariantMap{{QStringLiteral("index"), index},
                                     {QStringLiteral("loaded"), slot.loaded},
                                     {QStringLiteral("setIndex"), slot.setIndex},
                                     {QStringLiteral("patternIndex"), slot.patternIndex},
                                     {QStringLiteral("tileX"), slot.tileX},
                                     {QStringLiteral("tileY"), slot.tileY},
                                     {QStringLiteral("plane"), slot.plane},
                                     {QStringLiteral("palette"), slot.palette},
                                     {QStringLiteral("flipX"), slot.flipX},
                                     {QStringLiteral("flipY"), slot.flipY},
                                     {QStringLiteral("priority"), slot.priority}});
    }
    return result;
}

bool EditorProjectController::canPasteCharacterPattern() const
{
    return characterPatternFromClipboard().has_value();
}

int EditorProjectController::characterPatternWidth() const
{
    return retrovdp::core::targetProfile(
               static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
        .characterPatterns.pixelWidth;
}

int EditorProjectController::characterPatternHeight() const
{
    return retrovdp::core::targetProfile(
               static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
        .characterPatterns.pixelHeight;
}

int EditorProjectController::characterPatternsPerSet() const
{
    return retrovdp::core::targetProfile(
               static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
        .characterPatterns.patternsPerSet;
}

int EditorProjectController::characterSetCount() const
{
    return retrovdp::core::targetProfile(
               static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
        .characterPatterns.setCount;
}

int EditorProjectController::characterMapColumns() const
{
    if (usesGenesisMode5Editor()) {
        const auto mode = static_cast<retrovdp::core::ConversionMode>(
            imageInput_->conversionMode());
        if (retrovdp::core::primaryTargetProfile(mode)
            == retrovdp::core::TargetProfileId::SegaGenesis) {
            return static_cast<int>(retrovdp::core::displayMode(mode).geometry.width / 8U);
        }
    }
    return retrovdp::core::targetProfile(
               static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
        .characterPatterns.mapColumns;
}

int EditorProjectController::characterMapRows() const
{
    if (usesSmsMode4Editor() || usesGenesisMode5Editor()) {
        const auto mode = static_cast<retrovdp::core::ConversionMode>(
            imageInput_->conversionMode());
        if (retrovdp::core::primaryTargetProfile(mode)
            == static_cast<retrovdp::core::TargetProfileId>(activeTarget())) {
            return static_cast<int>(retrovdp::core::displayMode(mode).geometry.height / 8U);
        }
    }
    return retrovdp::core::targetProfile(
               static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
        .characterPatterns.mapRows;
}

bool EditorProjectController::screenImageSelectionCharacterAligned() const
{
    if (!imageInput_->hasScreenImageSelection()) return false;
    const int width = characterPatternWidth();
    const int height = characterPatternHeight();
    return width > 0 && height > 0
        && imageInput_->screenImageSelectionX() % width == 0
        && imageInput_->screenImageSelectionY() % height == 0
        && imageInput_->screenImageSelectionWidth() % width == 0
        && imageInput_->screenImageSelectionHeight() % height == 0;
}

QVariantList EditorProjectController::spriteSetNames() const
{
    QVariantList result;
    for (const auto& set : spriteSets_) result.push_back(set.name);
    return result;
}

QVariantList EditorProjectController::activeSpritePlacements() const
{
    QVariantList result;
    if (spriteSets_.empty()) return result;
    const auto& placements = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)].placements;
    for (int index = 0; index < spritePatternsPerSet(); ++index) {
        const auto& placement = placements[static_cast<std::size_t>(index)];
        const int effectiveSize = usesPerSpriteSizeEditor()
            ? placement.size : spriteGlobalSize_;
        const int placementX = effectiveSize == 16 ? placement.x16 : placement.x;
        const int placementY = effectiveSize == 16 ? placement.y16 : placement.y;
        result.push_back(QVariantMap{{QStringLiteral("index"), index},
                                     {QStringLiteral("x"), placementX},
                                     {QStringLiteral("y"), placementY},
                                     {QStringLiteral("visible"), placement.visible},
                                     {QStringLiteral("size"), effectiveSize},
                                     {QStringLiteral("storedSize"), placement.size},
                                     {QStringLiteral("color"), placement.color},
                                     {QStringLiteral("colorDepth"),
                                      activeSpriteColorDepth()},
                                     {QStringLiteral("palette"), placement.palette},
                                     {QStringLiteral("flipX"),
                                      (editScope_ == 1 || usesIndexedSpriteEditor())
                                          && !usesSmsMode4Editor()
                                          && placement.flipX},
                                     {QStringLiteral("flipY"),
                                      (editScope_ == 1 || usesIndexedSpriteEditor())
                                          && !usesSmsMode4Editor()
                                          && placement.flipY},
                                     {QStringLiteral("priority"),
                                      (usesGenesisMode5Editor() || usesGameBoyEditor()
                                       || usesGameBoyColorEditor() || usesSuperNesEditor())
                                          && placement.priority},
                                     {QStringLiteral("profile"),
                                      usesSmsMode4Editor()
                                          ? QStringLiteral("sms-mode4")
                                          : (usesGenesisMode5Editor()
                                              ? QStringLiteral("genesis-mode5")
                                          : (editScope_ == 1 ? QStringLiteral("f18a")
                                                             : QStringLiteral("tms9918a")))}});
    }
    return result;
}

QVariantList EditorProjectController::spriteEditorSlots() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(spriteEditorSlots_.size()));
    for (int index = 0; index < static_cast<int>(spriteEditorSlots_.size()); ++index) {
        const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(index)];
        QVariantMap values{{QStringLiteral("index"), index},
                           {QStringLiteral("loaded"), slot.loaded},
                           {QStringLiteral("setIndex"), slot.setIndex},
                           {QStringLiteral("spriteIndex"), slot.spriteIndex}};
        if (slot.loaded && slot.setIndex >= 0
            && slot.setIndex < static_cast<int>(spriteSets_.size())
            && slot.spriteIndex >= 0
            && slot.spriteIndex < spritePatternsPerSet()) {
            const auto& placement = spriteSets_[static_cast<std::size_t>(slot.setIndex)]
                                        .placements[static_cast<std::size_t>(slot.spriteIndex)];
            values.insert(QStringLiteral("size"), slot.size);
            values.insert(QStringLiteral("x"),
                          slot.size == 16 ? placement.x16 : placement.x);
            values.insert(QStringLiteral("y"),
                          slot.size == 16 ? placement.y16 : placement.y);
            values.insert(QStringLiteral("visible"), placement.visible);
            values.insert(QStringLiteral("activeForPlacement"),
                          slot.size == (usesPerSpriteSizeEditor()
                                            ? placement.size : spriteGlobalSize_));
            values.insert(QStringLiteral("color"), placement.color);
            values.insert(QStringLiteral("colorDepth"),
                          activeSpriteColorDepth());
            values.insert(QStringLiteral("palette"), placement.palette);
            values.insert(QStringLiteral("flipX"), placement.flipX);
            values.insert(QStringLiteral("flipY"), placement.flipY);
            values.insert(QStringLiteral("priority"), placement.priority);
        } else {
            values.insert(QStringLiteral("size"), slot.size);
            values.insert(QStringLiteral("x"), 0);
            values.insert(QStringLiteral("y"), 0);
            values.insert(QStringLiteral("visible"), false);
            values.insert(QStringLiteral("activeForPlacement"), false);
            values.insert(QStringLiteral("color"), 15);
            values.insert(QStringLiteral("colorDepth"), 1);
            values.insert(QStringLiteral("palette"), 0);
            values.insert(QStringLiteral("flipX"), false);
            values.insert(QStringLiteral("flipY"), false);
            values.insert(QStringLiteral("priority"), false);
        }
        result.push_back(values);
    }
    return result;
}

void EditorProjectController::setWorkspaceMode(int value)
{
    value = std::clamp(value, 0, 2);
    if (workspaceMode_ == value) return;
    if (workspaceMode_ == 1 && characterPanActive_) finishCharacterPan();
    if (workspaceMode_ == 2 && spritePanActive_) finishSpritePan();
    workspaceMode_ = value;
    emit projectChanged();
}

void EditorProjectController::setF18aEnabled(bool value)
{
    if (!value && !tms9918aEnabled_) return;
    if (f18aEnabled_ == value) return;
    f18aEnabled_ = value;
    if (!value) {
        previewTarget_ = 0;
        editScope_ = 0;
        activeSpriteSize_ = spriteGlobalSize_;
        activateSpriteEditorBank(spriteGlobalSize_);
        syncSpriteDrawingColor();
    }
    if (!targetEnabled(activeTarget())) setActiveTarget(0);
    emit projectChanged();
}

void EditorProjectController::setActiveTarget(int value)
{
    if (!targetEnabled(value)) return;
    const bool targetChanged = activeTarget() != value;
    if (targetChanged) imageInput_->setTargetProfile(value);
    const auto& profile = retrovdp::core::targetProfile(
        static_cast<retrovdp::core::TargetProfileId>(value));
    activeCharacterSet_ = std::clamp(
        activeCharacterSet_, 0, static_cast<int>(profile.characterPatterns.setCount) - 1);
    activeCharacterPattern_ = std::clamp(
        activeCharacterPattern_, 0,
        static_cast<int>(profile.characterPatterns.patternsPerSet) - 1);
    if (!characterEditorSlots_.empty()) {
        auto& editor = characterEditorSlots_[static_cast<std::size_t>(
            activeCharacterEditor_)];
        editor.setIndex = activeCharacterSet_;
        editor.patternIndex = activeCharacterPattern_;
    }
    activeSprite_ = std::clamp(
        activeSprite_, 0, static_cast<int>(profile.sprites.patternsPerSet) - 1);
    if (!spriteEditorSlots_.empty()) {
        auto& editor = spriteEditorSlots_[static_cast<std::size_t>(activeSpriteEditor_)];
        editor.spriteIndex = activeSprite_;
    }
    const int targetScope = retrovdp::core::hasCapability(
        profile.capabilities, retrovdp::core::TargetCapability::EnhancedColor) ? 1 : 0;
    setEditScope(targetScope);
    if (usesCompoundSpriteEditor() && !spriteSets_.empty()) {
        auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                              .placements[static_cast<std::size_t>(activeSprite_)];
        placement.size = normalizedSpriteSize(placement.size);
        activeSpriteSize_ = placement.size;
        if (!spriteEditorSlots_.empty())
            spriteEditorSlots_[static_cast<std::size_t>(activeSpriteEditor_)].size
                = activeSpriteSize_;
    }
    const bool previewChanged = previewTarget_ != value;
    previewTarget_ = value;
    if (workspaceMode_ == 0 && !screenImageModeAvailable()) setWorkspaceMode(1);
    if (workspaceMode_ == 1 && !characterModeAvailable()) setWorkspaceMode(2);
    if (workspaceMode_ == 2 && !spriteModeAvailable()) setWorkspaceMode(0);
    if (targetChanged || previewChanged) emit projectChanged();
}

void EditorProjectController::configureProject(const QString& name,
                                               bool tms9918aEnabled,
                                               bool f18aEnabled)
{
    configureProjectWithTargets(name, tms9918aEnabled, f18aEnabled,
                                plannedTargetIds_);
}

void EditorProjectController::configureProjectWithTargets(
    const QString& name,
    bool tms9918aEnabled,
    bool f18aEnabled,
    const QStringList& plannedTargetIds)
{
    projectName_ = name.trimmed().isEmpty()
        ? QStringLiteral("Untitled Project") : name.trimmed();
    tms9918aEnabled_ = tms9918aEnabled;
    f18aEnabled_ = f18aEnabled;
    v9938Enabled_ = plannedTargetIds.contains(QStringLiteral("v9938"));
    v9958Enabled_ = plannedTargetIds.contains(QStringLiteral("v9958"));
    segaSmsEnabled_ = plannedTargetIds.contains(QStringLiteral("sega-sms-vdp"));
    segaGenesisEnabled_ = plannedTargetIds.contains(QStringLiteral("sega-genesis-vdp"));
    huc6270Enabled_ = plannedTargetIds.contains(QStringLiteral("huc6270"));
    vicIiEnabled_ = plannedTargetIds.contains(QStringLiteral("vic-ii"));
    vicEnabled_ = plannedTargetIds.contains(QStringLiteral("vic"));
    gameBoyEnabled_ = plannedTargetIds.contains(QStringLiteral("game-boy-ppu"));
    gameBoyColorEnabled_ = plannedTargetIds.contains(QStringLiteral("game-boy-color-ppu"));
    superNesEnabled_ = plannedTargetIds.contains(QStringLiteral("super-nes-ppu"));
    if (!tms9918aEnabled_ && !f18aEnabled_ && !v9938Enabled_ && !v9958Enabled_
        && !segaSmsEnabled_ && !segaGenesisEnabled_ && !huc6270Enabled_
        && !vicIiEnabled_ && !vicEnabled_ && !gameBoyEnabled_
        && !gameBoyColorEnabled_ && !superNesEnabled_) {
        tms9918aEnabled_ = true;
    }
    plannedTargetIds_.clear();
    for (const QString& value : plannedTargetIds) {
        const QString id = value.trimmed();
        if (!id.isEmpty() && id != QStringLiteral("tms9918a")
            && id != QStringLiteral("f18a")
            && id != QStringLiteral("v9938")
            && id != QStringLiteral("v9958")
            && id != QStringLiteral("sega-sms-vdp")
            && id != QStringLiteral("sega-genesis-vdp")
            && id != QStringLiteral("huc6270")
            && id != QStringLiteral("vic-ii")
            && id != QStringLiteral("vic")
            && id != QStringLiteral("game-boy-ppu")
            && id != QStringLiteral("game-boy-color-ppu")
            && id != QStringLiteral("super-nes-ppu")
            && !plannedTargetIds_.contains(id)) {
            plannedTargetIds_.push_back(id);
        }
    }
    if (!f18aEnabled_) {
        previewTarget_ = 0;
        editScope_ = 0;
    }
    if (!targetEnabled(activeTarget())) {
        int fallback = 0;
        for (int candidate = 0; candidate <= 11; ++candidate) {
            if (targetEnabled(candidate)) {
                fallback = candidate;
                break;
            }
        }
        setActiveTarget(fallback);
    } else
        setActiveTarget(activeTarget());
    emit projectChanged();
}

void EditorProjectController::resetProjectData()
{
    workspaceMode_ = 0;
    previewTarget_ = 0;
    editScope_ = 0;
    for (int index = 0; index < static_cast<int>(characterSets_.size()); ++index)
        characterSets_[static_cast<std::size_t>(index)] = makeCharacterSet(index + 1);
    activeCharacterSet_ = 0;
    activeCharacterPattern_ = 0;
    characterForegroundColorIndex_ = 15;
    characterBackgroundColorIndex_ = 1;
    characterPaletteBank_ = 0;
    activeCharacterPlane_ = 0;
    genesisCompositePreview_ = false;
    characterEditorSlots_ = {{true, 0, 0, 0, 0}};
    activeCharacterEditor_ = 0;
    characterTilingMode_ = false;
    characterPanActive_ = false;
    characterEditActive_ = false;
    characterUndoHistory_.clear();
    characterRedoHistory_.clear();
    ++characterRevision_;

    spriteSets_.clear();
    spriteSets_.push_back(makeSpriteSet(1));
    spriteEditorSlots_ = {{false, 0, 0, 8}};
    activeSpriteSet_ = 0;
    activeSprite_ = 0;
    activeSpriteSize_ = 8;
    spriteGlobalSize_ = 8;
    spriteDrawingColorIndex_ = 15;
    activeSpriteEditor_ = 0;
    spritePlacementMode_ = false;
    spritePanActive_ = false;
    spriteEditActive_ = false;
    spriteUndoHistory_.clear();
    spriteRedoHistory_.clear();
    placementWidth_ = 256;
    placementHeight_ = 192;
    ++spriteRevision_;
    recipePath_.clear();
    imageInput_->newScreenImage();
}

void EditorProjectController::createProject(const QString& name,
                                            bool tms9918aEnabled,
                                            bool f18aEnabled)
{
    createProjectWithTargets(name, tms9918aEnabled, f18aEnabled, {});
}

void EditorProjectController::createProjectWithTargets(
    const QString& name,
    bool tms9918aEnabled,
    bool f18aEnabled,
    const QStringList& plannedTargetIds)
{
    resetProjectData();
    configureProjectWithTargets(name, tms9918aEnabled, f18aEnabled,
                                plannedTargetIds);
    setStatus(QStringLiteral("Created project %1").arg(projectName_));
}

void EditorProjectController::setPreviewTarget(int value)
{
    value = std::clamp(value, 0,
                       static_cast<int>(retrovdp::core::TargetProfileId::SuperNes));
    if (previewTarget_ == value) return;
    previewTarget_ = value;
    emit projectChanged();
}

void EditorProjectController::setEditScope(int value)
{
    const int maximumScope = activeTargetHasCapability(static_cast<std::uint32_t>(
        retrovdp::core::TargetCapability::EnhancedColor)) ? 1 : 0;
    value = std::clamp(value, 0, maximumScope);
    if (editScope_ == value) return;
    if (spritePanActive_) finishSpritePan();
    editScope_ = value;
    if (!spriteSets_.empty()) {
        const auto& activeEditor = spriteEditorSlots_[
            static_cast<std::size_t>(activeSpriteEditor_)];
        const auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                                    .placements[static_cast<std::size_t>(activeSprite_)];
        const bool perItemSize = usesPerSpriteSizeEditor();
        activeSpriteSize_ = perItemSize
            ? (activeEditor.loaded ? placement.size : activeEditor.size)
            : spriteGlobalSize_;
        if (!perItemSize) activateSpriteEditorBank(spriteGlobalSize_);
        syncSpriteDrawingColor();
    }
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterSet(int value)
{
    value = std::clamp(value, 0, characterSetCount() - 1);
    if (characterPanActive_ && value != activeCharacterSet_) finishCharacterPan();
    bool changed = activeCharacterSet_ != value;
    activeCharacterSet_ = value;
    if (!characterEditorSlots_.empty()) {
        auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
        if (slot.loaded && slot.setIndex != value) {
            slot.setIndex = value;
            changed = true;
        }
    }
    if (!changed) return;
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterPattern(int value)
{
    value = std::clamp(value, 0, characterPatternsPerSet() - 1);
    if (characterPanActive_ && value != activeCharacterPattern_) finishCharacterPan();
    bool changed = activeCharacterPattern_ != value;
    activeCharacterPattern_ = value;
    if (!characterEditorSlots_.empty()) {
        auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
        if (!slot.loaded || slot.setIndex != activeCharacterSet_
            || slot.patternIndex != value) {
            slot.loaded = true;
            slot.setIndex = activeCharacterSet_;
            slot.patternIndex = value;
            changed = true;
        }
    }
    if (!changed) return;
    emit projectChanged();
}

void EditorProjectController::setCharacterForegroundColorIndex(int value)
{
    value = std::clamp(value, 0, std::max(0, static_cast<int>(characterPaletteColors().size()) - 1));
    if (characterForegroundColorIndex_ == value) return;
    characterForegroundColorIndex_ = value;
    emit projectChanged();
}

void EditorProjectController::setCharacterBackgroundColorIndex(int value)
{
    value = std::clamp(value, 0, std::max(0, static_cast<int>(characterPaletteColors().size()) - 1));
    if (characterBackgroundColorIndex_ == value) return;
    characterBackgroundColorIndex_ = value;
    emit projectChanged();
}

void EditorProjectController::setActiveCharacterEditor(int value)
{
    if (characterEditorSlots_.empty()) return;
    value = std::clamp(value, 0, static_cast<int>(characterEditorSlots_.size()) - 1);
    if (characterPanActive_ && value != activeCharacterEditor_) finishCharacterPan();
    if (activeCharacterEditor_ == value) return;
    activeCharacterEditor_ = value;
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(value)];
    activeCharacterPlane_ = usesGenesisMode5Editor() ? slot.plane : 0;
    if (slot.loaded) {
        activeCharacterSet_ = slot.setIndex;
        activeCharacterPattern_ = slot.patternIndex;
        if (usesGenesisMode5Editor()) characterPaletteBank_ = slot.palette;
    }
    emit projectChanged();
}

void EditorProjectController::setCharacterTilingMode(bool value)
{
    if (characterTilingMode_ == value) return;
    if (value && characterPanActive_) finishCharacterPan();
    characterTilingMode_ = value;
    emit projectChanged();
}

void EditorProjectController::setCharacterPanActive(bool value)
{
    if (characterPanActive_ == value) return;
    if (!value) {
        finishCharacterPan();
        return;
    }
    if (characterTilingMode_ || characterEditorSlots_.empty()) return;
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return;
    endCharacterEdit();
    characterPanSetIndex_ = slot.setIndex;
    characterPanPatternIndex_ = slot.patternIndex;
    characterPanOriginal_ = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                                .patterns[static_cast<std::size_t>(slot.patternIndex)];
    characterPanX_ = 0;
    characterPanY_ = 0;
    characterPanActive_ = true;
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::setActiveSpriteSet(int value)
{
    const int maximum = std::max(0, static_cast<int>(spriteSets_.size()) - 1);
    value = std::clamp(value, 0, maximum);
    if (spritePanActive_ && value != activeSpriteSet_) finishSpritePan();
    bool changed = activeSpriteSet_ != value;
    activeSpriteSet_ = value;
    const auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                                .placements[static_cast<std::size_t>(activeSprite_)];
    activeSpriteSize_ = usesPerSpriteSizeEditor()
        ? placement.size : spriteGlobalSize_;
    changed = assignActiveSpriteToEditor() || changed;
    syncSpriteDrawingColor();
    if (!changed) return;
    emit projectChanged();
}

void EditorProjectController::setActiveSprite(int value)
{
    value = std::clamp(value, 0, spritePatternsPerSet() - 1);
    if (spritePanActive_ && value != activeSprite_) finishSpritePan();
    bool changed = activeSprite_ != value;
    activeSprite_ = value;
    if (!spriteSets_.empty()) {
        const auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                                    .placements[static_cast<std::size_t>(activeSprite_)];
        activeSpriteSize_ = usesPerSpriteSizeEditor()
            ? placement.size : spriteGlobalSize_;
        changed = assignActiveSpriteToEditor() || changed;
        syncSpriteDrawingColor();
    }
    if (!changed) return;
    emit projectChanged();
}

void EditorProjectController::setActiveSpriteEditor(int value)
{
    if (spriteEditorSlots_.empty()) return;
    value = std::clamp(value, 0, static_cast<int>(spriteEditorSlots_.size()) - 1);
    if (spritePanActive_ && value != activeSpriteEditor_) finishSpritePan();
    if (activeSpriteEditor_ == value) return;
    activeSpriteEditor_ = value;
    const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(value)];
    activeSpriteSize_ = slot.size;
    if (!usesPerSpriteSizeEditor()) {
        spriteGlobalSize_ = slot.size;
    }
    if (slot.loaded) {
        activeSpriteSet_ = slot.setIndex;
        activeSprite_ = slot.spriteIndex;
        syncSpriteDrawingColor();
    }
    emit projectChanged();
}

void EditorProjectController::setSpritePlacementMode(bool value)
{
    if (spritePlacementMode_ == value) return;
    if (value && spritePanActive_) finishSpritePan();
    spritePlacementMode_ = value;
    emit projectChanged();
}

void EditorProjectController::setPlacementWidth(int value)
{
    value = std::clamp(value, 8, 1024);
    if (placementWidth_ == value) return;
    placementWidth_ = value;
    emit projectChanged();
}

void EditorProjectController::setPlacementHeight(int value)
{
    value = std::clamp(value, 8, 1024);
    if (placementHeight_ == value) return;
    placementHeight_ = value;
    emit projectChanged();
}

void EditorProjectController::addSpriteSet()
{
    if (spriteSets_.size() >= 32U) {
        setStatus({}, QStringLiteral("A project can currently contain up to 32 sprite sets."));
        return;
    }
    spriteSets_.push_back(makeSpriteSet(static_cast<int>(spriteSets_.size()) + 1));
    activeSpriteSet_ = static_cast<int>(spriteSets_.size()) - 1;
    activeSprite_ = 0;
    activeSpriteSize_ = usesPerSpriteSizeEditor() ? 8 : spriteGlobalSize_;
    assignActiveSpriteToEditor();
    syncSpriteDrawingColor();
    emit projectChanged();
}

void EditorProjectController::removeActiveSpriteSet()
{
    if (spritePanActive_) finishSpritePan();
    if (spriteSets_.size() <= 1U) return;
    const int removedSet = activeSpriteSet_;
    spriteSets_.erase(spriteSets_.begin() + removedSet);
    activeSpriteSet_ = std::min(activeSpriteSet_, static_cast<int>(spriteSets_.size()) - 1);
    for (auto& editor : spriteEditorSlots_) {
        if (editor.setIndex == removedSet) {
            editor.loaded = false;
            editor.setIndex = activeSpriteSet_;
        } else if (editor.setIndex > removedSet) {
            --editor.setIndex;
        }
    }
    const auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                                .placements[static_cast<std::size_t>(activeSprite_)];
    activeSpriteSize_ = usesPerSpriteSizeEditor()
        ? placement.size : spriteGlobalSize_;
    syncSpriteDrawingColor();
    emit projectChanged();
}

void EditorProjectController::moveSprite(int spriteIndex, int x, int y)
{
    if (spriteSets_.empty() || spriteIndex < 0
        || spriteIndex >= spritePatternsPerSet()) return;
    auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                          .placements[static_cast<std::size_t>(spriteIndex)];
    const int effectiveSize = usesPerSpriteSizeEditor()
        ? placement.size : spriteGlobalSize_;
    x = std::clamp(x, -spritePatternWidth(effectiveSize), placementWidth_ - 1);
    y = std::clamp(y, -spritePatternHeight(effectiveSize), placementHeight_ - 1);
    int& placementX = effectiveSize == 16 ? placement.x16 : placement.x;
    int& placementY = effectiveSize == 16 ? placement.y16 : placement.y;
    if (placementX == x && placementY == y) return;
    placementX = x;
    placementY = y;
    emit projectChanged();
}

void EditorProjectController::addSpriteEditor()
{
    const std::size_t maximumEditors = static_cast<std::size_t>(
        spritePatternsPerSet());
    if (spriteEditorSlots_.size() >= maximumEditors) {
        setStatus({}, QStringLiteral("A sprite tray can contain up to %1 unique sprite editors.")
                          .arg(spritePatternsPerSet()));
        return;
    }
    const bool perItemSize = usesPerSpriteSizeEditor();
    const int editorSize = perItemSize ? activeSpriteSize_ : spriteGlobalSize_;
    spriteEditorSlots_.push_back(
        {false, activeSpriteSet_, activeSprite_, editorSize});
    activeSpriteEditor_ = static_cast<int>(spriteEditorSlots_.size()) - 1;
    emit projectChanged();
}

void EditorProjectController::removeActiveSpriteEditor()
{
    if (spriteEditorSlots_.size() <= 1U) return;
    const bool perItemSize = usesPerSpriteSizeEditor();
    const int activeBankSize = perItemSize ? activeSpriteSize_ : spriteGlobalSize_;
    if (!perItemSize) {
        const int bankEditorCount = static_cast<int>(std::count_if(
            spriteEditorSlots_.cbegin(), spriteEditorSlots_.cend(),
            [activeBankSize](const SpriteEditorSlot& editor) {
                return editor.size == activeBankSize;
            }));
        if (bankEditorCount <= 1) return;
    }
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();
    spriteEditorSlots_.erase(spriteEditorSlots_.begin() + activeSpriteEditor_);
    activeSpriteEditor_ = std::min(
        activeSpriteEditor_, static_cast<int>(spriteEditorSlots_.size()) - 1);
    if (!perItemSize) {
        activateSpriteEditorBank(activeBankSize);
        emit projectChanged();
        return;
    }
    const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(activeSpriteEditor_)];
    if (slot.loaded) {
        activeSpriteSet_ = slot.setIndex;
        activeSprite_ = slot.spriteIndex;
        activeSpriteSize_ = slot.size;
        if (!perItemSize) spriteGlobalSize_ = slot.size;
        syncSpriteDrawingColor();
    }
    emit projectChanged();
}

void EditorProjectController::moveSpriteEditor(int fromIndex, int toIndex)
{
    const int count = static_cast<int>(spriteEditorSlots_.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count
        || fromIndex == toIndex) {
        return;
    }
    SpriteEditorSlot slot = spriteEditorSlots_[static_cast<std::size_t>(fromIndex)];
    spriteEditorSlots_.erase(spriteEditorSlots_.begin() + fromIndex);
    spriteEditorSlots_.insert(spriteEditorSlots_.begin() + toIndex, slot);
    if (activeSpriteEditor_ == fromIndex) {
        activeSpriteEditor_ = toIndex;
    } else if (fromIndex < activeSpriteEditor_ && activeSpriteEditor_ <= toIndex) {
        --activeSpriteEditor_;
    } else if (toIndex <= activeSpriteEditor_ && activeSpriteEditor_ < fromIndex) {
        ++activeSpriteEditor_;
    }
    emit projectChanged();
}

void EditorProjectController::moveSpriteEditorTile(int editorIndex, int x, int y)
{
    if (editorIndex < 0 || editorIndex >= static_cast<int>(spriteEditorSlots_.size())) {
        return;
    }
    const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(editorIndex)];
    if (!slot.loaded || slot.setIndex < 0
        || slot.setIndex >= static_cast<int>(spriteSets_.size())
        || slot.spriteIndex < 0 || slot.spriteIndex >= spritePatternsPerSet()) {
        return;
    }
    auto& placement = spriteSets_[static_cast<std::size_t>(slot.setIndex)]
                          .placements[static_cast<std::size_t>(slot.spriteIndex)];
    const int effectiveSize = slot.size;
    x = std::clamp(x, -spritePatternWidth(effectiveSize), placementWidth_ - 1);
    y = std::clamp(y, -spritePatternHeight(effectiveSize), placementHeight_ - 1);
    int& placementX = effectiveSize == 16 ? placement.x16 : placement.x;
    int& placementY = effectiveSize == 16 ? placement.y16 : placement.y;
    if (placementX == x && placementY == y) return;
    placementX = x;
    placementY = y;
    emit projectChanged();
}

QVariantList EditorProjectController::characterPatternRows(int setIndex,
                                                           int patternIndex) const
{
    QVariantList result;
    if (setIndex < 0 || setIndex >= static_cast<int>(characterSets_.size())
        || patternIndex < 0 || patternIndex >= characterPatternsPerSet()) {
        return result;
    }
    CharacterPattern panPattern;
    const CharacterPattern* pattern = &characterSets_[static_cast<std::size_t>(setIndex)]
                                           .patterns[static_cast<std::size_t>(patternIndex)];
    if (characterPanActive_ && setIndex == characterPanSetIndex_
        && patternIndex == characterPanPatternIndex_) {
        panPattern = pannedCharacterPattern();
        pattern = &panPattern;
    }
    result.reserve(8);
    for (int row = 0; row < 8; ++row) {
        const int bitmap = pattern->bitmap[static_cast<std::size_t>(row)];
        const int color = pattern->colors[static_cast<std::size_t>(row)];
        QVariantMap values{{QStringLiteral("row"), row},
                           {QStringLiteral("pattern"), bitmap},
                           {QStringLiteral("color"), color},
                           {QStringLiteral("foreground"), color >> 4},
                           {QStringLiteral("background"), color & 0x0f},
                           {QStringLiteral("indexed"), usesIndexed4BppEditor()}};
        if (usesIndexed4BppEditor()) {
            QVariantList pixels;
            pixels.reserve(8);
            for (int column = 0; column < 8; ++column) {
                pixels.push_back(pattern->indexedOverride
                    ? pattern->indexedPixels[
                          static_cast<std::size_t>(row * 8 + column)]
                    : ((bitmap & (0x80U >> column)) != 0 ? color >> 4
                                                         : color & 0x0f));
            }
            values.insert(QStringLiteral("pixels"), pixels);
        }
        result.push_back(values);
    }
    return result;
}

void EditorProjectController::paintCharacterPixel(int setIndex,
                                                   int patternIndex,
                                                   int row,
                                                   int column,
                                                   bool foreground)
{
    if (setIndex < 0 || setIndex >= static_cast<int>(characterSets_.size())
        || patternIndex < 0 || patternIndex >= characterPatternsPerSet()
        || row < 0 || row >= 8 || column < 0 || column >= 8) {
        return;
    }
    if (characterPanActive_) return;
    if (characterEditActive_
        && (characterEditSetIndex_ != setIndex
            || characterEditPatternIndex_ != patternIndex)) {
        endCharacterEdit();
    }
    const bool standaloneEdit = !characterEditActive_;
    if (standaloneEdit) beginCharacterEdit(setIndex, patternIndex);
    auto& pattern = characterSets_[static_cast<std::size_t>(setIndex)]
                        .patterns[static_cast<std::size_t>(patternIndex)];
    if (usesIndexed4BppEditor()) {
        ensureIndexedCharacterOverride(pattern);
        auto& pixel = pattern.indexedPixels[
            static_cast<std::size_t>(row * 8 + column)];
        const auto value = static_cast<std::uint8_t>(foreground
            ? characterForegroundColorIndex_ : characterBackgroundColorIndex_);
        if (pixel != value) {
            pixel = value;
            ++characterRevision_;
            emit projectChanged();
        }
        if (standaloneEdit) endCharacterEdit();
        return;
    }
    auto& bitmap = pattern.bitmap[static_cast<std::size_t>(row)];
    auto& color = pattern.colors[static_cast<std::size_t>(row)];
    const auto mask = static_cast<std::uint8_t>(0x80U >> column);
    const std::uint8_t nextBitmap = foreground
        ? static_cast<std::uint8_t>(bitmap | mask)
        : static_cast<std::uint8_t>(bitmap & static_cast<std::uint8_t>(~mask));
    const auto nextColor = static_cast<std::uint8_t>(
        (characterForegroundColorIndex_ << 4) | characterBackgroundColorIndex_);
    if (bitmap != nextBitmap || color != nextColor) {
        bitmap = nextBitmap;
        color = nextColor;
        ++characterRevision_;
        emit projectChanged();
    }
    if (standaloneEdit) endCharacterEdit();
}

void EditorProjectController::drawCharacterLine(int setIndex,
                                                int patternIndex,
                                                int fromRow,
                                                int fromColumn,
                                                int toRow,
                                                int toColumn,
                                                bool foreground)
{
    if (setIndex < 0 || setIndex >= static_cast<int>(characterSets_.size())
        || patternIndex < 0 || patternIndex >= characterPatternsPerSet()
        || characterPanActive_) {
        return;
    }
    fromRow = std::clamp(fromRow, 0, 7);
    fromColumn = std::clamp(fromColumn, 0, 7);
    toRow = std::clamp(toRow, 0, 7);
    toColumn = std::clamp(toColumn, 0, 7);
    const bool groupedEdit = characterEditActive_
        && characterEditSetIndex_ == setIndex
        && characterEditPatternIndex_ == patternIndex;
    beginCharacterEdit(setIndex, patternIndex);

    int x = fromColumn;
    int y = fromRow;
    const int deltaX = std::abs(toColumn - fromColumn);
    const int stepX = fromColumn < toColumn ? 1 : -1;
    const int deltaY = -std::abs(toRow - fromRow);
    const int stepY = fromRow < toRow ? 1 : -1;
    int error = deltaX + deltaY;
    while (true) {
        paintCharacterPixel(setIndex, patternIndex, y, x, foreground);
        if (x == toColumn && y == toRow) break;
        const int twiceError = error * 2;
        if (twiceError >= deltaY) {
            error += deltaY;
            x += stepX;
        }
        if (twiceError <= deltaX) {
            error += deltaX;
            y += stepY;
        }
    }
    if (!groupedEdit) endCharacterEdit();
}

void EditorProjectController::beginCharacterEdit(int setIndex, int patternIndex)
{
    if (setIndex < 0 || setIndex >= static_cast<int>(characterSets_.size())
        || patternIndex < 0 || patternIndex >= characterPatternsPerSet()
        || characterPanActive_) {
        return;
    }
    if (characterEditActive_) {
        if (characterEditSetIndex_ == setIndex
            && characterEditPatternIndex_ == patternIndex) {
            return;
        }
        endCharacterEdit();
    }
    characterEditActive_ = true;
    characterEditSetIndex_ = setIndex;
    characterEditPatternIndex_ = patternIndex;
    characterEditBefore_ = characterSets_[static_cast<std::size_t>(setIndex)]
                               .patterns[static_cast<std::size_t>(patternIndex)];
}

void EditorProjectController::endCharacterEdit()
{
    if (!characterEditActive_) return;
    const int setIndex = characterEditSetIndex_;
    const int patternIndex = characterEditPatternIndex_;
    characterEditActive_ = false;
    const auto& after = characterSets_[static_cast<std::size_t>(setIndex)]
                            .patterns[static_cast<std::size_t>(patternIndex)];
    recordCharacterEdit(setIndex, patternIndex, characterEditBefore_, after);
    emit projectChanged();
}

void EditorProjectController::rotateActiveCharacterPattern()
{
    if (characterEditorSlots_.empty()) return;
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return;
    auto& pattern = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                        .patterns[static_cast<std::size_t>(slot.patternIndex)];
    const CharacterPattern before = pattern;
    if (usesIndexed4BppEditor()) {
        ensureIndexedCharacterOverride(pattern);
        const auto source = pattern.indexedPixels;
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                pattern.indexedPixels[static_cast<std::size_t>(column * 8 + 7 - row)] =
                    source[static_cast<std::size_t>(row * 8 + column)];
            }
        }
    } else {
        pattern.bitmap.fill(0);
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                if ((before.bitmap[static_cast<std::size_t>(row)]
                     & (0x80U >> column)) != 0) {
                    pattern.bitmap[static_cast<std::size_t>(column)] |=
                        static_cast<std::uint8_t>(0x80U >> (7 - row));
                }
            }
        }
    }
    if (sameCharacterPattern(before, pattern)) return;
    recordCharacterEdit(slot.setIndex, slot.patternIndex, before, pattern);
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::mirrorActiveCharacterPattern()
{
    if (characterEditorSlots_.empty()) return;
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return;
    auto& pattern = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                        .patterns[static_cast<std::size_t>(slot.patternIndex)];
    const CharacterPattern before = pattern;
    if (usesIndexed4BppEditor()) {
        ensureIndexedCharacterOverride(pattern);
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 4; ++column)
                std::swap(pattern.indexedPixels[static_cast<std::size_t>(row * 8 + column)],
                          pattern.indexedPixels[static_cast<std::size_t>(row * 8 + 7 - column)]);
    } else {
        for (auto& row : pattern.bitmap) row = reverseCharacterBits(row);
    }
    if (sameCharacterPattern(before, pattern)) return;
    recordCharacterEdit(slot.setIndex, slot.patternIndex, before, pattern);
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::flipActiveCharacterPattern()
{
    if (characterEditorSlots_.empty()) return;
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return;
    auto& pattern = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                        .patterns[static_cast<std::size_t>(slot.patternIndex)];
    const CharacterPattern before = pattern;
    if (usesIndexed4BppEditor()) {
        ensureIndexedCharacterOverride(pattern);
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 8; ++column)
                std::swap(pattern.indexedPixels[static_cast<std::size_t>(row * 8 + column)],
                          pattern.indexedPixels[static_cast<std::size_t>((7 - row) * 8 + column)]);
    } else {
        std::reverse(pattern.bitmap.begin(), pattern.bitmap.end());
        std::reverse(pattern.colors.begin(), pattern.colors.end());
    }
    if (sameCharacterPattern(before, pattern)) return;
    recordCharacterEdit(slot.setIndex, slot.patternIndex, before, pattern);
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::blankActiveCharacterPattern()
{
    if (characterEditorSlots_.empty()) return;
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return;
    auto& pattern = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                        .patterns[static_cast<std::size_t>(slot.patternIndex)];
    const CharacterPattern before = pattern;
    if (usesIndexed4BppEditor()) {
        ensureIndexedCharacterOverride(pattern);
        pattern.indexedPixels.fill(0);
    } else {
        pattern.bitmap.fill(0);
    }
    if (sameCharacterPattern(before, pattern)) return;
    recordCharacterEdit(slot.setIndex, slot.patternIndex, before, pattern);
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::nudgeCharacterPan(int horizontal, int vertical)
{
    if (!characterPanActive_) return;
    const int nextX = std::clamp(characterPanX_ + horizontal, -8, 8);
    const int nextY = std::clamp(characterPanY_ + vertical, -8, 8);
    if (nextX == characterPanX_ && nextY == characterPanY_) return;
    characterPanX_ = nextX;
    characterPanY_ = nextY;
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::centerCharacterPan()
{
    if (!characterPanActive_ || (characterPanX_ == 0 && characterPanY_ == 0)) return;
    characterPanX_ = 0;
    characterPanY_ = 0;
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::undoCharacterEdit()
{
    if (characterPanActive_) return;
    endCharacterEdit();
    if (characterUndoHistory_.empty()) return;
    const CharacterHistoryEntry entry = characterUndoHistory_.back();
    characterUndoHistory_.pop_back();
    for (const auto& change : entry.changes) {
        characterSets_[static_cast<std::size_t>(change.setIndex)]
            .patterns[static_cast<std::size_t>(change.patternIndex)] = change.before;
    }
    characterRedoHistory_.push_back(entry);
    ++characterRevision_;
    emit projectChanged();
}

void EditorProjectController::redoCharacterEdit()
{
    if (characterPanActive_) return;
    endCharacterEdit();
    if (characterRedoHistory_.empty()) return;
    const CharacterHistoryEntry entry = characterRedoHistory_.back();
    characterRedoHistory_.pop_back();
    for (const auto& change : entry.changes) {
        characterSets_[static_cast<std::size_t>(change.setIndex)]
            .patterns[static_cast<std::size_t>(change.patternIndex)] = change.after;
    }
    characterUndoHistory_.push_back(entry);
    ++characterRevision_;
    emit projectChanged();
}

bool EditorProjectController::copyActiveCharacterPattern()
{
    if (characterPanActive_) {
        setStatus({}, QStringLiteral("Finish pattern panning before copying."));
        return false;
    }
    endCharacterEdit();
    if (characterEditorSlots_.empty()) return false;
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return false;
    const auto& pattern = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                              .patterns[static_cast<std::size_t>(slot.patternIndex)];
    QJsonArray bitmap;
    QJsonArray colors;
    QJsonArray indexedPixels;
    for (int row = 0; row < 8; ++row) {
        bitmap.push_back(QStringLiteral("%1")
                             .arg(pattern.bitmap[static_cast<std::size_t>(row)],
                                  2, 16, QLatin1Char('0'))
                             .toUpper());
        colors.push_back(QStringLiteral("%1")
                             .arg(pattern.colors[static_cast<std::size_t>(row)],
                                  2, 16, QLatin1Char('0'))
                             .toUpper());
    }
    if (pattern.indexedOverride) {
        for (const std::uint8_t value : pattern.indexedPixels)
            indexedPixels.push_back(value);
    }
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("retrovdp.character-pattern")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("size"),
         QJsonObject{{QStringLiteral("width"), 8},
                     {QStringLiteral("height"), 8}}},
        {QStringLiteral("source"),
         QJsonObject{{QStringLiteral("set"), slot.setIndex},
                     {QStringLiteral("pattern"), slot.patternIndex}}},
        {QStringLiteral("bitmap"), bitmap},
        {QStringLiteral("colors"), colors},
        {QStringLiteral("indexedOverride"), pattern.indexedOverride},
        {QStringLiteral("indexedPixels"), indexedPixels},
    };
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        setStatus({}, QStringLiteral("The system clipboard is unavailable."));
        return false;
    }
    clipboard->setText(QString::fromUtf8(QJsonDocument(root).toJson(
        QJsonDocument::Indented)));
    setStatus(QStringLiteral("Copied character pattern %1 to the clipboard.")
                  .arg(slot.patternIndex, 2, 16, QLatin1Char('0'))
                  .toUpper());
    return true;
}

bool EditorProjectController::pasteActiveCharacterPattern()
{
    if (characterPanActive_) {
        setStatus({}, QStringLiteral("Finish pattern panning before pasting."));
        return false;
    }
    endCharacterEdit();
    if (characterEditorSlots_.empty()) return false;
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    if (!slot.loaded) return false;
    const auto clipboardPattern = characterPatternFromClipboard();
    if (!clipboardPattern.has_value()) {
        setStatus({}, QStringLiteral("The clipboard does not contain a valid character pattern."));
        return false;
    }
    auto& pattern = characterSets_[static_cast<std::size_t>(slot.setIndex)]
                        .patterns[static_cast<std::size_t>(slot.patternIndex)];
    const CharacterPattern before = pattern;
    pattern = *clipboardPattern;
    if (!sameCharacterPattern(before, pattern)) {
        recordCharacterEdit(slot.setIndex, slot.patternIndex, before, pattern);
        ++characterRevision_;
        emit projectChanged();
    }
    setStatus(QStringLiteral("Pasted clipboard data into character pattern %1.")
                  .arg(slot.patternIndex, 2, 16, QLatin1Char('0'))
                  .toUpper());
    return true;
}

bool EditorProjectController::extractScreenImagePatterns(
    int characterX, int characterY, int regionWidth, int regionHeight,
    int destinationPattern, bool verticalWrap)
{
    const auto* screenImage = imageInput_->screenImageData();
    if (screenImage == nullptr) {
        setStatus({}, QStringLiteral("Create a Screen Image before extracting patterns."));
        return false;
    }
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();

    const int patternWidth = characterPatternWidth();
    const int patternHeight = characterPatternHeight();
    const int patternCapacity = std::min(
        characterPatternsPerSet(), static_cast<int>(characterSets_[0].patterns.size()));
    const int setCount = std::min(characterSetCount(),
                                  static_cast<int>(characterSets_.size()));
    if (patternWidth != 8 || patternHeight != 8 || setCount <= 0) {
        setStatus({}, QStringLiteral(
            "The active target's character geometry is not yet supported by the editor model."));
        return false;
    }
    characterX = std::clamp(characterX, 0, characterMapColumns() - 1);
    characterY = std::clamp(characterY, 0, characterMapRows() - 1);
    regionWidth = std::clamp(regionWidth, 1, characterMapColumns() - characterX);
    regionHeight = std::clamp(regionHeight, 1, characterMapRows() - characterY);
    destinationPattern = std::clamp(destinationPattern, 0, patternCapacity - 1);
    const int patternCount = regionWidth * regionHeight;
    if (destinationPattern + patternCount > patternCapacity) {
        setStatus({}, QStringLiteral(
            "The selected %1-pattern region does not fit after destination pattern %2.")
                          .arg(patternCount)
                          .arg(destinationPattern));
        return false;
    }
    if (usesGenesisMode5Editor()) {
        int newSlots = 0;
        for (int sourceRow = 0; sourceRow < regionHeight; ++sourceRow) {
            for (int sourceColumn = 0; sourceColumn < regionWidth; ++sourceColumn) {
                const int tileX = (characterX + sourceColumn) * patternWidth;
                const int tileY = (characterY + sourceRow) * patternHeight;
                const bool exists = std::any_of(
                    characterEditorSlots_.cbegin(), characterEditorSlots_.cend(),
                    [this, tileX, tileY](const CharacterEditorSlot& slot) {
                        return slot.plane == activeCharacterPlane_
                            && slot.tileX == tileX && slot.tileY == tileY;
                    });
                if (!exists) ++newSlots;
            }
        }
        const int maximumSlots = characterMapColumns() * characterMapRows() * 3;
        if (static_cast<int>(characterEditorSlots_.size()) + newSlots > maximumSlots) {
            setStatus({}, QStringLiteral("The selected region does not fit in the Genesis layer maps."));
            return false;
        }
    }

    const QImage source = retrovdp::imageio::toQImage(*screenImage)
                              .convertToFormat(QImage::Format_RGBA8888);
    if (source.isNull()
        || (characterX + regionWidth) * patternWidth > source.width()
        || (characterY + regionHeight) * patternHeight > source.height()) {
        setStatus({}, QStringLiteral(
            "The selected character region lies outside the Screen Image."));
        return false;
    }

    const QVariantList paletteValues = characterPaletteColors();
    std::vector<QColor> palette;
    palette.reserve(static_cast<std::size_t>(std::max<qsizetype>(16, paletteValues.size())));
    for (const QVariant& value : paletteValues) palette.push_back(value.value<QColor>());
    while (palette.size() < 16) palette.push_back(QColor(Qt::black));

    std::vector<CharacterPatternChange> changes;
    changes.reserve(static_cast<std::size_t>(patternCount));
    auto& set = characterSets_[static_cast<std::size_t>(activeCharacterSet_)];
    for (int sourceRow = 0; sourceRow < regionHeight; ++sourceRow) {
        for (int sourceColumn = 0; sourceColumn < regionWidth; ++sourceColumn) {
            const int sequence = verticalWrap
                ? sourceColumn * regionHeight + sourceRow
                : sourceRow * regionWidth + sourceColumn;
            const int patternIndex = destinationPattern + sequence;
            CharacterPattern extracted;
            if (usesIndexed4BppEditor()) {
                extracted.indexedOverride = true;
                for (int pixelRow = 0; pixelRow < patternHeight; ++pixelRow) {
                    for (int pixelColumn = 0; pixelColumn < patternWidth; ++pixelColumn) {
                        const QColor color = source.pixelColor(
                            (characterX + sourceColumn) * patternWidth + pixelColumn,
                            (characterY + sourceRow) * patternHeight + pixelRow);
                        int nearest = 0;
                        int nearestDistance = colorDistanceSquared(color, palette[0]);
                        for (int paletteIndex = 1;
                             paletteIndex < static_cast<int>(palette.size()); ++paletteIndex) {
                            const int distance = colorDistanceSquared(
                                color, palette[static_cast<std::size_t>(paletteIndex)]);
                            if (distance < nearestDistance) {
                                nearest = paletteIndex;
                                nearestDistance = distance;
                            }
                        }
                        extracted.indexedPixels[static_cast<std::size_t>(
                            pixelRow * patternWidth + pixelColumn)] =
                                static_cast<std::uint8_t>(nearest);
                    }
                }
            } else {
            for (int pixelRow = 0; pixelRow < patternHeight; ++pixelRow) {
                std::array<int, 16> counts{};
                std::array<QColor, 8> sourceColors{};
                for (int pixelColumn = 0; pixelColumn < patternWidth; ++pixelColumn) {
                    const QColor color = source.pixelColor(
                        (characterX + sourceColumn) * patternWidth + pixelColumn,
                        (characterY + sourceRow) * patternHeight + pixelRow);
                    sourceColors[static_cast<std::size_t>(pixelColumn)] = color;
                    int nearest = 1;
                    int nearestDistance = colorDistanceSquared(color, palette[1]);
                    for (int paletteIndex = 2; paletteIndex < 16; ++paletteIndex) {
                        const int distance = colorDistanceSquared(
                            color, palette[static_cast<std::size_t>(paletteIndex)]);
                        if (distance < nearestDistance) {
                            nearest = paletteIndex;
                            nearestDistance = distance;
                        }
                    }
                    ++counts[static_cast<std::size_t>(nearest)];
                }
                int background = 1;
                for (int index = 2; index < 16; ++index) {
                    if (counts[static_cast<std::size_t>(index)]
                        > counts[static_cast<std::size_t>(background)]) {
                        background = index;
                    }
                }
                int foreground = background == characterForegroundColorIndex_
                    ? (background == 15 ? 1 : 15) : characterForegroundColorIndex_;
                for (int index = 1; index < 16; ++index) {
                    if (index != background
                        && counts[static_cast<std::size_t>(index)]
                            > counts[static_cast<std::size_t>(foreground)]) {
                        foreground = index;
                    }
                }
                std::uint8_t bitmap{};
                for (int pixelColumn = 0; pixelColumn < patternWidth; ++pixelColumn) {
                    const QColor color = sourceColors[static_cast<std::size_t>(pixelColumn)];
                    if (colorDistanceSquared(
                            color, palette[static_cast<std::size_t>(foreground)])
                        <= colorDistanceSquared(
                            color, palette[static_cast<std::size_t>(background)])) {
                        bitmap |= static_cast<std::uint8_t>(0x80U >> pixelColumn);
                    }
                }
                extracted.bitmap[static_cast<std::size_t>(pixelRow)] = bitmap;
                extracted.colors[static_cast<std::size_t>(pixelRow)] =
                    static_cast<std::uint8_t>((foreground << 4) | background);
            }
            }

            auto& destination = set.patterns[static_cast<std::size_t>(patternIndex)];
            if (!sameCharacterPattern(destination, extracted)) {
                changes.push_back({activeCharacterSet_, patternIndex,
                                   destination, extracted});
                destination = extracted;
            }
        }
    }

    if (!changes.empty()) {
        recordCharacterEdits(std::move(changes));
        ++characterRevision_;
    }
    activeCharacterPattern_ = destinationPattern;
    if (usesGenesisMode5Editor()) {
        int firstImportedEditor = -1;
        for (int sourceRow = 0; sourceRow < regionHeight; ++sourceRow) {
            for (int sourceColumn = 0; sourceColumn < regionWidth; ++sourceColumn) {
                const int sequence = verticalWrap
                    ? sourceColumn * regionHeight + sourceRow
                    : sourceRow * regionWidth + sourceColumn;
                const int tileX = (characterX + sourceColumn) * patternWidth;
                const int tileY = (characterY + sourceRow) * patternHeight;
                auto match = std::find_if(
                    characterEditorSlots_.begin(), characterEditorSlots_.end(),
                    [this, tileX, tileY](const CharacterEditorSlot& slot) {
                        return slot.plane == activeCharacterPlane_
                            && slot.tileX == tileX && slot.tileY == tileY;
                    });
                if (match == characterEditorSlots_.end()) {
                    characterEditorSlots_.push_back(
                        {true, activeCharacterSet_, destinationPattern + sequence,
                         tileX, tileY, activeCharacterPlane_, characterPaletteBank_});
                    match = std::prev(characterEditorSlots_.end());
                } else {
                    match->loaded = true;
                    match->setIndex = activeCharacterSet_;
                    match->patternIndex = destinationPattern + sequence;
                    match->palette = characterPaletteBank_;
                    match->flipX = false;
                    match->flipY = false;
                    match->priority = false;
                }
                if (firstImportedEditor < 0) {
                    firstImportedEditor = static_cast<int>(
                        std::distance(characterEditorSlots_.begin(), match));
                }
            }
        }
        if (firstImportedEditor >= 0) activeCharacterEditor_ = firstImportedEditor;
    } else if (!characterEditorSlots_.empty()) {
        auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
        slot.loaded = true;
        slot.setIndex = activeCharacterSet_;
        slot.patternIndex = destinationPattern;
    }
    emit projectChanged();
    setStatus(QStringLiteral(
        "Extracted %1 Screen Image patterns into Set %2 beginning at pattern %3 (%4 wrap)%5.")
                  .arg(patternCount)
                  .arg(activeCharacterSet_ + 1)
                  .arg(destinationPattern)
                  .arg(verticalWrap ? QStringLiteral("vertical")
                                    : QStringLiteral("horizontal"))
                  .arg(usesGenesisMode5Editor()
                           ? QStringLiteral(" and placed them in the active Genesis layer")
                           : QString()));
    return true;
}

QString EditorProjectController::characterPatternPreview(
    int setIndex, int firstPattern, int patternWidth, int patternHeight,
    bool verticalWrap) const
{
    const int tileWidth = characterPatternWidth();
    const int tileHeight = characterPatternHeight();
    const int capacity = std::min(
        characterPatternsPerSet(), static_cast<int>(characterSets_[0].patterns.size()));
    if (tileWidth != 8 || tileHeight != 8 || setIndex < 0
        || setIndex >= static_cast<int>(characterSets_.size()) || capacity <= 0) {
        return {};
    }
    firstPattern = std::clamp(firstPattern, 0, capacity - 1);
    patternWidth = std::clamp(patternWidth, 1, characterMapColumns());
    patternHeight = std::clamp(patternHeight, 1, characterMapRows());
    QImage preview(patternWidth * tileWidth, patternHeight * tileHeight,
                   QImage::Format_RGBA8888);
    preview.fill(QColor(16, 22, 28));
    const QVariantList paletteValues = characterPaletteColors();
    const auto& set = characterSets_[static_cast<std::size_t>(setIndex)];
    for (int row = 0; row < patternHeight; ++row) {
        for (int column = 0; column < patternWidth; ++column) {
            const int offset = verticalWrap
                ? column * patternHeight + row : row * patternWidth + column;
            const int patternIndex = firstPattern + offset;
            if (patternIndex >= capacity) continue;
            const auto& pattern = set.patterns[static_cast<std::size_t>(patternIndex)];
            for (int pixelRow = 0; pixelRow < tileHeight; ++pixelRow) {
                const std::uint8_t color = pattern.colors[
                    static_cast<std::size_t>(pixelRow)];
                const int foreground = color >> 4U;
                const int background = color & 0x0fU;
                for (int pixelColumn = 0; pixelColumn < tileWidth; ++pixelColumn) {
                    int paletteIndex{};
                    if (usesIndexed4BppEditor()) {
                        paletteIndex = pattern.indexedOverride
                            ? pattern.indexedPixels[static_cast<std::size_t>(
                                  pixelRow * tileWidth + pixelColumn)]
                            : ((pattern.bitmap[static_cast<std::size_t>(pixelRow)]
                                & (0x80U >> pixelColumn)) != 0
                                  ? foreground : background);
                    } else {
                        const bool ink =
                            (pattern.bitmap[static_cast<std::size_t>(pixelRow)]
                             & (0x80U >> pixelColumn)) != 0;
                        paletteIndex = ink ? foreground : background;
                    }
                    QColor pixel = paletteIndex < paletteValues.size()
                        ? paletteValues.at(paletteIndex).value<QColor>()
                        : QColor(Qt::black);
                    if (pixel.alpha() == 0) pixel = QColor(48, 56, 66);
                    preview.setPixelColor(column * tileWidth + pixelColumn,
                                          row * tileHeight + pixelRow, pixel);
                }
            }
        }
    }
    return imageDataUrl(preview);
}

void EditorProjectController::addCharacterEditor()
{
    const int columns = characterMapColumns();
    const int rows = characterMapRows();
    const std::size_t maximumEditors = usesGenesisMode5Editor()
        ? static_cast<std::size_t>(columns * rows * 3) : 768U;
    if (characterEditorSlots_.size() >= maximumEditors) {
        setStatus({}, QStringLiteral("The active target's character maps are full."));
        return;
    }
    std::vector<bool> occupied(static_cast<std::size_t>(columns * rows));
    for (const auto& slot : characterEditorSlots_) {
        if (usesGenesisMode5Editor() && slot.plane != activeCharacterPlane_) continue;
        const int column = std::clamp(slot.tileX / 8, 0, columns - 1);
        const int row = std::clamp(slot.tileY / 8, 0, rows - 1);
        occupied[static_cast<std::size_t>(row * columns + column)] = true;
    }
    int placement = 0;
    while (placement < static_cast<int>(occupied.size())
           && occupied[static_cast<std::size_t>(placement)]) {
        ++placement;
    }
    if (placement >= static_cast<int>(occupied.size())) placement = 0;
    characterEditorSlots_.push_back(
        {false, activeCharacterSet_, activeCharacterPattern_,
         (placement % columns) * 8, (placement / columns) * 8,
         activeCharacterPlane_, characterPaletteBank_});
    activeCharacterEditor_ = static_cast<int>(characterEditorSlots_.size()) - 1;
    emit projectChanged();
}

void EditorProjectController::removeActiveCharacterEditor()
{
    if (characterEditorSlots_.size() <= 1U) return;
    characterEditorSlots_.erase(characterEditorSlots_.begin() + activeCharacterEditor_);
    activeCharacterEditor_ = std::min(
        activeCharacterEditor_, static_cast<int>(characterEditorSlots_.size()) - 1);
    const auto& slot = characterEditorSlots_[static_cast<std::size_t>(activeCharacterEditor_)];
    activeCharacterPlane_ = usesGenesisMode5Editor() ? slot.plane : 0;
    if (slot.loaded) {
        activeCharacterSet_ = slot.setIndex;
        activeCharacterPattern_ = slot.patternIndex;
        if (usesGenesisMode5Editor()) characterPaletteBank_ = slot.palette;
    }
    emit projectChanged();
}

void EditorProjectController::moveCharacterEditor(int fromIndex, int toIndex)
{
    const int count = static_cast<int>(characterEditorSlots_.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count
        || fromIndex == toIndex) {
        return;
    }
    CharacterEditorSlot slot = characterEditorSlots_[static_cast<std::size_t>(fromIndex)];
    characterEditorSlots_.erase(characterEditorSlots_.begin() + fromIndex);
    characterEditorSlots_.insert(characterEditorSlots_.begin() + toIndex, slot);
    if (activeCharacterEditor_ == fromIndex) {
        activeCharacterEditor_ = toIndex;
    } else if (fromIndex < activeCharacterEditor_ && activeCharacterEditor_ <= toIndex) {
        --activeCharacterEditor_;
    } else if (toIndex <= activeCharacterEditor_ && activeCharacterEditor_ < fromIndex) {
        ++activeCharacterEditor_;
    }
    emit projectChanged();
}

void EditorProjectController::moveCharacterTile(int editorIndex, int x, int y)
{
    if (editorIndex < 0
        || editorIndex >= static_cast<int>(characterEditorSlots_.size())) {
        return;
    }
    auto& slot = characterEditorSlots_[static_cast<std::size_t>(editorIndex)];
    const int snappedX = snapCharacterTileCoordinate(
        x, (characterMapColumns() - 1) * characterPatternWidth());
    const int snappedY = snapCharacterTileCoordinate(
        y, (characterMapRows() - 1) * characterPatternHeight());
    if (slot.tileX == snappedX && slot.tileY == snappedY) return;
    slot.tileX = snappedX;
    slot.tileY = snappedY;
    emit projectChanged();
}

bool EditorProjectController::exportGenesisCharacterAssets(const QUrl& directoryUrl)
{
    if (!usesGenesisMode5Editor()) {
        setStatus({}, QStringLiteral("Select the Sega Genesis target before exporting native character assets."));
        return false;
    }
    const QString path = directoryUrl.toLocalFile();
    QDir directory(path);
    if (path.isEmpty() || !directory.exists()) {
        setStatus({}, QStringLiteral("Choose an existing local export folder."));
        return false;
    }

    QString base = projectName_.trimmed().toUpper();
    for (QChar& character : base) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('_')
            && character != QLatin1Char('-')) character = QLatin1Char('_');
    }
    while (base.contains(QStringLiteral("__"))) base.replace(QStringLiteral("__"), QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("GENESIS");

    QByteArray tiles;
    tiles.reserve(characterPatternsPerSet() * 32);
    const auto& set = characterSets_[static_cast<std::size_t>(activeCharacterSet_)];
    for (const auto& pattern : set.patterns) {
        for (int row = 0; row < 8; ++row) {
            const int foreground = (pattern.colors[static_cast<std::size_t>(row)] >> 4U) & 0x0f;
            const int background = pattern.colors[static_cast<std::size_t>(row)] & 0x0f;
            for (int column = 0; column < 8; column += 2) {
                const auto pixel = [&](int x) {
                    if (pattern.indexedOverride)
                        return static_cast<int>(pattern.indexedPixels[
                            static_cast<std::size_t>(row * 8 + x)] & 0x0fU);
                    return (pattern.bitmap[static_cast<std::size_t>(row)]
                            & (0x80U >> x)) != 0 ? foreground : background;
                };
                tiles.append(static_cast<char>((pixel(column) << 4) | pixel(column + 1)));
            }
        }
    }

    const int mapColumns = imageInput_->targetWidth() >= 320 ? 64 : 32;
    constexpr int mapRows = 32;
    std::array<QByteArray, 3> maps;
    std::array<std::vector<bool>, 3> occupied;
    for (int plane = 0; plane < 3; ++plane) {
        maps[static_cast<std::size_t>(plane)] = QByteArray(mapColumns * mapRows * 2, '\0');
        occupied[static_cast<std::size_t>(plane)].resize(
            static_cast<std::size_t>(mapColumns * mapRows));
    }
    for (const auto& slot : characterEditorSlots_) {
        if (!slot.loaded) continue;
        if (slot.setIndex != activeCharacterSet_) {
            setStatus({}, QStringLiteral("Genesis maps reference more than one pattern set. Select one set per native export."));
            return false;
        }
        const int plane = std::clamp(slot.plane, 0, 2);
        const int column = slot.tileX / 8;
        const int row = slot.tileY / 8;
        if (column < 0 || column >= mapColumns || row < 0 || row >= mapRows) continue;
        const std::size_t cell = static_cast<std::size_t>(row * mapColumns + column);
        if (occupied[static_cast<std::size_t>(plane)][cell]) {
            setStatus({}, QStringLiteral("Two tiles occupy the same cell in %1. Move or remove one before export.")
                              .arg(plane == 0 ? QStringLiteral("Plane A")
                                   : plane == 1 ? QStringLiteral("Plane B")
                                                : QStringLiteral("Window")));
            return false;
        }
        occupied[static_cast<std::size_t>(plane)][cell] = true;
        const std::uint16_t word = static_cast<std::uint16_t>(
            (slot.patternIndex & 0x07ff)
            | (slot.flipX ? 0x0800 : 0)
            | (slot.flipY ? 0x1000 : 0)
            | ((slot.palette & 0x03) << 13)
            | (slot.priority ? 0x8000 : 0));
        QByteArray& map = maps[static_cast<std::size_t>(plane)];
        map[static_cast<qsizetype>(cell * 2)] = static_cast<char>(word >> 8U);
        map[static_cast<qsizetype>(cell * 2 + 1)] = static_cast<char>(word & 0xffU);
    }

    QByteArray palette;
    palette.reserve(128);
    const QVariantList colors = imageInput_->paletteColors();
    for (int index = 0; index < 64; ++index) {
        const QColor color = index < colors.size() ? colors.at(index).value<QColor>() : QColor(Qt::black);
        const int red = std::clamp(static_cast<int>(std::lround(color.red() * 7.0 / 255.0)), 0, 7);
        const int green = std::clamp(static_cast<int>(std::lround(color.green() * 7.0 / 255.0)), 0, 7);
        const int blue = std::clamp(static_cast<int>(std::lround(color.blue() * 7.0 / 255.0)), 0, 7);
        const std::uint16_t word = static_cast<std::uint16_t>(
            (blue << 9U) | (green << 5U) | (red << 1U));
        palette.append(static_cast<char>(word >> 8U));
        palette.append(static_cast<char>(word & 0xffU));
    }

    QByteArray registers(24, '\0');
    const bool wide = mapColumns == 64;
    const bool pal30 = imageInput_->targetHeight() >= 240;
    registers[0] = static_cast<char>(0x04);
    registers[1] = static_cast<char>(0x74 | (pal30 ? 0x08 : 0));
    registers[2] = static_cast<char>(0x30);
    registers[3] = static_cast<char>(0x2c);
    registers[4] = static_cast<char>(0x07);
    registers[5] = static_cast<char>(wide ? 0x54 : 0x5f);
    registers[10] = static_cast<char>(0xff);
    registers[12] = static_cast<char>(wide ? 0x81 : 0x00);
    registers[13] = static_cast<char>(wide ? 0x2b : 0x2e);
    registers[15] = static_cast<char>(0x02);
    registers[16] = static_cast<char>(wide ? 0x01 : 0x00);

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
    if (!writeFile(QStringLiteral(".TILES"), tiles)
        || !writeFile(QStringLiteral(".PLANE_A.MAP"), maps[0])
        || !writeFile(QStringLiteral(".PLANE_B.MAP"), maps[1])
        || !writeFile(QStringLiteral(".WINDOW.MAP"), maps[2])
        || !writeFile(QStringLiteral(".PAL"), palette)
        || !writeFile(QStringLiteral(".REG"), registers)) return false;

    setStatus(QStringLiteral("Exported Genesis tiles, three maps, palette, and display registers to %1")
                  .arg(QDir::toNativeSeparators(directory.absolutePath())));
    return true;
}

bool EditorProjectController::saveRecipe(const QUrl& fileUrl)
{
    if (characterPanActive_) finishCharacterPan();
    endCharacterEdit();
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();
    QString path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        setStatus({}, QStringLiteral("Choose a local project file."));
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".rvdp.json");

    QJsonArray characterSets;
    for (const auto& set : characterSets_) {
        QJsonArray savedPatterns;
        for (int patternIndex = 0;
             patternIndex < static_cast<int>(set.patterns.size()); ++patternIndex) {
            const auto& pattern = set.patterns[static_cast<std::size_t>(patternIndex)];
            const bool hasBitmap = std::any_of(pattern.bitmap.begin(), pattern.bitmap.end(),
                                               [](std::uint8_t value) { return value != 0; });
            const bool hasCustomColors = std::any_of(
                pattern.colors.begin(), pattern.colors.end(),
                [](std::uint8_t value) { return value != defaultCharacterColor; });
            if (!hasBitmap && !hasCustomColors && !pattern.indexedOverride) continue;
            QJsonArray bitmap;
            QJsonArray colors;
            for (const std::uint8_t value : pattern.bitmap) bitmap.push_back(value);
            for (const std::uint8_t value : pattern.colors) colors.push_back(value);
            QJsonArray indexedPixels;
            if (pattern.indexedOverride) {
                for (const std::uint8_t value : pattern.indexedPixels)
                    indexedPixels.push_back(value);
            }
            savedPatterns.push_back(QJsonObject{
                {QStringLiteral("index"), patternIndex},
                {QStringLiteral("bitmap"), bitmap},
                {QStringLiteral("colors"), colors},
                {QStringLiteral("indexedOverride"), pattern.indexedOverride},
                {QStringLiteral("indexedPixels"), indexedPixels}});
        }
        characterSets.push_back(QJsonObject{{QStringLiteral("name"), set.name},
                                             {QStringLiteral("capacity"), 2048},
                                             {QStringLiteral("patterns"), savedPatterns}});
    }
    QJsonArray characterEditors;
    for (const auto& editor : characterEditorSlots_) {
        characterEditors.push_back(
            QJsonObject{{QStringLiteral("loaded"), editor.loaded},
                        {QStringLiteral("set"), editor.setIndex},
                        {QStringLiteral("pattern"), editor.patternIndex},
                        {QStringLiteral("tileX"), editor.tileX},
                        {QStringLiteral("tileY"), editor.tileY},
                        {QStringLiteral("plane"), editor.plane},
                        {QStringLiteral("palette"), editor.palette},
                        {QStringLiteral("flipX"), editor.flipX},
                        {QStringLiteral("flipY"), editor.flipY},
                        {QStringLiteral("priority"), editor.priority}});
    }
    QJsonArray spriteSets;
    for (const auto& set : spriteSets_) {
        const auto saveSpriteBank = [](const auto& patterns, int pixelCount) {
            QJsonArray saved;
            const int count = pixelCount;
            for (int index = 0; index < static_cast<int>(patterns.size()); ++index) {
                const auto& pattern = patterns[static_cast<std::size_t>(index)];
                const bool hasBaseline = std::any_of(
                    pattern.baselinePixels.begin(),
                    pattern.baselinePixels.begin() + count,
                    [](std::uint8_t value) { return value != 0; });
                if (!hasBaseline && !pattern.f18aOverride) continue;
                QJsonArray baseline;
                QJsonArray enhanced;
                for (int pixel = 0; pixel < count; ++pixel) {
                    baseline.push_back(pattern.baselinePixels[
                        static_cast<std::size_t>(pixel)]);
                    if (pattern.f18aOverride) {
                        enhanced.push_back(pattern.f18aPixels[
                            static_cast<std::size_t>(pixel)]);
                    }
                }
                saved.push_back(
                    QJsonObject{{QStringLiteral("index"), index},
                                {QStringLiteral("baseline"), baseline},
                                {QStringLiteral("f18aOverride"),
                                 pattern.f18aOverride},
                                {QStringLiteral("f18a"), enhanced}});
            }
            return saved;
        };
        QJsonArray placements;
        for (const auto& placement : set.placements) {
            placements.push_back(QJsonObject{{QStringLiteral("x"), placement.x},
                                              {QStringLiteral("y"), placement.y},
                                              {QStringLiteral("x16"), placement.x16},
                                              {QStringLiteral("y16"), placement.y16},
                                              {QStringLiteral("visible"), placement.visible},
                                              {QStringLiteral("size"), placement.size},
                                              {QStringLiteral("color"), placement.color},
                                              {QStringLiteral("colorDepth"),
                                               placement.colorDepth},
                                              {QStringLiteral("palette"),
                                               placement.palette},
                                              {QStringLiteral("flipX"), placement.flipX},
                                              {QStringLiteral("flipY"), placement.flipY},
                                              {QStringLiteral("priority"), placement.priority}});
        }
        spriteSets.push_back(QJsonObject{{QStringLiteral("name"), set.name},
                                          {QStringLiteral("capacity"), 128},
                                          {QStringLiteral("patterns8"),
                                           saveSpriteBank(set.patterns8, 64)},
                                          {QStringLiteral("patterns16"),
                                           saveSpriteBank(set.patterns16, 256)},
                                          {QStringLiteral("patternsGenesis"),
                                           saveSpriteBank(set.patternsGenesis, 4096)},
                                          {QStringLiteral("placements"), placements}});
    }
    QJsonArray spriteEditors;
    for (const auto& editor : spriteEditorSlots_) {
        spriteEditors.push_back(
            QJsonObject{{QStringLiteral("loaded"), editor.loaded},
                        {QStringLiteral("set"), editor.setIndex},
                        {QStringLiteral("sprite"), editor.spriteIndex},
                        {QStringLiteral("size"), editor.size}});
    }

    QJsonArray projectTargets;
    if (tms9918aEnabled_) projectTargets.push_back(QStringLiteral("tms9918a"));
    if (f18aEnabled_) projectTargets.push_back(QStringLiteral("f18a"));
    if (v9938Enabled_) projectTargets.push_back(QStringLiteral("v9938"));
    if (v9958Enabled_) projectTargets.push_back(QStringLiteral("v9958"));
    if (segaSmsEnabled_) projectTargets.push_back(QStringLiteral("sega-sms-vdp"));
    if (segaGenesisEnabled_)
        projectTargets.push_back(QStringLiteral("sega-genesis-vdp"));
    if (huc6270Enabled_) projectTargets.push_back(QStringLiteral("huc6270"));
    if (vicIiEnabled_) projectTargets.push_back(QStringLiteral("vic-ii"));
    if (vicEnabled_) projectTargets.push_back(QStringLiteral("vic"));
    if (gameBoyEnabled_) projectTargets.push_back(QStringLiteral("game-boy-ppu"));
    if (gameBoyColorEnabled_)
        projectTargets.push_back(QStringLiteral("game-boy-color-ppu"));
    if (superNesEnabled_) projectTargets.push_back(QStringLiteral("super-nes-ppu"));
    for (const QString& targetId : plannedTargetIds_)
        projectTargets.push_back(targetId);

    const QJsonObject root{
        {QStringLiteral("kind"), QStringLiteral("retrovdp-studio-recipe")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("project"),
         QJsonObject{{QStringLiteral("name"), projectName_},
                     {QStringLiteral("targets"), projectTargets}}},
        {QStringLiteral("workspace"), workspaceName(workspaceMode_)},
        {QStringLiteral("source"),
         QJsonObject{{QStringLiteral("path"), imageInput_->sourcePath()}}},
        {QStringLiteral("profiles"),
         QJsonObject{{QStringLiteral("tms9918a"),
                      QJsonObject{{QStringLiteral("enabled"), tms9918aEnabled_}}},
                     {QStringLiteral("f18a"),
                      QJsonObject{{QStringLiteral("enabled"), f18aEnabled_},
                                  {QStringLiteral("inherits"),
                                   QStringLiteral("tms9918a")}}},
                     {QStringLiteral("v9938"),
                      QJsonObject{{QStringLiteral("enabled"), v9938Enabled_},
                                  {QStringLiteral("inherits"), QStringLiteral("tms9918a")}}},
                     {QStringLiteral("v9958"),
                      QJsonObject{{QStringLiteral("enabled"), v9958Enabled_},
                                  {QStringLiteral("inherits"), QStringLiteral("v9938")}}},
                     {QStringLiteral("sega-sms-vdp"),
                      QJsonObject{{QStringLiteral("enabled"), segaSmsEnabled_}}},
                     {QStringLiteral("sega-genesis-vdp"),
                      QJsonObject{{QStringLiteral("enabled"), segaGenesisEnabled_}}},
                     {QStringLiteral("huc6270"),
                      QJsonObject{{QStringLiteral("enabled"), huc6270Enabled_}}},
                     {QStringLiteral("vic-ii"),
                      QJsonObject{{QStringLiteral("enabled"), vicIiEnabled_}}},
                     {QStringLiteral("vic"),
                      QJsonObject{{QStringLiteral("enabled"), vicEnabled_}}},
                     {QStringLiteral("game-boy-ppu"),
                      QJsonObject{{QStringLiteral("enabled"), gameBoyEnabled_}}},
                     {QStringLiteral("game-boy-color-ppu"),
                      QJsonObject{{QStringLiteral("enabled"), gameBoyColorEnabled_}}},
                     {QStringLiteral("super-nes-ppu"),
                      QJsonObject{{QStringLiteral("enabled"), superNesEnabled_}}}}},
        {QStringLiteral("preview"), previewName(previewTarget_)},
        {QStringLiteral("editScope"), editScope_ == 1
                                             ? QStringLiteral("f18a-enhancements")
                                             : QStringLiteral("shared-baseline")},
        {QStringLiteral("characterEditor"),
         QJsonObject{{QStringLiteral("activeSet"), activeCharacterSet_},
                     {QStringLiteral("activePattern"), activeCharacterPattern_},
                     {QStringLiteral("activeEditor"), activeCharacterEditor_},
                     {QStringLiteral("tilingMode"), characterTilingMode_},
                     {QStringLiteral("foregroundColor"),
                      characterForegroundColorIndex_},
                     {QStringLiteral("backgroundColor"),
                      characterBackgroundColorIndex_},
                     {QStringLiteral("paletteBank"), characterPaletteBank_},
                     {QStringLiteral("activePlane"), activeCharacterPlane_},
                     {QStringLiteral("compositePreview"), genesisCompositePreview_},
                     {QStringLiteral("editors"), characterEditors},
                     {QStringLiteral("sets"), characterSets}}},
        {QStringLiteral("spriteEditor"),
         QJsonObject{{QStringLiteral("activeSet"), activeSpriteSet_},
                     {QStringLiteral("activeSprite"), activeSprite_},
                     {QStringLiteral("activeSize"), activeSpriteSize_},
                     {QStringLiteral("globalSize"), spriteGlobalSize_},
                      {QStringLiteral("drawingColor"), spriteDrawingColorIndex_},
                      {QStringLiteral("activeEditor"), activeSpriteEditor_},
                      {QStringLiteral("editors"), spriteEditors},
                      {QStringLiteral("placementMode"), spritePlacementMode_},
                     {QStringLiteral("placementWidth"), placementWidth_},
                     {QStringLiteral("placementHeight"), placementHeight_},
                     {QStringLiteral("sets"), spriteSets}}},
        {QStringLiteral("conversion"), imageInput_->recipeSettings()},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus({}, QStringLiteral("Could not open the project for writing: %1")
                          .arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setStatus({}, QStringLiteral("Could not save the project: %1").arg(file.errorString()));
        return false;
    }
    recipePath_ = QFileInfo(path).absoluteFilePath();
    setStatus(QStringLiteral("Saved project %1").arg(QFileInfo(path).fileName()));
    emit projectChanged();
    return true;
}

bool EditorProjectController::loadRecipe(const QUrl& fileUrl)
{
    const QString path = fileUrl.toLocalFile();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus({}, QStringLiteral("Could not open the project: %1").arg(file.errorString()));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setStatus({}, QStringLiteral("Invalid project JSON: %1").arg(parseError.errorString()));
        return false;
    }
    const QJsonObject root = document.object();
    characterPanActive_ = false;
    characterPanX_ = 0;
    characterPanY_ = 0;
    characterEditActive_ = false;
    characterUndoHistory_.clear();
    characterRedoHistory_.clear();
    spritePanActive_ = false;
    spritePanX_ = 0;
    spritePanY_ = 0;
    spriteEditActive_ = false;
    spriteUndoHistory_.clear();
    spriteRedoHistory_.clear();
    const QString recipeKind = root.value(QStringLiteral("kind")).toString();
    if ((recipeKind != QStringLiteral("retrovdp-studio-recipe")
         && recipeKind != QStringLiteral("newconvert9918-recipe"))
        || root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        setStatus({}, QStringLiteral("This recipe type or schema version is not supported."));
        return false;
    }

    QString settingsError;
    if (!imageInput_->applyRecipeSettings(root.value(QStringLiteral("conversion")).toObject(),
                                          &settingsError)) {
        setStatus({}, settingsError);
        return false;
    }

    const QJsonObject project = root.value(QStringLiteral("project")).toObject();
    const QJsonArray configuredTargets = project.value(QStringLiteral("targets")).toArray();
    if (project.isEmpty()) {
        projectName_ = QFileInfo(path).completeBaseName();
        tms9918aEnabled_ = true;
        plannedTargetIds_.clear();
    } else {
        projectName_ = project.value(QStringLiteral("name"))
                           .toString(QStringLiteral("Untitled Project")).trimmed();
        if (projectName_.isEmpty()) projectName_ = QStringLiteral("Untitled Project");
        tms9918aEnabled_ = configuredTargets.contains(QStringLiteral("tms9918a"));
        v9938Enabled_ = configuredTargets.contains(QStringLiteral("v9938"));
        v9958Enabled_ = configuredTargets.contains(QStringLiteral("v9958"));
        segaSmsEnabled_ = configuredTargets.contains(QStringLiteral("sega-sms-vdp"));
        segaGenesisEnabled_ = configuredTargets.contains(
            QStringLiteral("sega-genesis-vdp"));
        huc6270Enabled_ = configuredTargets.contains(QStringLiteral("huc6270"));
        vicIiEnabled_ = configuredTargets.contains(QStringLiteral("vic-ii"));
        vicEnabled_ = configuredTargets.contains(QStringLiteral("vic"));
        gameBoyEnabled_ = configuredTargets.contains(QStringLiteral("game-boy-ppu"));
        gameBoyColorEnabled_ = configuredTargets.contains(
            QStringLiteral("game-boy-color-ppu"));
        superNesEnabled_ = configuredTargets.contains(QStringLiteral("super-nes-ppu"));
        plannedTargetIds_.clear();
        for (const QJsonValue& targetValue : configuredTargets) {
            const QString targetId = targetValue.toString().trimmed();
            if (!targetId.isEmpty() && targetId != QStringLiteral("tms9918a")
                && targetId != QStringLiteral("f18a")
                && targetId != QStringLiteral("v9938")
                && targetId != QStringLiteral("v9958")
                && targetId != QStringLiteral("sega-sms-vdp")
                && targetId != QStringLiteral("sega-genesis-vdp")
                && targetId != QStringLiteral("huc6270")
                && targetId != QStringLiteral("vic-ii")
                && targetId != QStringLiteral("vic")
                && targetId != QStringLiteral("game-boy-ppu")
                && targetId != QStringLiteral("game-boy-color-ppu")
                && targetId != QStringLiteral("super-nes-ppu")
                && !plannedTargetIds_.contains(targetId)) {
                plannedTargetIds_.push_back(targetId);
            }
        }
    }

    workspaceMode_ = workspaceValue(root.value(QStringLiteral("workspace")).toString());
    f18aEnabled_ = root.value(QStringLiteral("profiles")).toObject()
                        .value(QStringLiteral("f18a")).toObject()
                        .value(QStringLiteral("enabled")).toBool(true);
    if (!project.isEmpty())
        f18aEnabled_ = configuredTargets.contains(QStringLiteral("f18a"));
    if (project.isEmpty()) {
        const auto profiles = root.value(QStringLiteral("profiles")).toObject();
        v9938Enabled_ = profiles.value(QStringLiteral("v9938")).toObject()
                            .value(QStringLiteral("enabled")).toBool(false);
        v9958Enabled_ = profiles.value(QStringLiteral("v9958")).toObject()
                            .value(QStringLiteral("enabled")).toBool(false);
        segaSmsEnabled_ = profiles.value(QStringLiteral("sega-sms-vdp")).toObject()
                              .value(QStringLiteral("enabled")).toBool(false);
        segaGenesisEnabled_ = profiles.value(
            QStringLiteral("sega-genesis-vdp")).toObject()
                                  .value(QStringLiteral("enabled")).toBool(false);
        huc6270Enabled_ = profiles.value(QStringLiteral("huc6270")).toObject()
                              .value(QStringLiteral("enabled")).toBool(false);
        vicIiEnabled_ = profiles.value(QStringLiteral("vic-ii")).toObject()
                            .value(QStringLiteral("enabled")).toBool(false);
        vicEnabled_ = profiles.value(QStringLiteral("vic")).toObject()
                          .value(QStringLiteral("enabled")).toBool(false);
        gameBoyEnabled_ = profiles.value(QStringLiteral("game-boy-ppu")).toObject()
                              .value(QStringLiteral("enabled")).toBool(false);
        gameBoyColorEnabled_ = profiles.value(
            QStringLiteral("game-boy-color-ppu")).toObject()
                                   .value(QStringLiteral("enabled")).toBool(false);
        superNesEnabled_ = profiles.value(QStringLiteral("super-nes-ppu")).toObject()
                              .value(QStringLiteral("enabled")).toBool(false);
    }
    if (!tms9918aEnabled_ && !f18aEnabled_ && !v9938Enabled_ && !v9958Enabled_
        && !segaSmsEnabled_ && !segaGenesisEnabled_ && !huc6270Enabled_
        && !vicIiEnabled_ && !vicEnabled_ && !gameBoyEnabled_
        && !gameBoyColorEnabled_ && !superNesEnabled_)
        tms9918aEnabled_ = true;
    if (!targetEnabled(activeTarget())) {
        int fallbackTarget = 0;
        for (int candidate = 0; candidate <= 11; ++candidate) {
            if (targetEnabled(candidate)) {
                fallbackTarget = candidate;
                break;
            }
        }
        imageInput_->setTargetProfile(fallbackTarget);
    }
    previewTarget_ = activeTarget();
    const auto& activeProfile = retrovdp::core::targetProfile(
        static_cast<retrovdp::core::TargetProfileId>(activeTarget()));
    editScope_ = retrovdp::core::hasCapability(
        activeProfile.capabilities,
        retrovdp::core::TargetCapability::EnhancedColor) ? 1 : 0;

    const QJsonObject character = root.value(QStringLiteral("characterEditor")).toObject();
    activeCharacterSet_ = std::clamp(character.value(QStringLiteral("activeSet")).toInt(),
                                     0, 2);
    activeCharacterPattern_ = std::clamp(
        character.value(QStringLiteral("activePattern")).toInt(), 0,
        characterPatternsPerSet() - 1);
    characterForegroundColorIndex_ = std::clamp(
        character.value(QStringLiteral("foregroundColor")).toInt(15), 0, 15);
    characterBackgroundColorIndex_ = std::clamp(
        character.value(QStringLiteral("backgroundColor")).toInt(1), 0, 15);
    characterPaletteBank_ = std::clamp(
        character.value(QStringLiteral("paletteBank")).toInt(), 0,
        usesGenesisMode5Editor() ? 3 : (usesHuC6270Editor() ? 15 : 0));
    activeCharacterPlane_ = std::clamp(
        character.value(QStringLiteral("activePlane")).toInt(), 0,
        usesGenesisMode5Editor() ? 2 : 0);
    genesisCompositePreview_ = usesGenesisMode5Editor()
        && character.value(QStringLiteral("compositePreview")).toBool();
    characterTilingMode_ = character.value(QStringLiteral("tilingMode")).toBool();
    for (int index = 0; index < static_cast<int>(characterSets_.size()); ++index) {
        characterSets_[static_cast<std::size_t>(index)] = makeCharacterSet(index + 1);
    }
    const QJsonArray savedCharacterSets = character.value(QStringLiteral("sets")).toArray();
    for (int index = 0; index < std::min(3, static_cast<int>(savedCharacterSets.size())); ++index) {
        const QJsonObject savedSet = savedCharacterSets[index].toObject();
        auto& set = characterSets_[static_cast<std::size_t>(index)];
        const QString name = savedSet.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) set.name = name;
        const QJsonArray savedPatterns = savedSet.value(QStringLiteral("patterns")).toArray();
        for (const QJsonValue& patternValue : savedPatterns) {
            const QJsonObject savedPattern = patternValue.toObject();
            const int patternIndex = savedPattern.value(QStringLiteral("index")).toInt(-1);
            if (patternIndex < 0
                || patternIndex >= static_cast<int>(set.patterns.size())) continue;
            auto& pattern = set.patterns[static_cast<std::size_t>(patternIndex)];
            const QJsonArray bitmap = savedPattern.value(QStringLiteral("bitmap")).toArray();
            const QJsonArray colors = savedPattern.value(QStringLiteral("colors")).toArray();
            for (int row = 0; row < std::min(8, static_cast<int>(bitmap.size())); ++row) {
                pattern.bitmap[static_cast<std::size_t>(row)] = static_cast<std::uint8_t>(
                    std::clamp(bitmap[row].toInt(), 0, 255));
            }
            for (int row = 0; row < std::min(8, static_cast<int>(colors.size())); ++row) {
                pattern.colors[static_cast<std::size_t>(row)] = static_cast<std::uint8_t>(
                    std::clamp(colors[row].toInt(defaultCharacterColor), 0, 255));
            }
            const QJsonArray indexed = savedPattern.value(
                QStringLiteral("indexedPixels")).toArray();
            if (savedPattern.value(QStringLiteral("indexedOverride")).toBool()
                && indexed.size() == 64) {
                pattern.indexedOverride = true;
                for (int pixel = 0; pixel < 64; ++pixel) {
                    pattern.indexedPixels[static_cast<std::size_t>(pixel)] =
                        static_cast<std::uint8_t>(std::clamp(
                            indexed[pixel].toInt(), 0,
                            (1 << activeCharacterColorDepth()) - 1));
                }
            }
        }
    }
    ++characterRevision_;

    characterEditorSlots_.clear();
    const QJsonArray savedEditors = character.value(QStringLiteral("editors")).toArray();
    const int mapColumns = characterMapColumns();
    const int mapRows = characterMapRows();
    const int maximumSavedEditors = usesGenesisMode5Editor()
        ? mapColumns * mapRows * 3 : 768;
    for (int index = 0;
         index < std::min(maximumSavedEditors, static_cast<int>(savedEditors.size()));
         ++index) {
        const QJsonObject savedEditor = savedEditors[index].toObject();
        const int defaultX = (index % mapColumns) * 8;
        const int defaultY = ((index / mapColumns) % mapRows) * 8;
        characterEditorSlots_.push_back(
            {savedEditor.value(QStringLiteral("loaded")).toBool(),
             std::clamp(savedEditor.value(QStringLiteral("set")).toInt(), 0, 2),
             std::clamp(savedEditor.value(QStringLiteral("pattern")).toInt(), 0,
                        characterPatternsPerSet() - 1),
             snapCharacterTileCoordinate(
                 savedEditor.value(QStringLiteral("tileX")).toInt(defaultX),
                 (mapColumns - 1) * 8),
             snapCharacterTileCoordinate(
                 savedEditor.value(QStringLiteral("tileY")).toInt(defaultY),
                 (mapRows - 1) * 8),
             std::clamp(savedEditor.value(QStringLiteral("plane")).toInt(), 0,
                        usesGenesisMode5Editor() ? 2 : 0),
             std::clamp(savedEditor.value(QStringLiteral("palette")).toInt(), 0,
                        usesGenesisMode5Editor() ? 3 : 0),
             usesGenesisMode5Editor()
                 && savedEditor.value(QStringLiteral("flipX")).toBool(),
             usesGenesisMode5Editor()
                 && savedEditor.value(QStringLiteral("flipY")).toBool(),
             usesGenesisMode5Editor()
                 && savedEditor.value(QStringLiteral("priority")).toBool()});
    }
    if (characterEditorSlots_.empty()) {
        characterEditorSlots_.push_back(
            {true, activeCharacterSet_, activeCharacterPattern_, 0, 0});
    }
    activeCharacterEditor_ = std::clamp(
        character.value(QStringLiteral("activeEditor")).toInt(), 0,
        static_cast<int>(characterEditorSlots_.size()) - 1);
    const auto& activeEditor = characterEditorSlots_[
        static_cast<std::size_t>(activeCharacterEditor_)];
    if (activeEditor.loaded) {
        activeCharacterSet_ = activeEditor.setIndex;
        activeCharacterPattern_ = activeEditor.patternIndex;
        characterPaletteBank_ = activeEditor.palette;
    }
    activeCharacterPlane_ = usesGenesisMode5Editor() ? activeEditor.plane : 0;

    const QJsonObject sprite = root.value(QStringLiteral("spriteEditor")).toObject();
    spriteGlobalSize_ = normalizedSpriteSize(
        sprite.value(QStringLiteral("globalSize")).toInt(8));
    activeSpriteSize_ = normalizedSpriteSize(
        sprite.value(QStringLiteral("activeSize")).toInt(spriteGlobalSize_));
    spriteDrawingColorIndex_ = std::clamp(
        sprite.value(QStringLiteral("drawingColor")).toInt(1), 1, 15);
    placementWidth_ = std::clamp(sprite.value(QStringLiteral("placementWidth")).toInt(256),
                                 8, 1024);
    placementHeight_ = std::clamp(sprite.value(QStringLiteral("placementHeight")).toInt(192),
                                  8, 1024);
    spritePlacementMode_ = sprite.value(QStringLiteral("placementMode")).toBool();
    spriteSets_.clear();
    const QJsonArray savedSpriteSets = sprite.value(QStringLiteral("sets")).toArray();
    for (int setIndex = 0;
         setIndex < std::min(32, static_cast<int>(savedSpriteSets.size())); ++setIndex) {
        const QJsonObject savedSet = savedSpriteSets[setIndex].toObject();
        SpriteSet set = makeSpriteSet(setIndex + 1);
        const QString name = savedSet.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) set.name = name;
        const auto loadSpriteBank = [this](const QJsonArray& saved,
                                           auto& patterns,
                                           int pixelCount) {
            const int count = pixelCount;
            for (const QJsonValue& value : saved) {
                const QJsonObject object = value.toObject();
                const int index = object.value(QStringLiteral("index")).toInt(-1);
                if (index < 0 || index >= static_cast<int>(patterns.size())) continue;
                auto& pattern = patterns[static_cast<std::size_t>(index)];
                const QJsonArray baseline = object.value(
                    QStringLiteral("baseline")).toArray();
                const QJsonArray enhanced = object.value(
                    QStringLiteral("f18a")).toArray();
                for (int pixel = 0; pixel < std::min(count,
                         static_cast<int>(baseline.size())); ++pixel) {
                    pattern.baselinePixels[static_cast<std::size_t>(pixel)] =
                        static_cast<std::uint8_t>(std::clamp(
                            baseline[pixel].toInt(), 0, 1));
                }
                pattern.f18aOverride = object.value(
                    QStringLiteral("f18aOverride")).toBool();
                if (pattern.f18aOverride) {
                    pattern.f18aPixels = pattern.baselinePixels;
                    for (int pixel = 0; pixel < std::min(count,
                             static_cast<int>(enhanced.size())); ++pixel) {
                        pattern.f18aPixels[static_cast<std::size_t>(pixel)] =
                            static_cast<std::uint8_t>(std::clamp(
                            enhanced[pixel].toInt(), 0,
                            (1 << activeSpriteColorDepth()) - 1));
                    }
                }
            }
        };
        loadSpriteBank(savedSet.value(QStringLiteral("patterns8")).toArray(),
                       set.patterns8, 64);
        loadSpriteBank(savedSet.value(QStringLiteral("patterns16")).toArray(),
                       set.patterns16, 256);
        loadSpriteBank(savedSet.value(QStringLiteral("patternsGenesis")).toArray(),
                       set.patternsGenesis, 4096);
        const QJsonArray placements = savedSet.value(QStringLiteral("placements")).toArray();
        for (int index = 0;
             index < std::min(128, static_cast<int>(placements.size())); ++index) {
            const QJsonObject savedPlacement = placements[index].toObject();
            auto& placement = set.placements[static_cast<std::size_t>(index)];
            placement.x = std::clamp(savedPlacement.value(QStringLiteral("x")).toInt(),
                                     -32, placementWidth_ - 1);
            placement.y = std::clamp(savedPlacement.value(QStringLiteral("y")).toInt(),
                                     -32, placementHeight_ - 1);
            placement.x16 = std::clamp(
                savedPlacement.value(QStringLiteral("x16")).toInt(placement.x),
                -32, placementWidth_ - 1);
            placement.y16 = std::clamp(
                savedPlacement.value(QStringLiteral("y16")).toInt(placement.y),
                -32, placementHeight_ - 1);
            placement.visible = savedPlacement.value(QStringLiteral("visible")).toBool(true);
            placement.size = normalizedSpriteSize(
                savedPlacement.value(QStringLiteral("size")).toInt(8));
            placement.color = std::clamp(
                savedPlacement.value(QStringLiteral("color")).toInt(15), 1, 15);
            placement.colorDepth = usesIndexedSpriteEditor()
                ? activeSpriteColorDepth() : std::clamp(
                savedPlacement.value(QStringLiteral("colorDepth")).toInt(1), 1, 3);
            placement.palette = std::clamp(
                savedPlacement.value(QStringLiteral("palette")).toInt(), 0, 7);
            placement.flipX = savedPlacement.value(QStringLiteral("flipX")).toBool();
            placement.flipY = savedPlacement.value(QStringLiteral("flipY")).toBool();
            placement.priority = (usesGenesisMode5Editor() || usesGameBoyEditor()
                || usesGameBoyColorEditor() || usesSuperNesEditor())
                && savedPlacement.value(QStringLiteral("priority")).toBool();
        }
        spriteSets_.push_back(std::move(set));
    }
    if (spriteSets_.empty()) spriteSets_.push_back(makeSpriteSet(1));
    activeSpriteSet_ = std::clamp(sprite.value(QStringLiteral("activeSet")).toInt(), 0,
                                  static_cast<int>(spriteSets_.size()) - 1);
    activeSprite_ = std::clamp(sprite.value(QStringLiteral("activeSprite")).toInt(),
                               0, spritePatternsPerSet() - 1);
    spriteEditorSlots_.clear();
    const QJsonArray savedSpriteEditors = sprite.value(QStringLiteral("editors")).toArray();
    for (int index = 0;
         index < std::min(spritePatternsPerSet(),
                          static_cast<int>(savedSpriteEditors.size())); ++index) {
        const QJsonObject savedEditor = savedSpriteEditors[index].toObject();
        const bool loaded = savedEditor.value(QStringLiteral("loaded")).toBool();
        const int setIndex = std::clamp(
            savedEditor.value(QStringLiteral("set")).toInt(), 0,
            static_cast<int>(spriteSets_.size()) - 1);
        const int spriteIndex = std::clamp(
            savedEditor.value(QStringLiteral("sprite")).toInt(), 0,
            spritePatternsPerSet() - 1);
        const auto& savedPlacement = spriteSets_[static_cast<std::size_t>(setIndex)]
                                         .placements[static_cast<std::size_t>(spriteIndex)];
        const int fallbackSize = usesPerSpriteSizeEditor()
            ? savedPlacement.size : spriteGlobalSize_;
        const int editorSize = normalizedSpriteSize(
            savedEditor.value(QStringLiteral("size")).toInt(fallbackSize));
        const bool duplicate = loaded && std::any_of(
            spriteEditorSlots_.begin(), spriteEditorSlots_.end(),
            [setIndex, spriteIndex, editorSize](const SpriteEditorSlot& editor) {
                return editor.loaded && editor.setIndex == setIndex
                    && editor.spriteIndex == spriteIndex
                    && editor.size == editorSize;
            });
        if (!duplicate) {
            spriteEditorSlots_.push_back(
                {loaded, setIndex, spriteIndex, editorSize});
        }
    }
    if (spriteEditorSlots_.empty()) {
        spriteEditorSlots_.push_back(
            {true, activeSpriteSet_, activeSprite_, activeSpriteSize_});
    }
    activeSpriteEditor_ = std::clamp(
        sprite.value(QStringLiteral("activeEditor")).toInt(), 0,
        static_cast<int>(spriteEditorSlots_.size()) - 1);
    const auto& activeSpriteEditor = spriteEditorSlots_[
        static_cast<std::size_t>(activeSpriteEditor_)];
    if (activeSpriteEditor.loaded) {
        activeSpriteSet_ = activeSpriteEditor.setIndex;
        activeSprite_ = activeSpriteEditor.spriteIndex;
        activeSpriteSize_ = activeSpriteEditor.size;
        if (!usesPerSpriteSizeEditor()) {
            spriteGlobalSize_ = activeSpriteEditor.size;
        }
    }
    syncSpriteDrawingColor();
    ++spriteRevision_;

    const QString sourcePath = root.value(QStringLiteral("source")).toObject()
                                   .value(QStringLiteral("path")).toString();
    if (!sourcePath.isEmpty()) {
        const QString resolved = QFileInfo(sourcePath).isAbsolute()
            ? sourcePath
            : QFileInfo(path).dir().absoluteFilePath(sourcePath);
        if (QFileInfo::exists(resolved)) imageInput_->openUrl(QUrl::fromLocalFile(resolved));
    }

    recipePath_ = QFileInfo(path).absoluteFilePath();
    setStatus(QStringLiteral("Loaded project %1").arg(QFileInfo(path).fileName()));
    emit projectChanged();
    return true;
}

void EditorProjectController::clearStatus()
{
    setStatus({});
}

std::optional<EditorProjectController::CharacterPattern>
EditorProjectController::characterPatternFromClipboard() const
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return std::nullopt;
    const QString text = clipboard->text();
    if (text.isEmpty() || text.size() > 65536) return std::nullopt;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("retrovdp.character-pattern")
        || (version != 1 && version != 2)) {
        return std::nullopt;
    }
    const QJsonObject size = root.value(QStringLiteral("size")).toObject();
    if (size.value(QStringLiteral("width")).toInt() != 8
        || size.value(QStringLiteral("height")).toInt() != 8) {
        return std::nullopt;
    }

    CharacterPattern pattern;
    const auto parseBytes = [](const QJsonValue& value,
                               std::array<std::uint8_t, 8>& destination) {
        const QJsonArray values = value.toArray();
        if (values.size() != 8) return false;
        for (int index = 0; index < 8; ++index) {
            bool valid = false;
            int byte = -1;
            if (values[index].isDouble()) {
                byte = values[index].toInt(-1);
                valid = byte >= 0 && byte <= 255;
            } else if (values[index].isString()) {
                QString encoded = values[index].toString().trimmed();
                if (encoded.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                    encoded.remove(0, 2);
                }
                byte = encoded.toInt(&valid, 16);
                valid = valid && byte >= 0 && byte <= 255;
            }
            if (!valid) return false;
            destination[static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>(byte);
        }
        return true;
    };
    if (!parseBytes(root.value(QStringLiteral("bitmap")), pattern.bitmap)
        || !parseBytes(root.value(QStringLiteral("colors")), pattern.colors)) {
        return std::nullopt;
    }
    if (root.value(QStringLiteral("indexedOverride")).toBool()) {
        const QJsonArray pixels = root.value(QStringLiteral("indexedPixels")).toArray();
        if (pixels.size() != 64) return std::nullopt;
        pattern.indexedOverride = true;
        for (int index = 0; index < 64; ++index) {
            const int value = pixels[index].toInt(-1);
            if (value < 0 || value > (1 << activeCharacterColorDepth()) - 1)
                return std::nullopt;
            pattern.indexedPixels[static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>(value);
        }
    }
    return pattern;
}

EditorProjectController::CharacterPattern
EditorProjectController::pannedCharacterPattern() const
{
    CharacterPattern result = characterPanOriginal_;
    if (usesIndexed4BppEditor()) {
        result.indexedOverride = true;
        result.indexedPixels.fill(0);
        for (int row = 0; row < 8; ++row) {
            const int sourceRow = row - characterPanY_;
            if (sourceRow < 0 || sourceRow >= 8) continue;
            for (int column = 0; column < 8; ++column) {
                const int sourceColumn = column - characterPanX_;
                if (sourceColumn < 0 || sourceColumn >= 8) continue;
                result.indexedPixels[static_cast<std::size_t>(row * 8 + column)] =
                    characterPanOriginal_.indexedOverride
                    ? characterPanOriginal_.indexedPixels[static_cast<std::size_t>(
                          sourceRow * 8 + sourceColumn)]
                    : ((characterPanOriginal_.bitmap[static_cast<std::size_t>(sourceRow)]
                        & (0x80U >> sourceColumn)) != 0
                           ? characterPanOriginal_.colors[static_cast<std::size_t>(sourceRow)] >> 4U
                           : characterPanOriginal_.colors[static_cast<std::size_t>(sourceRow)] & 0x0fU);
            }
        }
        return result;
    }
    for (int row = 0; row < 8; ++row) {
        const int sourceRow = row - characterPanY_;
        result.bitmap[static_cast<std::size_t>(row)] = 0;
        if (sourceRow < 0 || sourceRow >= 8) continue;
        std::uint8_t bits = characterPanOriginal_.bitmap[
            static_cast<std::size_t>(sourceRow)];
        if (characterPanX_ > 0) {
            bits = static_cast<std::uint8_t>(bits >> characterPanX_);
        } else if (characterPanX_ < 0) {
            bits = static_cast<std::uint8_t>(bits << -characterPanX_);
        }
        result.bitmap[static_cast<std::size_t>(row)] = bits;
        result.colors[static_cast<std::size_t>(row)] =
            characterPanOriginal_.colors[static_cast<std::size_t>(sourceRow)];
    }
    return result;
}

void EditorProjectController::recordCharacterEdit(
    int setIndex, int patternIndex,
    const CharacterPattern& before, const CharacterPattern& after)
{
    if (sameCharacterPattern(before, after)) return;
    recordCharacterEdits({CharacterPatternChange{setIndex, patternIndex,
                                                  before, after}});
}

void EditorProjectController::recordCharacterEdits(
    std::vector<CharacterPatternChange> changes)
{
    if (changes.empty()) return;
    constexpr std::size_t maximumHistory = 128;
    if (characterUndoHistory_.size() >= maximumHistory) {
        characterUndoHistory_.erase(characterUndoHistory_.begin());
    }
    characterUndoHistory_.push_back({std::move(changes)});
    characterRedoHistory_.clear();
}

void EditorProjectController::finishCharacterPan()
{
    if (!characterPanActive_) return;
    const CharacterPattern before = characterPanOriginal_;
    const CharacterPattern after = pannedCharacterPattern();
    characterPanActive_ = false;
    characterPanX_ = 0;
    characterPanY_ = 0;
    auto& pattern = characterSets_[static_cast<std::size_t>(characterPanSetIndex_)]
                        .patterns[static_cast<std::size_t>(
                            characterPanPatternIndex_)];
    pattern = after;
    recordCharacterEdit(characterPanSetIndex_, characterPanPatternIndex_,
                        before, after);
    ++characterRevision_;
    emit projectChanged();
}

EditorProjectController::SpriteSet EditorProjectController::makeSpriteSet(int ordinal) const
{
    SpriteSet set;
    set.name = QStringLiteral("Set %1").arg(ordinal);
    for (int index = 0; index < static_cast<int>(set.placements.size()); ++index) {
        auto& placement = set.placements[static_cast<std::size_t>(index)];
        placement.x = 8 + (index % 8) * 30;
        placement.y = 8 + (index / 8) * 42;
        placement.x16 = placement.x;
        placement.y16 = placement.y;
        if (usesIndexed4BppEditor()) placement.colorDepth = 4;
    }
    return set;
}

EditorProjectController::CharacterSet
EditorProjectController::makeCharacterSet(int ordinal) const
{
    CharacterSet set;
    set.name = QStringLiteral("Set %1").arg(ordinal);
    for (auto& pattern : set.patterns) pattern.colors.fill(defaultCharacterColor);
    return set;
}

void EditorProjectController::setStatus(QString message, QString error)
{
    statusMessage_ = std::move(message);
    errorMessage_ = std::move(error);
    emit statusChanged();
}
