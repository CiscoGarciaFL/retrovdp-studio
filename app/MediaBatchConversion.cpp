#include "MediaBatchConversion.hpp"

#include "ConversionPipeline.hpp"
#include "ConversionRecipe.hpp"
#include "MediaClipController.hpp"

#include "retrovdp/core/TargetProfile.hpp"
#include "retrovdp/imageio/ExportWriter.hpp"
#include "retrovdp/imageio/ImageLoader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace retrovdp::appsupport {
namespace {

MediaBatchConversionResult failure(QString code, QString error)
{
    MediaBatchConversionResult result;
    result.errorCode = std::move(code);
    result.error = std::move(error);
    return result;
}

QString firstConversionError(const core::ConversionResult& result)
{
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.severity == core::DiagnosticSeverity::Error) {
            return QString::fromStdString(diagnostic.message);
        }
    }
    return QStringLiteral("Conversion failed without an error diagnostic.");
}

bool writeJson(const QString& path, const QJsonObject& object, QString& error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Could not create %1: %2")
                    .arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        error = QStringLiteral("Could not commit %1: %2")
                    .arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    return true;
}

QString stableModeName(core::ConversionMode mode)
{
    const auto& descriptor = core::displayMode(mode);
    return QString::fromLatin1(descriptor.stableId.data(),
                               static_cast<qsizetype>(descriptor.stableId.size()));
}

QString stableTargetName(core::TargetProfileId target)
{
    const auto& profile = core::targetProfile(target);
    return QString::fromLatin1(profile.stableId.data(),
                               static_cast<qsizetype>(profile.stableId.size()));
}

struct BatchFrameInput {
    int index{};
    QString sourceFrame;
    QString sourceName;
    qint64 ordinal{};
    qint64 number{};
    qint64 pts{};
    qint64 duration{};
};

struct BatchFrameOutput {
    bool success{};
    QString errorCode;
    QString error;
    QString relativePreview;
    QStringList nativeFiles;
    std::vector<std::string> warnings;
};

BatchFrameOutput frameFailure(QString code, QString error)
{
    BatchFrameOutput result;
    result.errorCode = std::move(code);
    result.error = std::move(error);
    return result;
}

BatchFrameOutput convertFrame(const BatchFrameInput& frame,
                              const ConversionRecipe& recipe,
                              const QString& stagingRoot,
                              core::CancellationToken cancellation)
{
    if (cancellation.isCancellationRequested()) {
        return frameFailure(QStringLiteral("conversion-cancelled"),
                            QStringLiteral("Frame conversion was cancelled."));
    }
    auto loaded = imageio::loadImageFile(frame.sourceFrame);
    if (!loaded) {
        return frameFailure(
            QStringLiteral("conversion-frame-load-failed"),
            QStringLiteral("Could not load source frame %1: %2")
                .arg(frame.index + 1).arg(loaded.error));
    }
    auto source = std::make_shared<const core::RgbImage>(std::move(*loaded.image));
    const core::ConversionRequest conversionRequest{
        .source = std::move(source),
        .settings = recipe.settings,
        .generation = static_cast<std::uint64_t>(frame.index + 1),
        .cancellation = cancellation,
    };
    auto converted = runConversion(conversionRequest, recipe.pipeline);
    if (!converted.succeeded() || !converted.preview || !converted.target) {
        if (converted.status == core::ConversionStatus::Cancelled) {
            return frameFailure(QStringLiteral("conversion-cancelled"),
                                QStringLiteral("Frame conversion was cancelled."));
        }
        return frameFailure(
            QStringLiteral("conversion-frame-failed"),
            QStringLiteral("Frame %1 failed: %2")
                .arg(frame.index + 1).arg(firstConversionError(converted)));
    }

    const QString baseName = QStringLiteral("frame_%1")
                                 .arg(frame.index + 1, 6, 10, QLatin1Char('0'));
    const formats::ExportRequest nativeRequest{
        .format = recipe.exportFormat,
        .baseName = baseName.toStdString(),
        .target = &*converted.target,
        .preview = &*converted.preview,
    };
    auto nativeManifest = recipe.exportFormat == formats::ExportFormat::Png
        ? imageio::generatePngExport(nativeRequest)
        : formats::generateExport(nativeRequest);
    if (!nativeManifest) {
        return frameFailure(
            QStringLiteral("conversion-export-unavailable"),
            QStringLiteral("Frame %1 could not be exported: %2")
                .arg(frame.index + 1)
                .arg(QString::fromStdString(nativeManifest.message)));
    }

    const QDir stagingDirectory(stagingRoot);
    const QString relativeNativeDirectory = QStringLiteral("native/%1").arg(baseName);
    const auto nativeWritten = imageio::writeExportManifest(
        stagingDirectory.filePath(relativeNativeDirectory), nativeManifest);
    if (!nativeWritten) {
        return frameFailure(
            QStringLiteral("conversion-native-write-failed"),
            QStringLiteral("Frame %1 native output failed: %2")
                .arg(frame.index + 1).arg(nativeWritten.error));
    }

    const formats::ExportRequest previewRequest{
        .format = formats::ExportFormat::Png,
        .baseName = baseName.toStdString(),
        .target = &*converted.target,
        .preview = &*converted.preview,
    };
    const auto previewManifest = imageio::generatePngExport(previewRequest);
    const auto previewWritten = imageio::writeExportManifest(
        stagingDirectory.filePath(QStringLiteral("previews")), previewManifest);
    if (!previewWritten) {
        return frameFailure(
            QStringLiteral("conversion-preview-write-failed"),
            QStringLiteral("Frame %1 preview output failed: %2")
                .arg(frame.index + 1).arg(previewWritten.error));
    }

    BatchFrameOutput result;
    result.success = true;
    result.relativePreview = QStringLiteral("previews/%1.png").arg(baseName);
    for (const QString& path : nativeWritten.paths) {
        result.nativeFiles.push_back(QDir::fromNativeSeparators(
            stagingDirectory.relativeFilePath(path)));
    }
    result.warnings = std::move(nativeManifest.warnings);
    return result;
}

} // namespace

int availableConversionWorkers()
{
    return std::max(1, QThread::idealThreadCount());
}

int automaticConversionWorkers()
{
    return std::min(std::max(1, availableConversionWorkers() - 1), 8);
}

int resolvedConversionWorkers(int requestedWorkers, std::size_t frameCount)
{
    const int requested = requestedWorkers <= 0
        ? automaticConversionWorkers() : requestedWorkers;
    const int bounded = std::clamp(requested, 1, availableConversionWorkers());
    if (frameCount == 0) return 1;
    return static_cast<int>(std::min<std::size_t>(
        static_cast<std::size_t>(bounded), frameCount));
}

MediaBatchConversionResult convertMediaClip(
    const MediaBatchConversionRequest& request,
    const MediaBatchConversionLimits& limits)
{
    MediaClipController clip;
    if (!clip.openClipUrl(QUrl::fromLocalFile(request.clipManifestPath))) {
        return failure(QStringLiteral("clip-package-invalid"), clip.errorMessage());
    }
    if (static_cast<std::size_t>(clip.frameCount()) > limits.maximumFrames) {
        return failure(QStringLiteral("conversion-frame-limit"),
                       QStringLiteral("The clip exceeds the configured conversion frame limit."));
    }
    const auto loadedRecipe = loadConversionRecipe(request.recipePath);
    if (!loadedRecipe) {
        return failure(loadedRecipe.errorCode, loadedRecipe.error);
    }
    const ConversionRecipe& recipe = *loadedRecipe.recipe;
    const QFileInfo destination(QDir::cleanPath(request.destinationDirectory));
    if (destination.exists()) {
        return failure(QStringLiteral("conversion-output-exists"),
                       QStringLiteral("Conversion destination already exists: %1")
                           .arg(destination.absoluteFilePath()));
    }
    const QDir parent(destination.absolutePath());
    if (!QDir().mkpath(parent.absolutePath())) {
        return failure(QStringLiteral("conversion-output-parent"),
                       QStringLiteral("Could not create the conversion parent directory."));
    }
    QTemporaryDir staging(parent.filePath(QStringLiteral(".retrovdp-convert-XXXXXX")));
    if (!staging.isValid()) {
        return failure(QStringLiteral("conversion-staging-failed"),
                       QStringLiteral("Could not create a temporary conversion directory."));
    }
    QDir stagingDirectory(staging.path());
    if (!stagingDirectory.mkpath(QStringLiteral("previews"))
        || !stagingDirectory.mkpath(QStringLiteral("native"))) {
        return failure(QStringLiteral("conversion-staging-failed"),
                       QStringLiteral("Could not create conversion output directories."));
    }

    const QString recipeSnapshot = stagingDirectory.filePath(
        QStringLiteral("recipe.rvdp.json"));
    if (!QFile::copy(recipe.absolutePath, recipeSnapshot)) {
        return failure(QStringLiteral("recipe-snapshot-failed"),
                       QStringLiteral("Could not snapshot the conversion recipe."));
    }

    const QString targetName = stableTargetName(recipe.settings.targetProfile);
    const QString modeName = stableModeName(recipe.settings.mode);
    const QString formatName = QString::fromStdString(
        formats::exportFormatId(recipe.exportFormat).value());

    std::vector<BatchFrameInput> frameInputs;
    frameInputs.reserve(static_cast<std::size_t>(clip.frameCount()));
    for (int frameIndex = 0; frameIndex < clip.frameCount(); ++frameIndex) {
        const QModelIndex modelIndex = clip.index(frameIndex);
        frameInputs.push_back({
            .index = frameIndex,
            .sourceFrame = modelIndex.data(
                MediaClipController::FrameUrlRole).toUrl().toLocalFile(),
            .sourceName = modelIndex.data(MediaClipController::FileRole).toString(),
            .ordinal = modelIndex.data(MediaClipController::OrdinalRole).toLongLong(),
            .number = modelIndex.data(MediaClipController::NumberRole).toLongLong(),
            .pts = modelIndex.data(
                MediaClipController::PtsMicrosecondsRole).toLongLong(),
            .duration = modelIndex.data(
                MediaClipController::DurationMicrosecondsRole).toLongLong(),
        });
    }

    const int workerCount = resolvedConversionWorkers(
        request.conversionWorkers, frameInputs.size());
    std::vector<BatchFrameOutput> frameOutputs(frameInputs.size());
    std::atomic_size_t nextFrame{};
    std::atomic_bool failed{};
    core::CancellationSource cancellation;
    std::mutex failureMutex;
    std::optional<MediaBatchConversionResult> firstFailure;

    QThreadPool workerPool;
    workerPool.setMaxThreadCount(workerCount);
    workerPool.setExpiryTimeout(-1);
    std::vector<QFuture<void>> workers;
    workers.reserve(static_cast<std::size_t>(workerCount));
    for (int worker = 0; worker < workerCount; ++worker) {
        workers.push_back(QtConcurrent::run(&workerPool, [&] {
            while (!failed.load(std::memory_order_relaxed)) {
                const std::size_t index = nextFrame.fetch_add(
                    1, std::memory_order_relaxed);
                if (index >= frameInputs.size()) return;

                BatchFrameOutput output;
                try {
                    output = convertFrame(frameInputs[index], recipe, staging.path(),
                                          cancellation.token());
                } catch (const std::exception& exception) {
                    output.errorCode = QStringLiteral("conversion-frame-exception");
                    output.error = QStringLiteral("Frame %1 failed unexpectedly: %2")
                                       .arg(frameInputs[index].index + 1)
                                       .arg(QString::fromLocal8Bit(exception.what()));
                } catch (...) {
                    output.errorCode = QStringLiteral("conversion-frame-exception");
                    output.error = QStringLiteral("Frame %1 failed unexpectedly.")
                                       .arg(frameInputs[index].index + 1);
                }
                frameOutputs[index] = std::move(output);
                if (!frameOutputs[index].success) {
                    bool expected = false;
                    if (failed.compare_exchange_strong(expected, true)) {
                        {
                            const std::lock_guard lock(failureMutex);
                            firstFailure = failure(frameOutputs[index].errorCode,
                                                   frameOutputs[index].error);
                        }
                        cancellation.requestCancellation();
                    }
                    return;
                }
            }
        }));
    }
    for (auto& worker : workers) worker.waitForFinished();
    workerPool.waitForDone();
    if (firstFailure) return std::move(*firstFailure);

    QJsonArray outputFrames;
    QJsonArray runFrames;
    QJsonArray runWarnings;
    for (std::size_t index = 0; index < frameInputs.size(); ++index) {
        const auto& input = frameInputs[index];
        const auto& output = frameOutputs[index];
        outputFrames.push_back(QJsonObject{
            {QStringLiteral("ordinal"), input.ordinal},
            {QStringLiteral("number"), input.number},
            {QStringLiteral("file"), output.relativePreview},
            {QStringLiteral("ptsUs"), input.pts},
            {QStringLiteral("durationUs"), input.duration},
        });
        QJsonArray nativeFiles;
        for (const QString& path : output.nativeFiles) nativeFiles.push_back(path);
        for (const std::string& warning : output.warnings) {
            const QString text = QString::fromStdString(warning);
            runWarnings.push_back(QJsonObject{
                {QStringLiteral("frame"), input.index + 1},
                {QStringLiteral("message"), text},
            });
        }
        runFrames.push_back(QJsonObject{
            {QStringLiteral("ordinal"), input.ordinal},
            {QStringLiteral("source"), input.sourceName},
            {QStringLiteral("preview"), output.relativePreview},
            {QStringLiteral("native"), nativeFiles},
            {QStringLiteral("ptsUs"), input.pts},
            {QStringLiteral("durationUs"), input.duration},
        });
    }

    const QJsonObject recipeJson{
        {QStringLiteral("file"), QStringLiteral("recipe.rvdp.json")},
        {QStringLiteral("sourcePath"), QDir::fromNativeSeparators(recipe.absolutePath)},
        {QStringLiteral("sha256"), recipe.sha256},
    };
    const QJsonObject targetJson{
        {QStringLiteral("target"), targetName},
        {QStringLiteral("mode"), modeName},
        {QStringLiteral("format"), formatName},
    };
    const QJsonObject runRoot{
        {QStringLiteral("kind"), QStringLiteral("retrovdp-media-conversion-run")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("sourceClip"), QDir::fromNativeSeparators(
             QFileInfo(request.clipManifestPath).absoluteFilePath())},
        {QStringLiteral("recipe"), recipeJson},
        {QStringLiteral("output"), targetJson},
        {QStringLiteral("conversion"), QJsonObject{
             {QStringLiteral("workers"), workerCount},
        }},
        {QStringLiteral("frames"), runFrames},
        {QStringLiteral("warnings"), runWarnings},
    };
    const QJsonObject monitorRoot{
        {QStringLiteral("kind"), QStringLiteral("retrovdp-media-output")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("path"), QDir::fromNativeSeparators(
                  QFileInfo(request.clipManifestPath).absoluteFilePath())},
             {QStringLiteral("timelineId"), clip.clipName()},
             {QStringLiteral("format"), QStringLiteral("target-preview")},
        }},
        {QStringLiteral("range"), QJsonObject{
             {QStringLiteral("startUs"), clip.startMicroseconds()},
             {QStringLiteral("durationUs"), clip.durationMicroseconds()},
        }},
        {QStringLiteral("extraction"), QJsonObject{
             {QStringLiteral("framesPerSecond"), clip.framesPerSecond()},
             {QStringLiteral("sizing"), QStringLiteral("target")},
             {QStringLiteral("width"), static_cast<qint64>(
                  core::displayMode(recipe.settings.mode).geometry.width)},
             {QStringLiteral("height"), static_cast<qint64>(
                  core::displayMode(recipe.settings.mode).geometry.height)},
        }},
        {QStringLiteral("frames"), outputFrames},
        {QStringLiteral("audio"), QJsonObject{}},
        {QStringLiteral("output"), QJsonObject{
             {QStringLiteral("run"), QStringLiteral("run.json")},
             {QStringLiteral("target"), targetJson},
             {QStringLiteral("recipeSha256"), recipe.sha256},
        }},
    };
    QString writeError;
    if (!writeJson(stagingDirectory.filePath(QStringLiteral("run.json")),
                   runRoot, writeError)
        || !writeJson(stagingDirectory.filePath(QStringLiteral("clip.json")),
                      monitorRoot, writeError)) {
        return failure(QStringLiteral("conversion-manifest-write-failed"), writeError);
    }

    staging.setAutoRemove(false);
    if (!QDir(destination.absolutePath()).rename(staging.path(), destination.fileName())) {
        staging.setAutoRemove(true);
        return failure(QStringLiteral("conversion-commit-failed"),
                       QStringLiteral("Could not commit the completed conversion run."));
    }
    MediaBatchConversionResult result;
    result.outputDirectory = destination.absoluteFilePath();
    result.runManifestPath = QDir(result.outputDirectory).filePath(
        QStringLiteral("run.json"));
    result.monitorManifestPath = QDir(result.outputDirectory).filePath(
        QStringLiteral("clip.json"));
    result.frameCount = static_cast<std::size_t>(clip.frameCount());
    result.workersUsed = workerCount;
    return result;
}

} // namespace retrovdp::appsupport
