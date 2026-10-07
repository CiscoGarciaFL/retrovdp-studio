#pragma once

#include <QObject>
#include <QString>

class ImageInputController;

class AppPreferencesController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(int previewLayout READ previewLayout WRITE setPreviewLayout NOTIFY preferencesChanged)
    Q_PROPERTY(bool sidePanelVisible READ sidePanelVisible WRITE setSidePanelVisible NOTIFY preferencesChanged)
    Q_PROPERTY(int sidePanelMode READ sidePanelMode WRITE setSidePanelMode NOTIFY preferencesChanged)
    Q_PROPERTY(bool restoreWindowGeometry READ restoreWindowGeometry WRITE setRestoreWindowGeometry NOTIFY preferencesChanged)
    Q_PROPERTY(bool rememberWorkspaceMode READ rememberWorkspaceMode WRITE setRememberWorkspaceMode NOTIFY preferencesChanged)
    Q_PROPERTY(int lastWorkspaceMode READ lastWorkspaceMode WRITE setLastWorkspaceMode NOTIFY preferencesChanged)
    Q_PROPERTY(bool rememberConversionSettings READ rememberConversionSettings WRITE setRememberConversionSettings NOTIFY preferencesChanged)
    Q_PROPERTY(int defaultPreset READ defaultPreset WRITE setDefaultPreset NOTIFY preferencesChanged)
    Q_PROPERTY(int defaultExportFormat READ defaultExportFormat WRITE setDefaultExportFormat NOTIFY preferencesChanged)
    Q_PROPERTY(int conversionWorkers READ conversionWorkers WRITE setConversionWorkers NOTIFY preferencesChanged)
    Q_PROPERTY(int availableConversionWorkers READ availableConversionWorkers CONSTANT)
    Q_PROPERTY(int automaticConversionWorkers READ automaticConversionWorkers CONSTANT)
    Q_PROPERTY(bool hasWindowGeometry READ hasWindowGeometry NOTIFY preferencesChanged)
    Q_PROPERTY(int windowX READ windowX NOTIFY preferencesChanged)
    Q_PROPERTY(int windowY READ windowY NOTIFY preferencesChanged)
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY preferencesChanged)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY preferencesChanged)
    Q_PROPERTY(QString ffmpegPath READ ffmpegPath WRITE setFfmpegPath NOTIFY preferencesChanged)
    Q_PROPERTY(QString ffprobePath READ ffprobePath WRITE setFfprobePath NOTIFY preferencesChanged)
    Q_PROPERTY(bool mediaToolsReady READ mediaToolsReady NOTIFY preferencesChanged)
    Q_PROPERTY(bool mediaToolsTesting READ mediaToolsTesting NOTIFY preferencesChanged)
    Q_PROPERTY(QString mediaToolsStatus READ mediaToolsStatus NOTIFY preferencesChanged)

public:
    explicit AppPreferencesController(ImageInputController* imageInput,
                                      QObject* parent = nullptr);

    [[nodiscard]] int previewLayout() const { return previewLayout_; }
    [[nodiscard]] bool sidePanelVisible() const { return sidePanelVisible_; }
    [[nodiscard]] int sidePanelMode() const { return sidePanelMode_; }
    [[nodiscard]] bool restoreWindowGeometry() const { return restoreWindowGeometry_; }
    [[nodiscard]] bool rememberWorkspaceMode() const { return rememberWorkspaceMode_; }
    [[nodiscard]] int lastWorkspaceMode() const { return lastWorkspaceMode_; }
    [[nodiscard]] bool rememberConversionSettings() const {
        return rememberConversionSettings_;
    }
    [[nodiscard]] int defaultPreset() const { return defaultPreset_; }
    [[nodiscard]] int defaultExportFormat() const { return defaultExportFormat_; }
    [[nodiscard]] int conversionWorkers() const { return conversionWorkers_; }
    [[nodiscard]] int availableConversionWorkers() const;
    [[nodiscard]] int automaticConversionWorkers() const;
    [[nodiscard]] bool hasWindowGeometry() const { return hasWindowGeometry_; }
    [[nodiscard]] int windowX() const { return windowX_; }
    [[nodiscard]] int windowY() const { return windowY_; }
    [[nodiscard]] int windowWidth() const { return windowWidth_; }
    [[nodiscard]] int windowHeight() const { return windowHeight_; }
    [[nodiscard]] QString ffmpegPath() const { return ffmpegPath_; }
    [[nodiscard]] QString ffprobePath() const { return ffprobePath_; }
    [[nodiscard]] bool mediaToolsReady() const { return mediaToolsReady_; }
    [[nodiscard]] bool mediaToolsTesting() const { return mediaToolsTesting_; }
    [[nodiscard]] QString mediaToolsStatus() const { return mediaToolsStatus_; }

    void setPreviewLayout(int value);
    void setSidePanelVisible(bool value);
    void setSidePanelMode(int value);
    void setRestoreWindowGeometry(bool value);
    void setRememberWorkspaceMode(bool value);
    void setLastWorkspaceMode(int value);
    void setRememberConversionSettings(bool value);
    void setDefaultPreset(int value);
    void setDefaultExportFormat(int value);
    void setConversionWorkers(int value);
    void setFfmpegPath(const QString& value);
    void setFfprobePath(const QString& value);

    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height);
    Q_INVOKABLE void applyStartupPreferences();
    Q_INVOKABLE void resetInterfaceSettings();
    Q_INVOKABLE void resetBehaviorSettings();
    Q_INVOKABLE void resetDefaultSettings();
    Q_INVOKABLE void resetMediaToolSettings();
    Q_INVOKABLE void restoreAllDefaults();
    Q_INVOKABLE void testMediaTools();

signals:
    void preferencesChanged();

private:
    void loadSettings();
    void saveSettings() const;

    ImageInputController* imageInput_{};
    int previewLayout_{1};
    bool sidePanelVisible_{true};
    int sidePanelMode_{};
    bool restoreWindowGeometry_{true};
    bool rememberWorkspaceMode_{};
    int lastWorkspaceMode_{};
    bool rememberConversionSettings_{true};
    int defaultPreset_{};
    int defaultExportFormat_{};
    int conversionWorkers_{};
    bool hasWindowGeometry_{};
    int windowX_{};
    int windowY_{};
    int windowWidth_{1360};
    int windowHeight_{860};
    QString ffmpegPath_;
    QString ffprobePath_;
    bool mediaToolsReady_{};
    bool mediaToolsTesting_{};
    quint64 mediaToolsGeneration_{};
    QString mediaToolsStatus_{QStringLiteral(
        "Not tested. Leave both paths empty to search beside the application and on PATH.")};
};
