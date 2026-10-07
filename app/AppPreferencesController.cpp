#include "AppPreferencesController.hpp"

#include "ImageInputController.hpp"
#include "MediaBatchConversion.hpp"
#include "MediaToolDiscovery.hpp"

#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QThreadPool>

#include <algorithm>

namespace {

QString mediaToolStatusText(const retrovdp::appsupport::MediaToolStatus& status)
{
    if (status.ready()) {
        return QStringLiteral("FFmpeg media tools are ready.\n%1\n%2\n%3\n%4")
            .arg(status.ffmpeg.version, status.ffmpeg.resolvedPath,
                 status.ffprobe.version, status.ffprobe.resolvedPath);
    }

    QStringList errors;
    if (!status.ffmpeg.available) errors.push_back(status.ffmpeg.error);
    if (!status.ffprobe.available) errors.push_back(status.ffprobe.error);
    return QStringLiteral(
               "FFmpeg media tools are unavailable. See docs/FFMPEG_SETUP.md.\n%1")
        .arg(errors.join(QLatin1Char('\n')));
}

} // namespace

AppPreferencesController::AppPreferencesController(ImageInputController* imageInput,
                                                   QObject* parent)
    : QObject(parent), imageInput_(imageInput)
{
    loadSettings();
}

void AppPreferencesController::setPreviewLayout(int value)
{
    value = std::clamp(value, 0, 2);
    if (previewLayout_ == value) return;
    previewLayout_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setSidePanelVisible(bool value)
{
    if (sidePanelVisible_ == value) return;
    sidePanelVisible_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setSidePanelMode(int value)
{
    value = std::clamp(value, 0, 1);
    if (sidePanelMode_ == value) return;
    sidePanelMode_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setRestoreWindowGeometry(bool value)
{
    if (restoreWindowGeometry_ == value) return;
    restoreWindowGeometry_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setRememberWorkspaceMode(bool value)
{
    if (rememberWorkspaceMode_ == value) return;
    rememberWorkspaceMode_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setLastWorkspaceMode(int value)
{
    value = std::clamp(value, 0, 2);
    if (lastWorkspaceMode_ == value) return;
    lastWorkspaceMode_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setRememberConversionSettings(bool value)
{
    if (rememberConversionSettings_ == value) return;
    rememberConversionSettings_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setDefaultPreset(int value)
{
    value = std::clamp(value, 0, 3);
    if (defaultPreset_ == value) return;
    defaultPreset_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setDefaultExportFormat(int value)
{
    value = std::clamp(value, 0, 8);
    if (defaultExportFormat_ == value) return;
    defaultExportFormat_ = value;
    saveSettings();
    emit preferencesChanged();
}

int AppPreferencesController::availableConversionWorkers() const
{
    return retrovdp::appsupport::availableConversionWorkers();
}

int AppPreferencesController::automaticConversionWorkers() const
{
    return retrovdp::appsupport::automaticConversionWorkers();
}

void AppPreferencesController::setConversionWorkers(int value)
{
    value = std::clamp(value, 0, availableConversionWorkers());
    if (conversionWorkers_ == value) return;
    conversionWorkers_ = value;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setFfmpegPath(const QString& value)
{
    const QString normalized = value.trimmed();
    if (ffmpegPath_ == normalized) return;
    ffmpegPath_ = normalized;
    ++mediaToolsGeneration_;
    mediaToolsReady_ = false;
    mediaToolsTesting_ = false;
    mediaToolsStatus_ = QStringLiteral("Paths changed. Test the media tools again.");
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::setFfprobePath(const QString& value)
{
    const QString normalized = value.trimmed();
    if (ffprobePath_ == normalized) return;
    ffprobePath_ = normalized;
    ++mediaToolsGeneration_;
    mediaToolsReady_ = false;
    mediaToolsTesting_ = false;
    mediaToolsStatus_ = QStringLiteral("Paths changed. Test the media tools again.");
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::saveWindowGeometry(int x, int y, int width, int height)
{
    if (width < 720 || height < 560) return;
    windowX_ = x;
    windowY_ = y;
    windowWidth_ = width;
    windowHeight_ = height;
    hasWindowGeometry_ = true;
    saveSettings();
}

void AppPreferencesController::applyStartupPreferences()
{
    if (imageInput_ == nullptr) return;
    if (!rememberConversionSettings_) {
        imageInput_->resetSettings();
        imageInput_->applyPreset(defaultPreset_);
    }
    imageInput_->setExportFormat(defaultExportFormat_);
}

void AppPreferencesController::resetInterfaceSettings()
{
    previewLayout_ = 1;
    sidePanelVisible_ = true;
    sidePanelMode_ = 0;
    restoreWindowGeometry_ = true;
    rememberWorkspaceMode_ = false;
    lastWorkspaceMode_ = 0;
    hasWindowGeometry_ = false;
    windowX_ = 0;
    windowY_ = 0;
    windowWidth_ = 1360;
    windowHeight_ = 860;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::resetBehaviorSettings()
{
    rememberConversionSettings_ = true;
    conversionWorkers_ = 0;
    if (imageInput_ != nullptr) {
        imageInput_->setAutoUpdate(true);
        imageInput_->setLivePreview(false);
    }
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::resetDefaultSettings()
{
    defaultPreset_ = 0;
    defaultExportFormat_ = 0;
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::resetMediaToolSettings()
{
    ffmpegPath_.clear();
    ffprobePath_.clear();
    ++mediaToolsGeneration_;
    mediaToolsReady_ = false;
    mediaToolsTesting_ = false;
    mediaToolsStatus_ = QStringLiteral(
        "Not tested. Leave both paths empty to search beside the application and on PATH.");
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::restoreAllDefaults()
{
    previewLayout_ = 1;
    sidePanelVisible_ = true;
    sidePanelMode_ = 0;
    restoreWindowGeometry_ = true;
    rememberWorkspaceMode_ = false;
    lastWorkspaceMode_ = 0;
    rememberConversionSettings_ = true;
    conversionWorkers_ = 0;
    defaultPreset_ = 0;
    defaultExportFormat_ = 0;
    hasWindowGeometry_ = false;
    windowX_ = 0;
    windowY_ = 0;
    windowWidth_ = 1360;
    windowHeight_ = 860;
    ffmpegPath_.clear();
    ffprobePath_.clear();
    ++mediaToolsGeneration_;
    mediaToolsReady_ = false;
    mediaToolsTesting_ = false;
    mediaToolsStatus_ = QStringLiteral(
        "Not tested. Leave both paths empty to search beside the application and on PATH.");
    if (imageInput_ != nullptr) {
        imageInput_->resetSettings();
        imageInput_->setAutoUpdate(true);
        imageInput_->setLivePreview(false);
        imageInput_->setExportFormat(0);
    }
    saveSettings();
    emit preferencesChanged();
}

void AppPreferencesController::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("preferences"));
    previewLayout_ = std::clamp(settings.value(QStringLiteral("previewLayout"), 1).toInt(), 0, 2);
    sidePanelVisible_ = settings.value(QStringLiteral("sidePanelVisible"), true).toBool();
    sidePanelMode_ = std::clamp(settings.value(QStringLiteral("sidePanelMode"), 0).toInt(), 0, 1);
    restoreWindowGeometry_ = settings.value(QStringLiteral("restoreWindowGeometry"), true).toBool();
    rememberWorkspaceMode_ = settings.value(QStringLiteral("rememberWorkspaceMode"), false).toBool();
    lastWorkspaceMode_ = std::clamp(settings.value(QStringLiteral("lastWorkspaceMode"), 0).toInt(), 0, 2);
    rememberConversionSettings_ = settings.value(QStringLiteral("rememberConversionSettings"), true).toBool();
    defaultPreset_ = std::clamp(settings.value(QStringLiteral("defaultPreset"), 0).toInt(), 0, 3);
    defaultExportFormat_ = std::clamp(settings.value(QStringLiteral("defaultExportFormat"), 0).toInt(), 0, 8);
    conversionWorkers_ = std::clamp(
        settings.value(QStringLiteral("conversionWorkers"), 0).toInt(),
        0, availableConversionWorkers());
    hasWindowGeometry_ = settings.value(QStringLiteral("hasWindowGeometry"), false).toBool();
    windowX_ = settings.value(QStringLiteral("windowX"), 0).toInt();
    windowY_ = settings.value(QStringLiteral("windowY"), 0).toInt();
    windowWidth_ = std::max(720, settings.value(QStringLiteral("windowWidth"), 1360).toInt());
    windowHeight_ = std::max(560, settings.value(QStringLiteral("windowHeight"), 860).toInt());
    ffmpegPath_ = settings.value(QStringLiteral("ffmpegPath")).toString().trimmed();
    ffprobePath_ = settings.value(QStringLiteral("ffprobePath")).toString().trimmed();
    settings.endGroup();
}

void AppPreferencesController::saveSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("preferences"));
    settings.setValue(QStringLiteral("previewLayout"), previewLayout_);
    settings.setValue(QStringLiteral("sidePanelVisible"), sidePanelVisible_);
    settings.setValue(QStringLiteral("sidePanelMode"), sidePanelMode_);
    settings.setValue(QStringLiteral("restoreWindowGeometry"), restoreWindowGeometry_);
    settings.setValue(QStringLiteral("rememberWorkspaceMode"), rememberWorkspaceMode_);
    settings.setValue(QStringLiteral("lastWorkspaceMode"), lastWorkspaceMode_);
    settings.setValue(QStringLiteral("rememberConversionSettings"), rememberConversionSettings_);
    settings.setValue(QStringLiteral("defaultPreset"), defaultPreset_);
    settings.setValue(QStringLiteral("defaultExportFormat"), defaultExportFormat_);
    settings.setValue(QStringLiteral("conversionWorkers"), conversionWorkers_);
    settings.setValue(QStringLiteral("hasWindowGeometry"), hasWindowGeometry_);
    settings.setValue(QStringLiteral("windowX"), windowX_);
    settings.setValue(QStringLiteral("windowY"), windowY_);
    settings.setValue(QStringLiteral("windowWidth"), windowWidth_);
    settings.setValue(QStringLiteral("windowHeight"), windowHeight_);
    settings.setValue(QStringLiteral("ffmpegPath"), ffmpegPath_);
    settings.setValue(QStringLiteral("ffprobePath"), ffprobePath_);
    settings.endGroup();
    settings.sync();
}

void AppPreferencesController::testMediaTools()
{
    if (mediaToolsTesting_) return;
    mediaToolsTesting_ = true;
    mediaToolsReady_ = false;
    mediaToolsStatus_ = QStringLiteral("Checking FFmpeg and FFprobe…");
    const quint64 generation = ++mediaToolsGeneration_;
    emit preferencesChanged();

    const QString ffmpeg = ffmpegPath_;
    const QString ffprobe = ffprobePath_;
    const QPointer<AppPreferencesController> self(this);
    QThreadPool::globalInstance()->start([self, ffmpeg, ffprobe, generation] {
        const auto status = retrovdp::appsupport::probeMediaTools(ffmpeg, ffprobe);
        if (self.isNull()) return;
        QMetaObject::invokeMethod(
            self,
            [self, status, generation] {
                if (self.isNull() || self->mediaToolsGeneration_ != generation) return;
                self->mediaToolsTesting_ = false;
                self->mediaToolsReady_ = status.ready();
                self->mediaToolsStatus_ = mediaToolStatusText(status);
                emit self->preferencesChanged();
            },
            Qt::QueuedConnection);
    });
}
