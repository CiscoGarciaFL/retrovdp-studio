#include "MediaBatchController.hpp"

#include "AppPreferencesController.hpp"
#include "MediaClipController.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrentRun>

MediaBatchController::MediaBatchController(MediaClipController* mediaClip,
                                           AppPreferencesController* preferences,
                                           QObject* parent)
    : QObject(parent)
    , mediaClip_(mediaClip)
    , preferences_(preferences)
{
    connect(&watcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto result = watcher_.result();
        if (result) {
            outputManifestUrl_ = QUrl::fromLocalFile(result.monitorManifestPath);
            statusMessage_ = tr("Converted %1 frames to %2.")
                                 .arg(result.frameCount)
                                 .arg(result.outputDirectory)
                + tr(" Used %1 conversion worker(s).").arg(result.workersUsed);
            errorMessage_.clear();
            emit stateChanged();
            emit conversionFinished();
        } else {
            outputManifestUrl_.clear();
            statusMessage_.clear();
            errorMessage_ = result.error;
            emit stateChanged();
        }
    });
}

bool MediaBatchController::startConversion(const QUrl& recipeUrl,
                                           const QUrl& outputParentUrl,
                                           const QString& outputName)
{
    if (busy()) return false;
    errorMessage_.clear();
    outputManifestUrl_.clear();
    if (mediaClip_ == nullptr || !mediaClip_->clipLoaded()
        || mediaClip_->outputMonitor()) {
        statusMessage_.clear();
        errorMessage_ = tr("Load a source clip package before starting conversion.");
        emit stateChanged();
        return false;
    }
    if (!recipeUrl.isLocalFile() || !outputParentUrl.isLocalFile()) {
        statusMessage_.clear();
        errorMessage_ = tr("The recipe and output parent must be local paths.");
        emit stateChanged();
        return false;
    }
    QString safeName = outputName.trimmed();
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                     QStringLiteral("-"));
    while (safeName.startsWith(QLatin1Char('-'))) safeName.remove(0, 1);
    while (safeName.endsWith(QLatin1Char('-'))) safeName.chop(1);
    if (safeName.isEmpty()) safeName = QStringLiteral("target-run");
    const QFileInfo parentInfo(outputParentUrl.toLocalFile());
    if (!parentInfo.exists() || !parentInfo.isDir()) {
        statusMessage_.clear();
        errorMessage_ = tr("The selected output parent directory does not exist.");
        emit stateChanged();
        return false;
    }
    const retrovdp::appsupport::MediaBatchConversionRequest request{
        .clipManifestPath = mediaClip_->manifestPath(),
        .recipePath = recipeUrl.toLocalFile(),
        .destinationDirectory = QDir(parentInfo.absoluteFilePath()).filePath(safeName),
        .conversionWorkers = preferences_ != nullptr
            ? preferences_->conversionWorkers() : 0,
    };
    const int workers = retrovdp::appsupport::resolvedConversionWorkers(
        request.conversionWorkers, static_cast<std::size_t>(mediaClip_->frameCount()));
    statusMessage_ = tr("Converting %1 clip frames with %2 worker(s)…")
                         .arg(mediaClip_->frameCount()).arg(workers);
    watcher_.setFuture(QtConcurrent::run([request] {
        return retrovdp::appsupport::convertMediaClip(request);
    }));
    emit stateChanged();
    return true;
}
