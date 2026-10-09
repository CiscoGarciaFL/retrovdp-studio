#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class ImageInputController;

class EditorProjectController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY projectChanged)
    Q_PROPERTY(bool tms9918aEnabled READ tms9918aEnabled WRITE setTms9918aEnabled NOTIFY projectChanged)
    Q_PROPERTY(int workspaceMode READ workspaceMode WRITE setWorkspaceMode NOTIFY projectChanged)
    Q_PROPERTY(bool f18aEnabled READ f18aEnabled WRITE setF18aEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool v9938Enabled READ v9938Enabled NOTIFY projectChanged)
    Q_PROPERTY(bool v9958Enabled READ v9958Enabled NOTIFY projectChanged)
    Q_PROPERTY(bool segaSmsEnabled READ segaSmsEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool segaGenesisEnabled READ segaGenesisEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool huc6270Enabled READ huc6270Enabled NOTIFY projectChanged)
    Q_PROPERTY(bool vicIiEnabled READ vicIiEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool vicEnabled READ vicEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool gameBoyEnabled READ gameBoyEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool gameBoyColorEnabled READ gameBoyColorEnabled NOTIFY projectChanged)
    Q_PROPERTY(bool superNesEnabled READ superNesEnabled NOTIFY projectChanged)
    Q_PROPERTY(QStringList plannedTargetIds READ plannedTargetIds NOTIFY projectChanged)
    Q_PROPERTY(int activeTarget READ activeTarget WRITE setActiveTarget NOTIFY projectChanged)
    Q_PROPERTY(QVariantList supportedTargets READ supportedTargets NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap activeTargetInfo READ activeTargetInfo NOTIFY projectChanged)
    Q_PROPERTY(bool screenImageModeAvailable READ screenImageModeAvailable NOTIFY projectChanged)
    Q_PROPERTY(bool characterModeAvailable READ characterModeAvailable NOTIFY projectChanged)
    Q_PROPERTY(bool spriteModeAvailable READ spriteModeAvailable NOTIFY projectChanged)
    Q_PROPERTY(int previewTarget READ previewTarget WRITE setPreviewTarget NOTIFY projectChanged)
    Q_PROPERTY(int editScope READ editScope WRITE setEditScope NOTIFY projectChanged)
    Q_PROPERTY(int activeCharacterSet READ activeCharacterSet WRITE setActiveCharacterSet NOTIFY projectChanged)
    Q_PROPERTY(int activeCharacterPattern READ activeCharacterPattern WRITE setActiveCharacterPattern NOTIFY projectChanged)
    Q_PROPERTY(QVariantList characterSetNames READ characterSetNames NOTIFY projectChanged)
    Q_PROPERTY(QVariantList characterPaletteColors READ characterPaletteColors NOTIFY projectChanged)
    Q_PROPERTY(QVariantList spritePaletteColors READ spritePaletteColors NOTIFY projectChanged)
    Q_PROPERTY(int characterForegroundColorIndex READ characterForegroundColorIndex WRITE setCharacterForegroundColorIndex NOTIFY projectChanged)
    Q_PROPERTY(int characterBackgroundColorIndex READ characterBackgroundColorIndex WRITE setCharacterBackgroundColorIndex NOTIFY projectChanged)
    Q_PROPERTY(int characterPaletteBank READ characterPaletteBank WRITE setCharacterPaletteBank NOTIFY projectChanged)
    Q_PROPERTY(int activeCharacterPlane READ activeCharacterPlane WRITE setActiveCharacterPlane NOTIFY projectChanged)
    Q_PROPERTY(bool genesisCompositePreview READ genesisCompositePreview WRITE setGenesisCompositePreview NOTIFY projectChanged)
    Q_PROPERTY(int activeCharacterTilePalette READ activeCharacterTilePalette WRITE setActiveCharacterTilePalette NOTIFY projectChanged)
    Q_PROPERTY(bool activeCharacterTileFlipX READ activeCharacterTileFlipX WRITE setActiveCharacterTileFlipX NOTIFY projectChanged)
    Q_PROPERTY(bool activeCharacterTileFlipY READ activeCharacterTileFlipY WRITE setActiveCharacterTileFlipY NOTIFY projectChanged)
    Q_PROPERTY(bool activeCharacterTilePriority READ activeCharacterTilePriority WRITE setActiveCharacterTilePriority NOTIFY projectChanged)
    Q_PROPERTY(int characterRevision READ characterRevision NOTIFY projectChanged)
    Q_PROPERTY(QVariantList characterEditorSlots READ characterEditorSlots NOTIFY projectChanged)
    Q_PROPERTY(int activeCharacterEditor READ activeCharacterEditor WRITE setActiveCharacterEditor NOTIFY projectChanged)
    Q_PROPERTY(bool characterTilingMode READ characterTilingMode WRITE setCharacterTilingMode NOTIFY projectChanged)
    Q_PROPERTY(bool characterPanActive READ characterPanActive WRITE setCharacterPanActive NOTIFY projectChanged)
    Q_PROPERTY(int characterPanX READ characterPanX NOTIFY projectChanged)
    Q_PROPERTY(int characterPanY READ characterPanY NOTIFY projectChanged)
    Q_PROPERTY(bool canUndoCharacter READ canUndoCharacter NOTIFY projectChanged)
    Q_PROPERTY(bool canRedoCharacter READ canRedoCharacter NOTIFY projectChanged)
    Q_PROPERTY(bool canPasteCharacterPattern READ canPasteCharacterPattern NOTIFY projectChanged)
    Q_PROPERTY(int characterPatternWidth READ characterPatternWidth NOTIFY projectChanged)
    Q_PROPERTY(int characterPatternHeight READ characterPatternHeight NOTIFY projectChanged)
    Q_PROPERTY(int characterPatternsPerSet READ characterPatternsPerSet NOTIFY projectChanged)
    Q_PROPERTY(int characterSetCount READ characterSetCount NOTIFY projectChanged)
    Q_PROPERTY(int characterMapColumns READ characterMapColumns NOTIFY projectChanged)
    Q_PROPERTY(int characterMapRows READ characterMapRows NOTIFY projectChanged)
    Q_PROPERTY(bool screenImageSelectionCharacterAligned READ
                   screenImageSelectionCharacterAligned NOTIFY projectChanged)
    Q_PROPERTY(int activeSpriteSet READ activeSpriteSet WRITE setActiveSpriteSet NOTIFY projectChanged)
    Q_PROPERTY(int activeSprite READ activeSprite WRITE setActiveSprite NOTIFY projectChanged)
    Q_PROPERTY(int activeSpriteSize READ activeSpriteSize WRITE setActiveSpriteSize NOTIFY projectChanged)
    Q_PROPERTY(int spriteGlobalSize READ spriteGlobalSize WRITE setSpriteGlobalSize NOTIFY projectChanged)
    Q_PROPERTY(int spriteDrawingColorIndex READ spriteDrawingColorIndex WRITE setSpriteDrawingColorIndex NOTIFY projectChanged)
    Q_PROPERTY(int activeSpritePaletteBank READ activeSpritePaletteBank WRITE setActiveSpritePaletteBank NOTIFY projectChanged)
    Q_PROPERTY(bool activeSpritePriority READ activeSpritePriority WRITE setActiveSpritePriority NOTIFY projectChanged)
    Q_PROPERTY(int activeSpriteColorDepth READ activeSpriteColorDepth WRITE setActiveSpriteColorDepth NOTIFY projectChanged)
    Q_PROPERTY(int spriteRevision READ spriteRevision NOTIFY projectChanged)
    Q_PROPERTY(QVariantList spriteSetNames READ spriteSetNames NOTIFY projectChanged)
    Q_PROPERTY(QVariantList activeSpritePlacements READ activeSpritePlacements NOTIFY projectChanged)
    Q_PROPERTY(QVariantList spriteEditorSlots READ spriteEditorSlots NOTIFY projectChanged)
    Q_PROPERTY(int activeSpriteEditor READ activeSpriteEditor WRITE setActiveSpriteEditor NOTIFY projectChanged)
    Q_PROPERTY(bool spritePlacementMode READ spritePlacementMode WRITE setSpritePlacementMode NOTIFY projectChanged)
    Q_PROPERTY(bool spritePanActive READ spritePanActive WRITE setSpritePanActive NOTIFY projectChanged)
    Q_PROPERTY(int spritePanX READ spritePanX NOTIFY projectChanged)
    Q_PROPERTY(int spritePanY READ spritePanY NOTIFY projectChanged)
    Q_PROPERTY(bool canUndoSprite READ canUndoSprite NOTIFY projectChanged)
    Q_PROPERTY(bool canRedoSprite READ canRedoSprite NOTIFY projectChanged)
    Q_PROPERTY(bool canPasteSpritePattern READ canPasteSpritePattern NOTIFY projectChanged)
    Q_PROPERTY(bool canRotateSpritePattern READ canRotateSpritePattern NOTIFY projectChanged)
    Q_PROPERTY(int spritePatternsPerSet READ spritePatternsPerSet NOTIFY projectChanged)
    Q_PROPERTY(int placementWidth READ placementWidth WRITE setPlacementWidth NOTIFY projectChanged)
    Q_PROPERTY(int placementHeight READ placementHeight WRITE setPlacementHeight NOTIFY projectChanged)
    Q_PROPERTY(QString recipePath READ recipePath NOTIFY projectChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)

public:
    explicit EditorProjectController(ImageInputController* imageInput,
                                     QObject* parent = nullptr);

    [[nodiscard]] QString projectName() const { return projectName_; }
    [[nodiscard]] bool tms9918aEnabled() const { return tms9918aEnabled_; }
    [[nodiscard]] int workspaceMode() const { return workspaceMode_; }
    [[nodiscard]] bool f18aEnabled() const { return f18aEnabled_; }
    [[nodiscard]] bool v9938Enabled() const { return v9938Enabled_; }
    [[nodiscard]] bool v9958Enabled() const { return v9958Enabled_; }
    [[nodiscard]] bool segaSmsEnabled() const { return segaSmsEnabled_; }
    [[nodiscard]] bool segaGenesisEnabled() const { return segaGenesisEnabled_; }
    [[nodiscard]] bool huc6270Enabled() const { return huc6270Enabled_; }
    [[nodiscard]] bool vicIiEnabled() const { return vicIiEnabled_; }
    [[nodiscard]] bool vicEnabled() const { return vicEnabled_; }
    [[nodiscard]] bool gameBoyEnabled() const { return gameBoyEnabled_; }
    [[nodiscard]] bool gameBoyColorEnabled() const { return gameBoyColorEnabled_; }
    [[nodiscard]] bool superNesEnabled() const { return superNesEnabled_; }
    [[nodiscard]] QStringList plannedTargetIds() const { return plannedTargetIds_; }
    [[nodiscard]] int activeTarget() const;
    [[nodiscard]] QVariantList supportedTargets() const;
    [[nodiscard]] QVariantMap activeTargetInfo() const;
    [[nodiscard]] bool screenImageModeAvailable() const;
    [[nodiscard]] bool characterModeAvailable() const;
    [[nodiscard]] bool spriteModeAvailable() const;
    [[nodiscard]] int previewTarget() const { return previewTarget_; }
    [[nodiscard]] int editScope() const { return editScope_; }
    [[nodiscard]] int activeCharacterSet() const { return activeCharacterSet_; }
    [[nodiscard]] int activeCharacterPattern() const { return activeCharacterPattern_; }
    [[nodiscard]] QVariantList characterSetNames() const;
    [[nodiscard]] QVariantList characterPaletteColors() const;
    [[nodiscard]] QVariantList spritePaletteColors() const;
    [[nodiscard]] int characterForegroundColorIndex() const {
        return characterForegroundColorIndex_;
    }
    [[nodiscard]] int characterBackgroundColorIndex() const {
        return characterBackgroundColorIndex_;
    }
    [[nodiscard]] int characterPaletteBank() const { return characterPaletteBank_; }
    [[nodiscard]] int activeCharacterPlane() const { return activeCharacterPlane_; }
    [[nodiscard]] bool genesisCompositePreview() const { return genesisCompositePreview_; }
    [[nodiscard]] int activeCharacterTilePalette() const;
    [[nodiscard]] bool activeCharacterTileFlipX() const;
    [[nodiscard]] bool activeCharacterTileFlipY() const;
    [[nodiscard]] bool activeCharacterTilePriority() const;
    [[nodiscard]] int characterRevision() const { return characterRevision_; }
    [[nodiscard]] QVariantList characterEditorSlots() const;
    [[nodiscard]] int activeCharacterEditor() const { return activeCharacterEditor_; }
    [[nodiscard]] bool characterTilingMode() const { return characterTilingMode_; }
    [[nodiscard]] bool characterPanActive() const { return characterPanActive_; }
    [[nodiscard]] int characterPanX() const { return characterPanX_; }
    [[nodiscard]] int characterPanY() const { return characterPanY_; }
    [[nodiscard]] bool canUndoCharacter() const {
        return !characterPanActive_ && !characterUndoHistory_.empty();
    }
    [[nodiscard]] bool canRedoCharacter() const {
        return !characterPanActive_ && !characterRedoHistory_.empty();
    }
    [[nodiscard]] bool canPasteCharacterPattern() const;
    [[nodiscard]] int characterPatternWidth() const;
    [[nodiscard]] int characterPatternHeight() const;
    [[nodiscard]] int characterPatternsPerSet() const;
    [[nodiscard]] int characterSetCount() const;
    [[nodiscard]] int characterMapColumns() const;
    [[nodiscard]] int characterMapRows() const;
    [[nodiscard]] bool screenImageSelectionCharacterAligned() const;
    [[nodiscard]] int activeSpriteSet() const { return activeSpriteSet_; }
    [[nodiscard]] int activeSprite() const { return activeSprite_; }
    [[nodiscard]] int activeSpriteSize() const { return activeSpriteSize_; }
    [[nodiscard]] int spriteGlobalSize() const { return spriteGlobalSize_; }
    [[nodiscard]] int spriteDrawingColorIndex() const { return spriteDrawingColorIndex_; }
    [[nodiscard]] int activeSpritePaletteBank() const;
    [[nodiscard]] bool activeSpritePriority() const;
    [[nodiscard]] int activeSpriteColorDepth() const;
    [[nodiscard]] int spriteRevision() const { return spriteRevision_; }
    [[nodiscard]] QVariantList spriteSetNames() const;
    [[nodiscard]] QVariantList activeSpritePlacements() const;
    [[nodiscard]] QVariantList spriteEditorSlots() const;
    [[nodiscard]] int activeSpriteEditor() const { return activeSpriteEditor_; }
    [[nodiscard]] bool spritePlacementMode() const { return spritePlacementMode_; }
    [[nodiscard]] bool spritePanActive() const { return spritePanActive_; }
    [[nodiscard]] int spritePanX() const { return spritePanX_; }
    [[nodiscard]] int spritePanY() const { return spritePanY_; }
    [[nodiscard]] bool canUndoSprite() const {
        return !spritePanActive_ && !spriteUndoHistory_.empty();
    }
    [[nodiscard]] bool canRedoSprite() const {
        return !spritePanActive_ && !spriteRedoHistory_.empty();
    }
    [[nodiscard]] bool canPasteSpritePattern() const;
    [[nodiscard]] bool canRotateSpritePattern() const;
    [[nodiscard]] int spritePatternsPerSet() const;
    [[nodiscard]] int placementWidth() const { return placementWidth_; }
    [[nodiscard]] int placementHeight() const { return placementHeight_; }
    [[nodiscard]] QString recipePath() const { return recipePath_; }
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }

    void setProjectName(const QString& value);
    void setTms9918aEnabled(bool value);
    void setWorkspaceMode(int value);
    void setF18aEnabled(bool value);
    void setActiveTarget(int value);
    void setPreviewTarget(int value);
    void setEditScope(int value);
    void setActiveCharacterSet(int value);
    void setActiveCharacterPattern(int value);
    void setCharacterForegroundColorIndex(int value);
    void setCharacterBackgroundColorIndex(int value);
    void setCharacterPaletteBank(int value);
    void setActiveCharacterPlane(int value);
    void setGenesisCompositePreview(bool value);
    void setActiveCharacterTilePalette(int value);
    void setActiveCharacterTileFlipX(bool value);
    void setActiveCharacterTileFlipY(bool value);
    void setActiveCharacterTilePriority(bool value);
    void setActiveCharacterEditor(int value);
    void setCharacterTilingMode(bool value);
    void setCharacterPanActive(bool value);
    void setActiveSpriteSet(int value);
    void setActiveSprite(int value);
    void setActiveSpriteSize(int value);
    void setSpriteGlobalSize(int value);
    void setSpriteDrawingColorIndex(int value);
    void setActiveSpritePaletteBank(int value);
    void setActiveSpritePriority(bool value);
    void setActiveSpriteColorDepth(int value);
    void setActiveSpriteEditor(int value);
    void setSpritePlacementMode(bool value);
    void setSpritePanActive(bool value);
    void setPlacementWidth(int value);
    void setPlacementHeight(int value);

    Q_INVOKABLE void createProject(const QString& name,
                                   bool tms9918aEnabled,
                                   bool f18aEnabled);
    Q_INVOKABLE void configureProject(const QString& name,
                                      bool tms9918aEnabled,
                                      bool f18aEnabled);
    Q_INVOKABLE void createProjectWithTargets(const QString& name,
                                              bool tms9918aEnabled,
                                              bool f18aEnabled,
                                              const QStringList& plannedTargetIds);
    Q_INVOKABLE void configureProjectWithTargets(const QString& name,
                                                 bool tms9918aEnabled,
                                                 bool f18aEnabled,
                                                 const QStringList& plannedTargetIds);
    Q_INVOKABLE void addSpriteSet();
    Q_INVOKABLE void removeActiveSpriteSet();
    Q_INVOKABLE void selectSpritePattern(int spriteIndex, int size);
    Q_INVOKABLE void moveSprite(int spriteIndex, int x, int y);
    Q_INVOKABLE void addSpriteEditor();
    Q_INVOKABLE void removeActiveSpriteEditor();
    Q_INVOKABLE void moveSpriteEditor(int fromIndex, int toIndex);
    Q_INVOKABLE void moveSpriteEditorTile(int editorIndex, int x, int y);
    Q_INVOKABLE QVariantList spritePatternPixels(int setIndex,
                                                int spriteIndex,
                                                int size) const;
    Q_INVOKABLE int spritePatternWidth(int size) const;
    Q_INVOKABLE int spritePatternHeight(int size) const;
    Q_INVOKABLE void paintSpritePixel(int setIndex,
                                      int spriteIndex,
                                      int size,
                                      int row,
                                      int column,
                                      bool foreground);
    Q_INVOKABLE void drawSpriteLine(int setIndex,
                                    int spriteIndex,
                                    int size,
                                    int fromRow,
                                    int fromColumn,
                                    int toRow,
                                    int toColumn,
                                    bool foreground);
    Q_INVOKABLE void beginSpriteEdit(int setIndex, int spriteIndex, int size);
    Q_INVOKABLE void endSpriteEdit();
    Q_INVOKABLE void rotateActiveSpritePattern();
    Q_INVOKABLE void mirrorActiveSpritePattern();
    Q_INVOKABLE void flipActiveSpritePattern();
    Q_INVOKABLE void blankActiveSpritePattern();
    Q_INVOKABLE void nudgeSpritePan(int horizontal, int vertical);
    Q_INVOKABLE void centerSpritePan();
    Q_INVOKABLE void undoSpriteEdit();
    Q_INVOKABLE void redoSpriteEdit();
    Q_INVOKABLE bool copyActiveSpritePattern();
    Q_INVOKABLE bool pasteActiveSpritePattern();
    Q_INVOKABLE QVariantList characterPatternRows(int setIndex,
                                                  int patternIndex) const;
    Q_INVOKABLE void paintCharacterPixel(int setIndex,
                                         int patternIndex,
                                         int row,
                                         int column,
                                         bool foreground);
    Q_INVOKABLE void drawCharacterLine(int setIndex,
                                       int patternIndex,
                                       int fromRow,
                                       int fromColumn,
                                       int toRow,
                                       int toColumn,
                                       bool foreground);
    Q_INVOKABLE void beginCharacterEdit(int setIndex, int patternIndex);
    Q_INVOKABLE void endCharacterEdit();
    Q_INVOKABLE void rotateActiveCharacterPattern();
    Q_INVOKABLE void mirrorActiveCharacterPattern();
    Q_INVOKABLE void flipActiveCharacterPattern();
    Q_INVOKABLE void blankActiveCharacterPattern();
    Q_INVOKABLE void nudgeCharacterPan(int horizontal, int vertical);
    Q_INVOKABLE void centerCharacterPan();
    Q_INVOKABLE void undoCharacterEdit();
    Q_INVOKABLE void redoCharacterEdit();
    Q_INVOKABLE bool copyActiveCharacterPattern();
    Q_INVOKABLE bool pasteActiveCharacterPattern();
    Q_INVOKABLE bool extractScreenImagePatterns(int characterX,
                                                int characterY,
                                                int regionWidth,
                                                int regionHeight,
                                                int destinationPattern,
                                                bool verticalWrap);
    Q_INVOKABLE QString characterPatternPreview(int setIndex,
                                                int firstPattern,
                                                int patternWidth,
                                                int patternHeight,
                                                bool verticalWrap) const;
    Q_INVOKABLE void addCharacterEditor();
    Q_INVOKABLE void removeActiveCharacterEditor();
    Q_INVOKABLE void moveCharacterEditor(int fromIndex, int toIndex);
    Q_INVOKABLE void moveCharacterTile(int editorIndex, int x, int y);
    Q_INVOKABLE bool saveRecipe(const QUrl& fileUrl);
    Q_INVOKABLE bool loadRecipe(const QUrl& fileUrl);
    Q_INVOKABLE bool exportGenesisCharacterAssets(const QUrl& directoryUrl);
    Q_INVOKABLE bool exportNintendoEditorAssets(const QUrl& directoryUrl);
    Q_INVOKABLE void clearStatus();

signals:
    void projectChanged();
    void statusChanged();

private:
    struct CharacterPattern {
        std::array<std::uint8_t, 8> bitmap{};
        std::array<std::uint8_t, 8> colors{};
        std::array<std::uint8_t, 64> indexedPixels{};
        bool indexedOverride{};
    };
    struct CharacterSet {
        QString name;
        std::array<CharacterPattern, 2048> patterns;
    };
    struct CharacterEditorSlot {
        bool loaded{};
        int setIndex{};
        int patternIndex{};
        int tileX{};
        int tileY{};
        int plane{};
        int palette{};
        bool flipX{};
        bool flipY{};
        bool priority{};
    };
    struct CharacterPatternChange {
        int setIndex{};
        int patternIndex{};
        CharacterPattern before;
        CharacterPattern after;
    };
    struct CharacterHistoryEntry {
        std::vector<CharacterPatternChange> changes;
    };
    struct SpritePlacement {
        int x{};
        int y{};
        int x16{};
        int y16{};
        bool visible{true};
        int size{8};
        int color{15};
        int colorDepth{1};
        int palette{};
        bool flipX{};
        bool flipY{};
        bool priority{};
    };
    struct SpritePattern {
        std::vector<std::uint8_t> baselinePixels = std::vector<std::uint8_t>(4096);
        std::vector<std::uint8_t> f18aPixels = std::vector<std::uint8_t>(4096);
        bool f18aOverride{};
    };
    struct SpriteSet {
        QString name;
        std::array<SpritePattern, 64> patterns8;
        std::array<SpritePattern, 64> patterns16;
        std::array<SpritePattern, 128> patternsGenesis;
        std::array<SpritePlacement, 128> placements;
    };
    struct SpriteHistoryEntry {
        int setIndex{};
        int spriteIndex{};
        int size{8};
        SpritePattern before;
        SpritePattern after;
    };
    struct SpriteEditorSlot {
        bool loaded{};
        int setIndex{};
        int spriteIndex{};
        int size{8};
    };
    struct SpriteClipboardData {
        int size{8};
        int width{8};
        int height{8};
        int colorDepth{1};
        bool enhanced{};
        std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(4096);
    };

    [[nodiscard]] CharacterSet makeCharacterSet(int ordinal) const;
    [[nodiscard]] SpriteSet makeSpriteSet(int ordinal) const;
    [[nodiscard]] CharacterPattern pannedCharacterPattern() const;
    [[nodiscard]] std::optional<CharacterPattern>
        characterPatternFromClipboard() const;
    void recordCharacterEdit(int setIndex, int patternIndex,
                             const CharacterPattern& before,
                             const CharacterPattern& after);
    void recordCharacterEdits(std::vector<CharacterPatternChange> changes);
    void finishCharacterPan();
    [[nodiscard]] SpritePattern& spritePattern(int setIndex,
                                               int spriteIndex,
                                               int size);
    [[nodiscard]] const SpritePattern& spritePattern(int setIndex,
                                                     int spriteIndex,
                                                     int size) const;
    [[nodiscard]] const std::vector<std::uint8_t>&
        visibleSpritePixels(const SpritePattern& pattern) const;
    [[nodiscard]] std::vector<std::uint8_t> pannedSpritePixels() const;
    [[nodiscard]] std::optional<SpriteClipboardData>
        spritePatternFromClipboard() const;
    void ensureF18aSpriteOverride(SpritePattern& pattern);
    void recordSpriteEdit(int setIndex, int spriteIndex, int size,
                          const SpritePattern& before,
                          const SpritePattern& after);
    void finishSpritePan();
    bool activateSpriteEditorBank(int size);
    bool assignActiveSpriteToEditor();
    void syncSpriteDrawingColor();
    [[nodiscard]] bool usesSmsMode4Editor() const;
    [[nodiscard]] bool usesGenesisMode5Editor() const;
    [[nodiscard]] bool usesHuC6270Editor() const;
    [[nodiscard]] bool usesVicIIEditor() const;
    [[nodiscard]] bool usesGameBoyEditor() const;
    [[nodiscard]] bool usesGameBoyColorEditor() const;
    [[nodiscard]] bool usesSuperNesEditor() const;
    [[nodiscard]] bool usesCompoundSpriteEditor() const;
    [[nodiscard]] bool usesIndexed4BppEditor() const;
    [[nodiscard]] bool usesIndexedSpriteEditor() const;
    [[nodiscard]] int activeCharacterColorDepth() const;
    [[nodiscard]] int normalizedSpriteSize(int value) const;
    [[nodiscard]] bool usesPerSpriteSizeEditor() const;
    void ensureIndexedCharacterOverride(CharacterPattern& pattern) const;
    void resetProjectData();
    [[nodiscard]] bool targetEnabled(int value) const;
    [[nodiscard]] bool activeTargetHasCapability(std::uint32_t capability) const;
    void setStatus(QString message, QString error = {});

    ImageInputController* imageInput_{};
    QString projectName_{QStringLiteral("Untitled Project")};
    bool tms9918aEnabled_{true};
    int workspaceMode_{};
    bool f18aEnabled_{true};
    bool v9938Enabled_{};
    bool v9958Enabled_{};
    bool segaSmsEnabled_{};
    bool segaGenesisEnabled_{};
    bool huc6270Enabled_{};
    bool vicIiEnabled_{};
    bool vicEnabled_{};
    bool gameBoyEnabled_{};
    bool gameBoyColorEnabled_{};
    bool superNesEnabled_{};
    QStringList plannedTargetIds_;
    int previewTarget_{};
    int editScope_{};
    int activeCharacterSet_{};
    int activeCharacterPattern_{};
    std::vector<CharacterSet> characterSets_{3};
    int characterForegroundColorIndex_{15};
    int characterBackgroundColorIndex_{1};
    int characterPaletteBank_{};
    int activeCharacterPlane_{};
    bool genesisCompositePreview_{};
    int characterRevision_{};
    std::vector<CharacterEditorSlot> characterEditorSlots_;
    int activeCharacterEditor_{};
    bool characterTilingMode_{};
    bool characterPanActive_{};
    int characterPanX_{};
    int characterPanY_{};
    int characterPanSetIndex_{};
    int characterPanPatternIndex_{};
    CharacterPattern characterPanOriginal_;
    bool characterEditActive_{};
    int characterEditSetIndex_{};
    int characterEditPatternIndex_{};
    CharacterPattern characterEditBefore_;
    std::vector<CharacterHistoryEntry> characterUndoHistory_;
    std::vector<CharacterHistoryEntry> characterRedoHistory_;
    int activeSpriteSet_{};
    int activeSprite_{};
    int activeSpriteSize_{8};
    int spriteGlobalSize_{8};
    int spriteDrawingColorIndex_{15};
    int spriteRevision_{};
    std::vector<SpriteSet> spriteSets_;
    std::vector<SpriteEditorSlot> spriteEditorSlots_;
    int activeSpriteEditor_{};
    bool spritePlacementMode_{};
    bool spritePanActive_{};
    int spritePanX_{};
    int spritePanY_{};
    int spritePanSetIndex_{};
    int spritePanSpriteIndex_{};
    int spritePanSize_{8};
    bool spritePanEnhanced_{};
    SpritePattern spritePanOriginal_;
    bool spriteEditActive_{};
    int spriteEditSetIndex_{};
    int spriteEditSpriteIndex_{};
    int spriteEditSize_{8};
    SpritePattern spriteEditBefore_;
    std::vector<SpriteHistoryEntry> spriteUndoHistory_;
    std::vector<SpriteHistoryEntry> spriteRedoHistory_;
    int placementWidth_{256};
    int placementHeight_{192};
    QString recipePath_;
    QString statusMessage_;
    QString errorMessage_;
};
