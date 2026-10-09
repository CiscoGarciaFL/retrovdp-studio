#include "EditorProjectController.hpp"
#include "ImageInputController.hpp"

#include "retrovdp/core/TargetProfile.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cstdlib>

namespace {

bool sameSpritePattern(const auto& left, const auto& right)
{
    return left.baselinePixels == right.baselinePixels
        && left.f18aPixels == right.f18aPixels
        && left.f18aOverride == right.f18aOverride;
}

} // namespace

int EditorProjectController::activeSpriteColorDepth() const
{
    if (usesGameBoyEditor() || usesGameBoyColorEditor()) return 2;
    if (usesSuperNesEditor()) return 4;
    if (usesIndexed4BppEditor()) return 4;
    if (usesVicIIEditor()) return 2;
    if (spriteSets_.empty()) return 1;
    const auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                                .placements[static_cast<std::size_t>(activeSprite_)];
    return editScope_ == 1 ? placement.colorDepth : 1;
}

int EditorProjectController::spritePatternsPerSet() const
{
    if (usesGenesisMode5Editor() && imageInput_->targetWidth() == 256) return 64;
    return std::min<int>(
        retrovdp::core::targetProfile(
            static_cast<retrovdp::core::TargetProfileId>(activeTarget()))
            .sprites.patternsPerSet,
        128);
}

int EditorProjectController::normalizedSpriteSize(int value) const
{
    if (usesSuperNesEditor()) {
        int size = value >= 100 ? value / 100 : value;
        size = size <= 12 ? 8 : (size <= 24 ? 16 : (size <= 48 ? 32 : 64));
        return size * 100 + size;
    }
    if (usesGenesisMode5Editor()) {
        int width = value >= 100 ? value / 100 : value;
        int height = value >= 100 ? value % 100 : value;
        width = std::clamp(((width + 4) / 8) * 8, 8, 32);
        height = std::clamp(((height + 4) / 8) * 8, 8, 32);
        return width * 100 + height;
    }
    if (usesHuC6270Editor()) {
        int width = value >= 100 ? value / 100 : value;
        int height = value >= 100 ? value % 100 : value;
        width = width <= 24 ? 16 : 32;
        height = height <= 24 ? 16 : (height <= 48 ? 32 : 64);
        return width * 100 + height;
    }
    if (usesVicIIEditor()) return 2421;
    return value >= 16 ? 16 : 8;
}

int EditorProjectController::spritePatternWidth(int size) const
{
    size = normalizedSpriteSize(size);
    if (usesSmsMode4Editor()) return 8;
    if (usesCompoundSpriteEditor()) return size / 100;
    return size;
}

int EditorProjectController::spritePatternHeight(int size) const
{
    size = normalizedSpriteSize(size);
    return usesCompoundSpriteEditor() ? size % 100 : size;
}

bool EditorProjectController::canRotateSpritePattern() const
{
    return spritePatternWidth(activeSpriteSize_)
        == spritePatternHeight(activeSpriteSize_);
}

bool EditorProjectController::canPasteSpritePattern() const
{
    const auto data = spritePatternFromClipboard();
    if (!data.has_value()
        || data->width != spritePatternWidth(activeSpriteSize_)
        || data->height != spritePatternHeight(activeSpriteSize_)) return false;
    const int maximum = (editScope_ == 1 || usesIndexedSpriteEditor())
        ? (1 << activeSpriteColorDepth()) - 1 : 1;
    const int count = data->width * data->height;
    for (int index = 0; index < count; ++index) {
        if (data->pixels[static_cast<std::size_t>(index)] > maximum) return false;
    }
    return true;
}

void EditorProjectController::selectSpritePattern(int spriteIndex, int size)
{
    if (spriteSets_.empty()) return;
    spriteIndex = std::clamp(spriteIndex, 0, spritePatternsPerSet() - 1);
    size = normalizedSpriteSize(size);
    if (spritePanActive_
        && (spriteIndex != activeSprite_ || size != activeSpriteSize_)) {
        finishSpritePan();
    }

    bool changed = activeSprite_ != spriteIndex || activeSpriteSize_ != size;
    activeSprite_ = spriteIndex;
    activeSpriteSize_ = size;
    const bool perItemSize = usesPerSpriteSizeEditor();
    if (!perItemSize) {
        if (spriteGlobalSize_ != size) {
            spriteGlobalSize_ = size;
            changed = true;
        }
    } else {
        auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                              .placements[static_cast<std::size_t>(activeSprite_)];
        if (placement.size != size) {
            placement.size = size;
            changed = true;
        }
    }
    changed = assignActiveSpriteToEditor() || changed;
    syncSpriteDrawingColor();
    if (changed) emit projectChanged();
}

void EditorProjectController::setActiveSpriteSize(int value)
{
    value = normalizedSpriteSize(value);
    if (spritePanActive_ && value != activeSpriteSize_) finishSpritePan();
    bool changed = activeSpriteSize_ != value;
    activeSpriteSize_ = value;
    const bool perItemSize = usesPerSpriteSizeEditor();
    if (!perItemSize) {
        if (spriteGlobalSize_ != value) {
            spriteGlobalSize_ = value;
            changed = true;
        }
    } else if (!spriteSets_.empty()) {
        auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                              .placements[static_cast<std::size_t>(activeSprite_)];
        if (placement.size != value) {
            placement.size = value;
            changed = true;
        }
    }
    changed = assignActiveSpriteToEditor() || changed;
    if (changed) emit projectChanged();
}

void EditorProjectController::setSpriteGlobalSize(int value)
{
    value = normalizedSpriteSize(value);
    if (spriteGlobalSize_ == value) return;
    if (spritePanActive_) finishSpritePan();
    spriteGlobalSize_ = value;
    const bool perItemSize = usesPerSpriteSizeEditor();
    if (!perItemSize) {
        activeSpriteSize_ = value;
        activateSpriteEditorBank(value);
        syncSpriteDrawingColor();
    }
    emit projectChanged();
}

void EditorProjectController::setSpriteDrawingColorIndex(int value)
{
    if (spriteSets_.empty()) return;
    auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                          .placements[static_cast<std::size_t>(activeSprite_)];
    const int maximum = usesIndexedSpriteEditor()
        ? (1 << activeSpriteColorDepth()) - 1
        : (editScope_ == 1 ? (1 << activeSpriteColorDepth()) - 1 : 15);
    value = std::clamp(value, 1, maximum);
    bool changed = spriteDrawingColorIndex_ != value;
    spriteDrawingColorIndex_ = value;
    if (editScope_ == 0 && !usesIndexedSpriteEditor() && placement.color != value) {
        placement.color = value;
        changed = true;
    }
    if (changed) emit projectChanged();
}

int EditorProjectController::activeSpritePaletteBank() const
{
    if (spriteSets_.empty()) return 0;
    return spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
        .placements[static_cast<std::size_t>(activeSprite_)].palette;
}

void EditorProjectController::setActiveSpritePaletteBank(int value)
{
    if (spriteSets_.empty()) return;
    value = std::clamp(value, 0, usesGenesisMode5Editor() ? 3
                              : (usesHuC6270Editor() ? 15
                              : (usesGameBoyEditor() ? 1
                              : (usesGameBoyColorEditor() || usesSuperNesEditor() ? 7 : 0))));
    auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                          .placements[static_cast<std::size_t>(activeSprite_)];
    if (placement.palette == value) return;
    placement.palette = value;
    ++spriteRevision_;
    emit projectChanged();
}

bool EditorProjectController::activeSpritePriority() const
{
    return !spriteSets_.empty()
        && spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
               .placements[static_cast<std::size_t>(activeSprite_)].priority;
}

void EditorProjectController::setActiveSpritePriority(bool value)
{
    if ((!usesGenesisMode5Editor() && !usesGameBoyEditor()
         && !usesGameBoyColorEditor() && !usesSuperNesEditor())
        || spriteSets_.empty()) return;
    auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                          .placements[static_cast<std::size_t>(activeSprite_)];
    if (placement.priority == value) return;
    placement.priority = value;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::setActiveSpriteColorDepth(int value)
{
    if (editScope_ != 1 || spriteSets_.empty()) return;
    value = usesGameBoyEditor() || usesGameBoyColorEditor() ? 2
        : usesIndexed4BppEditor() ? 4 : std::clamp(value, 1, 3);
    auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                          .placements[static_cast<std::size_t>(activeSprite_)];
    if (placement.colorDepth == value) return;
    placement.colorDepth = value;
    syncSpriteDrawingColor();
    emit projectChanged();
}

void EditorProjectController::syncSpriteDrawingColor()
{
    if (spriteSets_.empty()) {
        spriteDrawingColorIndex_ = 15;
        return;
    }
    const auto& placement = spriteSets_[static_cast<std::size_t>(activeSpriteSet_)]
                                .placements[static_cast<std::size_t>(activeSprite_)];
    if (editScope_ == 0 && !usesIndexedSpriteEditor()) {
        spriteDrawingColorIndex_ = placement.color;
        return;
    }
    spriteDrawingColorIndex_ = std::clamp(
        spriteDrawingColorIndex_, 1, (1 << activeSpriteColorDepth()) - 1);
}

bool EditorProjectController::activateSpriteEditorBank(int size)
{
    size = normalizedSpriteSize(size);
    if (activeSpriteEditor_ >= 0
        && activeSpriteEditor_ < static_cast<int>(spriteEditorSlots_.size())
        && spriteEditorSlots_[static_cast<std::size_t>(activeSpriteEditor_)].size
            == size) {
        return false;
    }

    int matchingIndex = -1;
    for (int index = 0; index < static_cast<int>(spriteEditorSlots_.size()); ++index) {
        const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(index)];
        if (slot.size != size) continue;
        matchingIndex = index;
        if (slot.loaded) break;
    }
    if (matchingIndex < 0) {
        const std::size_t maximumEditors = static_cast<std::size_t>(
            spritePatternsPerSet());
        if (spriteEditorSlots_.size() >= maximumEditors) {
            setStatus({}, QStringLiteral("A sprite tray can contain up to %1 unique sprite editors.")
                              .arg(spritePatternsPerSet()));
            return false;
        }
        spriteEditorSlots_.push_back(
            {false, activeSpriteSet_, activeSprite_, size});
        matchingIndex = static_cast<int>(spriteEditorSlots_.size()) - 1;
    }

    activeSpriteEditor_ = matchingIndex;
    activeSpriteSize_ = size;
    const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(matchingIndex)];
    if (slot.loaded) {
        activeSpriteSet_ = slot.setIndex;
        activeSprite_ = slot.spriteIndex;
    }
    return true;
}

bool EditorProjectController::assignActiveSpriteToEditor()
{
    if (spriteEditorSlots_.empty()) {
        spriteEditorSlots_.push_back(
            {false, activeSpriteSet_, activeSprite_, activeSpriteSize_});
        activeSpriteEditor_ = 0;
    }
    for (int index = 0; index < static_cast<int>(spriteEditorSlots_.size()); ++index) {
        const auto& slot = spriteEditorSlots_[static_cast<std::size_t>(index)];
        if (slot.loaded && slot.setIndex == activeSpriteSet_
            && slot.spriteIndex == activeSprite_
            && slot.size == activeSpriteSize_) {
            activeSpriteEditor_ = index;
            return true;
        }
    }

    int targetIndex = -1;
    if (activeSpriteEditor_ >= 0
        && activeSpriteEditor_ < static_cast<int>(spriteEditorSlots_.size())) {
        const auto& activeEditor = spriteEditorSlots_[
            static_cast<std::size_t>(activeSpriteEditor_)];
        if (!activeEditor.loaded || activeEditor.size == activeSpriteSize_) {
            targetIndex = activeSpriteEditor_;
        }
    } else {
        for (int index = 0; index < static_cast<int>(spriteEditorSlots_.size()); ++index) {
            const auto& candidate = spriteEditorSlots_[static_cast<std::size_t>(index)];
            if (!candidate.loaded && candidate.size == activeSpriteSize_) {
                targetIndex = index;
                break;
            }
        }
    }
    if (targetIndex < 0) {
        const std::size_t maximumEditors = static_cast<std::size_t>(
            spritePatternsPerSet());
        if (spriteEditorSlots_.size() >= maximumEditors) return false;
        spriteEditorSlots_.push_back(
            {false, activeSpriteSet_, activeSprite_, activeSpriteSize_});
        targetIndex = static_cast<int>(spriteEditorSlots_.size()) - 1;
    }
    activeSpriteEditor_ = targetIndex;
    auto& slot = spriteEditorSlots_[static_cast<std::size_t>(targetIndex)];
    slot.loaded = true;
    slot.setIndex = activeSpriteSet_;
    slot.spriteIndex = activeSprite_;
    slot.size = activeSpriteSize_;
    return true;
}

void EditorProjectController::setSpritePanActive(bool value)
{
    if (spritePanActive_ == value) return;
    if (!value) {
        finishSpritePan();
        return;
    }
    if (spritePlacementMode_ || spriteSets_.empty()) return;
    endSpriteEdit();
    spritePanSetIndex_ = activeSpriteSet_;
    spritePanSpriteIndex_ = activeSprite_;
    spritePanSize_ = activeSpriteSize_;
    spritePanEnhanced_ = editScope_ == 1 || usesIndexedSpriteEditor();
    spritePanOriginal_ = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    spritePanX_ = 0;
    spritePanY_ = 0;
    spritePanActive_ = true;
    ++spriteRevision_;
    emit projectChanged();
}

QVariantList EditorProjectController::spritePatternPixels(int setIndex,
                                                          int spriteIndex,
                                                          int size) const
{
    QVariantList result;
    size = normalizedSpriteSize(size);
    if (setIndex < 0 || setIndex >= static_cast<int>(spriteSets_.size())
        || spriteIndex < 0 || spriteIndex >= spritePatternsPerSet()) {
        return result;
    }
    std::vector<std::uint8_t> panPixels(2048);
    const std::vector<std::uint8_t>* pixels =
        &visibleSpritePixels(spritePattern(setIndex, spriteIndex, size));
    if (spritePanActive_ && setIndex == spritePanSetIndex_
        && spriteIndex == spritePanSpriteIndex_ && size == spritePanSize_) {
        panPixels = pannedSpritePixels();
        pixels = &panPixels;
    }
    const int count = spritePatternWidth(size) * spritePatternHeight(size);
    result.reserve(count);
    for (int index = 0; index < count; ++index) {
        result.push_back((*pixels)[static_cast<std::size_t>(index)]);
    }
    return result;
}

void EditorProjectController::paintSpritePixel(int setIndex,
                                               int spriteIndex,
                                               int size,
                                               int row,
                                               int column,
                                               bool foreground)
{
    size = normalizedSpriteSize(size);
    if (setIndex < 0 || setIndex >= static_cast<int>(spriteSets_.size())
        || spriteIndex < 0 || spriteIndex >= spritePatternsPerSet()
        || row < 0 || row >= spritePatternHeight(size)
        || column < 0 || column >= spritePatternWidth(size)
        || spritePanActive_) {
        return;
    }
    if (spriteEditActive_
        && (spriteEditSetIndex_ != setIndex
            || spriteEditSpriteIndex_ != spriteIndex
            || spriteEditSize_ != size)) {
        endSpriteEdit();
    }
    const bool standalone = !spriteEditActive_;
    if (standalone) beginSpriteEdit(setIndex, spriteIndex, size);
    auto& pattern = spritePattern(setIndex, spriteIndex, size);
    std::vector<std::uint8_t>* pixels = &pattern.baselinePixels;
    int value = foreground ? 1 : 0;
    if (editScope_ == 1 || usesIndexedSpriteEditor()) {
        ensureF18aSpriteOverride(pattern);
        pixels = &pattern.f18aPixels;
        value = foreground ? spriteDrawingColorIndex_ : 0;
    }
    auto& pixel = (*pixels)[static_cast<std::size_t>(
        row * spritePatternWidth(size) + column)];
    if (pixel != value) {
        pixel = static_cast<std::uint8_t>(value);
        ++spriteRevision_;
        emit projectChanged();
    }
    if (standalone) endSpriteEdit();
}

void EditorProjectController::drawSpriteLine(int setIndex,
                                             int spriteIndex,
                                             int size,
                                             int fromRow,
                                             int fromColumn,
                                             int toRow,
                                             int toColumn,
                                             bool foreground)
{
    size = normalizedSpriteSize(size);
    if (setIndex < 0 || setIndex >= static_cast<int>(spriteSets_.size())
        || spriteIndex < 0 || spriteIndex >= spritePatternsPerSet()
        || spritePanActive_) {
        return;
    }
    fromRow = std::clamp(fromRow, 0, spritePatternHeight(size) - 1);
    fromColumn = std::clamp(fromColumn, 0, spritePatternWidth(size) - 1);
    toRow = std::clamp(toRow, 0, spritePatternHeight(size) - 1);
    toColumn = std::clamp(toColumn, 0, spritePatternWidth(size) - 1);
    const bool groupedEdit = spriteEditActive_
        && spriteEditSetIndex_ == setIndex
        && spriteEditSpriteIndex_ == spriteIndex
        && spriteEditSize_ == size;
    beginSpriteEdit(setIndex, spriteIndex, size);

    int x = fromColumn;
    int y = fromRow;
    const int deltaX = std::abs(toColumn - fromColumn);
    const int stepX = fromColumn < toColumn ? 1 : -1;
    const int deltaY = -std::abs(toRow - fromRow);
    const int stepY = fromRow < toRow ? 1 : -1;
    int error = deltaX + deltaY;
    while (true) {
        paintSpritePixel(setIndex, spriteIndex, size, y, x, foreground);
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
    if (!groupedEdit) endSpriteEdit();
}

void EditorProjectController::beginSpriteEdit(int setIndex, int spriteIndex, int size)
{
    size = normalizedSpriteSize(size);
    if (setIndex < 0 || setIndex >= static_cast<int>(spriteSets_.size())
        || spriteIndex < 0 || spriteIndex >= spritePatternsPerSet()
        || spritePanActive_) {
        return;
    }
    if (spriteEditActive_) {
        if (spriteEditSetIndex_ == setIndex
            && spriteEditSpriteIndex_ == spriteIndex
            && spriteEditSize_ == size) {
            return;
        }
        endSpriteEdit();
    }
    spriteEditActive_ = true;
    spriteEditSetIndex_ = setIndex;
    spriteEditSpriteIndex_ = spriteIndex;
    spriteEditSize_ = size;
    spriteEditBefore_ = spritePattern(setIndex, spriteIndex, size);
}

void EditorProjectController::endSpriteEdit()
{
    if (!spriteEditActive_) return;
    const int setIndex = spriteEditSetIndex_;
    const int spriteIndex = spriteEditSpriteIndex_;
    const int size = spriteEditSize_;
    spriteEditActive_ = false;
    recordSpriteEdit(setIndex, spriteIndex, size, spriteEditBefore_,
                     spritePattern(setIndex, spriteIndex, size));
    emit projectChanged();
}

void EditorProjectController::rotateActiveSpritePattern()
{
    if (!canRotateSpritePattern()) {
        setStatus({}, QStringLiteral(
            "Rotation is unavailable when the active hardware sprite is rectangular."));
        return;
    }
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();
    auto& pattern = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    const SpritePattern before = pattern;
    const bool indexed = editScope_ == 1 || usesIndexedSpriteEditor();
    if (indexed) ensureF18aSpriteOverride(pattern);
    auto& pixels = indexed ? pattern.f18aPixels : pattern.baselinePixels;
    const auto source = pixels;
    const int size = activeSpriteSize_;
    const int width = spritePatternWidth(size);
    const int height = spritePatternHeight(size);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            pixels[static_cast<std::size_t>(column * width + (width - 1 - row))] =
                source[static_cast<std::size_t>(row * width + column)];
        }
    }
    recordSpriteEdit(activeSpriteSet_, activeSprite_, size, before, pattern);
    if (sameSpritePattern(before, pattern)) return;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::mirrorActiveSpritePattern()
{
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();
    auto& pattern = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    const SpritePattern before = pattern;
    const bool indexed = editScope_ == 1 || usesIndexedSpriteEditor();
    if (indexed) ensureF18aSpriteOverride(pattern);
    auto& pixels = indexed ? pattern.f18aPixels : pattern.baselinePixels;
    const int size = activeSpriteSize_;
    const int width = spritePatternWidth(size);
    const int height = spritePatternHeight(size);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width / 2; ++column) {
            std::swap(pixels[static_cast<std::size_t>(row * width + column)],
                      pixels[static_cast<std::size_t>(row * width + width - 1 - column)]);
        }
    }
    recordSpriteEdit(activeSpriteSet_, activeSprite_, size, before, pattern);
    if (sameSpritePattern(before, pattern)) return;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::flipActiveSpritePattern()
{
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();
    auto& pattern = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    const SpritePattern before = pattern;
    const bool indexed = editScope_ == 1 || usesIndexedSpriteEditor();
    if (indexed) ensureF18aSpriteOverride(pattern);
    auto& pixels = indexed ? pattern.f18aPixels : pattern.baselinePixels;
    const int size = activeSpriteSize_;
    const int width = spritePatternWidth(size);
    const int height = spritePatternHeight(size);
    for (int row = 0; row < height / 2; ++row) {
        for (int column = 0; column < width; ++column) {
            std::swap(pixels[static_cast<std::size_t>(row * width + column)],
                      pixels[static_cast<std::size_t>((height - 1 - row) * width + column)]);
        }
    }
    recordSpriteEdit(activeSpriteSet_, activeSprite_, size, before, pattern);
    if (sameSpritePattern(before, pattern)) return;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::blankActiveSpritePattern()
{
    if (spritePanActive_) finishSpritePan();
    endSpriteEdit();
    auto& pattern = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    const SpritePattern before = pattern;
    const bool indexed = editScope_ == 1 || usesIndexedSpriteEditor();
    if (indexed) ensureF18aSpriteOverride(pattern);
    auto& pixels = indexed ? pattern.f18aPixels : pattern.baselinePixels;
    const int count = spritePatternWidth(activeSpriteSize_)
        * spritePatternHeight(activeSpriteSize_);
    std::fill_n(pixels.begin(), count, 0);
    recordSpriteEdit(activeSpriteSet_, activeSprite_, activeSpriteSize_, before, pattern);
    if (sameSpritePattern(before, pattern)) return;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::nudgeSpritePan(int horizontal, int vertical)
{
    if (!spritePanActive_) return;
    const int width = spritePatternWidth(spritePanSize_);
    const int height = spritePatternHeight(spritePanSize_);
    const int nextX = std::clamp(spritePanX_ + horizontal, -width, width);
    const int nextY = std::clamp(spritePanY_ + vertical, -height, height);
    if (nextX == spritePanX_ && nextY == spritePanY_) return;
    spritePanX_ = nextX;
    spritePanY_ = nextY;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::centerSpritePan()
{
    if (!spritePanActive_ || (spritePanX_ == 0 && spritePanY_ == 0)) return;
    spritePanX_ = 0;
    spritePanY_ = 0;
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::undoSpriteEdit()
{
    if (spritePanActive_) return;
    endSpriteEdit();
    if (spriteUndoHistory_.empty()) return;
    const SpriteHistoryEntry entry = spriteUndoHistory_.back();
    spriteUndoHistory_.pop_back();
    spritePattern(entry.setIndex, entry.spriteIndex, entry.size) = entry.before;
    spriteRedoHistory_.push_back(entry);
    ++spriteRevision_;
    emit projectChanged();
}

void EditorProjectController::redoSpriteEdit()
{
    if (spritePanActive_) return;
    endSpriteEdit();
    if (spriteRedoHistory_.empty()) return;
    const SpriteHistoryEntry entry = spriteRedoHistory_.back();
    spriteRedoHistory_.pop_back();
    spritePattern(entry.setIndex, entry.spriteIndex, entry.size) = entry.after;
    spriteUndoHistory_.push_back(entry);
    ++spriteRevision_;
    emit projectChanged();
}

bool EditorProjectController::copyActiveSpritePattern()
{
    if (spritePanActive_) return false;
    endSpriteEdit();
    const auto& pattern = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    const auto& pixels = visibleSpritePixels(pattern);
    const int width = spritePatternWidth(activeSpriteSize_);
    const int height = spritePatternHeight(activeSpriteSize_);
    QJsonArray rows;
    for (int row = 0; row < height; ++row) {
        QJsonArray values;
        for (int column = 0; column < width; ++column) {
            values.push_back(pixels[static_cast<std::size_t>(
                row * width + column)]);
        }
        rows.push_back(values);
    }
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("retrovdp.sprite-pattern")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("size"), activeSpriteSize_},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("scope"), editScope_ == 1
                                      ? (usesSmsMode4Editor()
                                             ? QStringLiteral("sms-mode4")
                                             : usesGenesisMode5Editor()
                                                 ? QStringLiteral("genesis-mode5")
                                             : QStringLiteral("f18a-override"))
                                      : QStringLiteral("tms9918a-baseline")},
        {QStringLiteral("source"),
         QJsonObject{{QStringLiteral("set"), activeSpriteSet_},
                     {QStringLiteral("sprite"), activeSprite_}}},
        {QStringLiteral("colorDepth"), activeSpriteColorDepth()},
        {QStringLiteral("pixels"), rows},
    };
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return false;
    clipboard->setText(QString::fromUtf8(QJsonDocument(root).toJson(
        QJsonDocument::Indented)));
    setStatus(QStringLiteral("Copied %1x%2 sprite %3 to the clipboard.")
                  .arg(width)
                  .arg(height)
                  .arg(activeSprite_));
    return true;
}

bool EditorProjectController::pasteActiveSpritePattern()
{
    if (spritePanActive_) return false;
    endSpriteEdit();
    const auto data = spritePatternFromClipboard();
    if (!data.has_value()
        || data->width != spritePatternWidth(activeSpriteSize_)
        || data->height != spritePatternHeight(activeSpriteSize_)) {
        setStatus({}, QStringLiteral("Clipboard sprite size does not match the active editor."));
        return false;
    }
    const int maximum = (editScope_ == 1 || usesIndexedSpriteEditor())
        ? (1 << activeSpriteColorDepth()) - 1 : 1;
    const int count = data->width * data->height;
    for (int index = 0; index < count; ++index) {
        if (data->pixels[static_cast<std::size_t>(index)] > maximum) {
            const QString targetName = activeTargetInfo()
                                           .value(QStringLiteral("name"))
                                           .toString();
            setStatus({}, !usesIndexedSpriteEditor() && editScope_ == 0
                ? QStringLiteral("Enhanced-color sprite pixels are not supported by %1.")
                      .arg(targetName)
                : QStringLiteral("Clipboard pixel indexes exceed the active %1 color depth.")
                      .arg(targetName));
            return false;
        }
    }
    auto& pattern = spritePattern(activeSpriteSet_, activeSprite_, activeSpriteSize_);
    const SpritePattern before = pattern;
    if (editScope_ == 1 || usesIndexedSpriteEditor()) {
        ensureF18aSpriteOverride(pattern);
        pattern.f18aPixels = data->pixels;
    } else {
        pattern.baselinePixels = data->pixels;
    }
    recordSpriteEdit(activeSpriteSet_, activeSprite_, activeSpriteSize_, before, pattern);
    if (!sameSpritePattern(before, pattern)) {
        ++spriteRevision_;
        emit projectChanged();
    }
    setStatus(QStringLiteral("Pasted clipboard data into sprite %1.").arg(activeSprite_));
    return true;
}

EditorProjectController::SpritePattern&
EditorProjectController::spritePattern(int setIndex, int spriteIndex, int size)
{
    auto& set = spriteSets_[static_cast<std::size_t>(setIndex)];
    if (usesCompoundSpriteEditor())
        return set.patternsGenesis[static_cast<std::size_t>(spriteIndex)];
    return normalizedSpriteSize(size) == 16
        ? set.patterns16[static_cast<std::size_t>(spriteIndex)]
        : set.patterns8[static_cast<std::size_t>(spriteIndex)];
}

const EditorProjectController::SpritePattern&
EditorProjectController::spritePattern(int setIndex, int spriteIndex, int size) const
{
    const auto& set = spriteSets_[static_cast<std::size_t>(setIndex)];
    if (usesCompoundSpriteEditor())
        return set.patternsGenesis[static_cast<std::size_t>(spriteIndex)];
    return normalizedSpriteSize(size) == 16
        ? set.patterns16[static_cast<std::size_t>(spriteIndex)]
        : set.patterns8[static_cast<std::size_t>(spriteIndex)];
}

const std::vector<std::uint8_t>&
EditorProjectController::visibleSpritePixels(const SpritePattern& pattern) const
{
    return (editScope_ == 1 || usesIndexedSpriteEditor()) && pattern.f18aOverride
        ? pattern.f18aPixels : pattern.baselinePixels;
}

std::vector<std::uint8_t> EditorProjectController::pannedSpritePixels() const
{
    const auto& source = spritePanEnhanced_ && spritePanOriginal_.f18aOverride
        ? spritePanOriginal_.f18aPixels : spritePanOriginal_.baselinePixels;
    std::vector<std::uint8_t> result(2048);
    const int width = spritePatternWidth(spritePanSize_);
    const int height = spritePatternHeight(spritePanSize_);
    for (int row = 0; row < height; ++row) {
        const int sourceRow = row - spritePanY_;
        if (sourceRow < 0 || sourceRow >= height) continue;
        for (int column = 0; column < width; ++column) {
            const int sourceColumn = column - spritePanX_;
            if (sourceColumn < 0 || sourceColumn >= width) continue;
            result[static_cast<std::size_t>(row * width + column)] =
                source[static_cast<std::size_t>(
                    sourceRow * width + sourceColumn)];
        }
    }
    return result;
}

std::optional<EditorProjectController::SpriteClipboardData>
EditorProjectController::spritePatternFromClipboard() const
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return std::nullopt;
    const QString text = clipboard->text();
    if (text.isEmpty() || text.size() > 262144) return std::nullopt;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const int version = root.value(QStringLiteral("version")).toInt();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("retrovdp.sprite-pattern")
        || (version != 1 && version != 2)) {
        return std::nullopt;
    }
    SpriteClipboardData data;
    data.size = normalizedSpriteSize(root.value(QStringLiteral("size")).toInt());
    if (root.value(QStringLiteral("size")).toInt() != data.size) return std::nullopt;
    data.width = root.contains(QStringLiteral("width"))
        ? root.value(QStringLiteral("width")).toInt() : data.size;
    data.height = root.contains(QStringLiteral("height"))
        ? root.value(QStringLiteral("height")).toInt() : data.size;
    if (data.width < 8 || data.width > 32 || data.width % 8 != 0
        || data.height < 8 || data.height > 64)
        return std::nullopt;
    data.enhanced = root.value(QStringLiteral("scope")).toString()
        == QStringLiteral("f18a-override");
    data.colorDepth = std::clamp(
        root.value(QStringLiteral("colorDepth")).toInt(1), 1, 4);
    const QJsonArray rows = root.value(QStringLiteral("pixels")).toArray();
    if (rows.size() != data.height) return std::nullopt;
    for (int row = 0; row < data.height; ++row) {
        const QJsonArray values = rows[row].toArray();
        if (values.size() != data.width) return std::nullopt;
        for (int column = 0; column < data.width; ++column) {
            const int value = values[column].toInt(-1);
            if (value < 0 || value > (1 << data.colorDepth) - 1) {
                return std::nullopt;
            }
            data.pixels[static_cast<std::size_t>(row * data.width + column)] =
                static_cast<std::uint8_t>(value);
        }
    }
    return data;
}

void EditorProjectController::ensureF18aSpriteOverride(SpritePattern& pattern)
{
    if (pattern.f18aOverride) return;
    pattern.f18aPixels = pattern.baselinePixels;
    pattern.f18aOverride = true;
}

void EditorProjectController::recordSpriteEdit(int setIndex, int spriteIndex, int size,
                                               const SpritePattern& before,
                                               const SpritePattern& after)
{
    if (sameSpritePattern(before, after)) return;
    constexpr std::size_t maximumHistory = 128;
    if (spriteUndoHistory_.size() >= maximumHistory) {
        spriteUndoHistory_.erase(spriteUndoHistory_.begin());
    }
    spriteUndoHistory_.push_back({setIndex, spriteIndex, size, before, after});
    spriteRedoHistory_.clear();
}

void EditorProjectController::finishSpritePan()
{
    if (!spritePanActive_) return;
    const auto shifted = pannedSpritePixels();
    const SpritePattern before = spritePanOriginal_;
    spritePanActive_ = false;
    spritePanX_ = 0;
    spritePanY_ = 0;
    auto& pattern = spritePattern(spritePanSetIndex_, spritePanSpriteIndex_,
                                  spritePanSize_);
    pattern = before;
    if (spritePanEnhanced_) {
        ensureF18aSpriteOverride(pattern);
        pattern.f18aPixels = shifted;
    } else {
        pattern.baselinePixels = shifted;
    }
    recordSpriteEdit(spritePanSetIndex_, spritePanSpriteIndex_, spritePanSize_,
                     before, pattern);
    ++spriteRevision_;
    emit projectChanged();
}
