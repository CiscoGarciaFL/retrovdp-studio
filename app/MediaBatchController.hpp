#pragma once

#include "MediaBatchConversion.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>

class MediaClipController;
class AppPreferencesController;

class MediaBatchController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QUrl outputManifestUrl READ outputManifestUrl NOTIFY stateChanged)

public:
    explicit MediaBatchController(MediaClipController* mediaClip,
                                  AppPreferencesController* preferences,
                                  QObject* parent = nullptr);

    [[nodiscard]] bool busy() const { return watcher_.isRunning(); }
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }
    [[nodiscard]] QUrl outputManifestUrl() const { return outputManifestUrl_; }

    Q_INVOKABLE bool startConversion(const QUrl& recipeUrl,
                                     const QUrl& outputParentUrl,
                                     const QString& outputName);

signals:
    void stateChanged();
    void conversionFinished();

private:
    MediaClipController* mediaClip_{};
    AppPreferencesController* preferences_{};
    QFutureWatcher<retrovdp::appsupport::MediaBatchConversionResult> watcher_;
    QString statusMessage_;
    QString errorMessage_;
    QUrl outputManifestUrl_;
};
