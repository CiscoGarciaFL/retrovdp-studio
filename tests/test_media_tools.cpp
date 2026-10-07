#include "MediaExtraction.hpp"
#include "MediaBatchConversion.hpp"
#include "MediaClipController.hpp"
#include "MediaProbe.hpp"
#include "MediaToolDiscovery.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>

#include <iostream>

namespace {

struct TestContext {
    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    TestContext test;

    const QString ffmpeg = QStringLiteral(RETROVDP_FFMPEG_FIXTURE_PATH);
    const QString ffprobe = QStringLiteral(RETROVDP_FFPROBE_FIXTURE_PATH);
    const auto available = retrovdp::appsupport::probeMediaTools(
        ffmpeg, ffprobe, 2000);
    test.expect(available.ready()
                    && available.ffmpeg.resolvedPath == QFileInfo(ffmpeg).canonicalFilePath()
                    && available.ffprobe.resolvedPath == QFileInfo(ffprobe).canonicalFilePath()
                    && available.ffmpeg.version.startsWith(QStringLiteral("ffmpeg version"))
                    && available.ffprobe.version.startsWith(QStringLiteral("ffprobe version")),
                "explicit media tools should be launched and identified by version output");

    QTemporaryDir missingDirectory;
    const auto unavailable = retrovdp::appsupport::probeMediaTools(
        QDir(missingDirectory.path()).filePath(QStringLiteral("missing-ffmpeg")),
        QDir(missingDirectory.path()).filePath(QStringLiteral("missing-ffprobe")),
        2000);
    test.expect(!unavailable.ready()
                    && !unavailable.ffmpeg.available
                    && !unavailable.ffprobe.available
                    && unavailable.ffmpeg.error.contains(QStringLiteral("not found"))
                    && unavailable.ffprobe.error.contains(QStringLiteral("not found")),
                "missing explicit media tools should return actionable probe errors");

    const auto probed = retrovdp::appsupport::probeMediaFile(ffprobe, ffprobe);
    test.expect(probed
                    && probed.timeline->durationMicroseconds == 5'055'000
                    && probed.timeline->videoStreams.size() == 1
                    && probed.timeline->audioStreams.size() == 1
                    && probed.timeline->videoStreams.front().averageFrameRate
                        == retrovdp::media::Rational{30'000, 1001}
                    && probed.timeline->audioStreams.front().sampleRate == 48'000,
                "bounded FFprobe JSON should normalize into a synchronized media timeline");

    retrovdp::appsupport::MediaProbeLimits smallOutputLimit;
    smallOutputLimit.maximumJsonBytes = 32;
    const auto limited = retrovdp::appsupport::probeMediaFile(
        ffprobe, ffprobe, smallOutputLimit);
    test.expect(!limited
                    && limited.errorCode == QStringLiteral("ffprobe-output-limit"),
                "media probing should reject metadata larger than its configured bound");

    QTemporaryDir extractionParent;
    const QString extractionDestination = QDir(extractionParent.path())
                                              .filePath(QStringLiteral("clip-package"));
    const retrovdp::appsupport::MediaExtractionRequest extractionRequest{
        .sourcePath = ffprobe,
        .destinationDirectory = extractionDestination,
        .ffmpegExecutable = ffmpeg,
        .timeline = &*probed.timeline,
        .durationMicroseconds = 1'000'000,
        .framesPerSecond = {2, 1},
        .sizingMode = retrovdp::appsupport::ExtractionSizingMode::Fit,
        .outputWidth = 320,
        .outputHeight = 240,
        .audioFormat = retrovdp::appsupport::AudioExtractionFormat::Wav,
    };
    const auto extracted = retrovdp::appsupport::extractMediaClip(extractionRequest);
    QFile manifestFile(extracted.manifestPath);
    const bool manifestOpened = manifestFile.open(QIODevice::ReadOnly);
    const QJsonObject manifest = manifestOpened
        ? QJsonDocument::fromJson(manifestFile.readAll()).object() : QJsonObject{};
    test.expect(extracted
                    && extracted.frameCount == 2
                    && QFileInfo::exists(QDir(extractionDestination).filePath(
                        QStringLiteral("frames/frame_000001.png")))
                    && QFileInfo::exists(extracted.audioPath)
                    && manifest.value(QStringLiteral("kind"))
                        == QStringLiteral("retrovdp-media-clip")
                    && manifest.value(QStringLiteral("frames")).toArray().size() == 2
                    && manifest.value(QStringLiteral("audio")).toObject()
                           .value(QStringLiteral("format")) == QStringLiteral("wav"),
                "media extraction should atomically commit frames, audio, and clip metadata");

    MediaClipController clipController;
    const bool clipOpened = clipController.openClipUrl(
        QUrl::fromLocalFile(extracted.manifestPath));
    clipController.seekToFrame(1);
    const QModelIndex secondFrame = clipController.index(1);
    test.expect(clipOpened && clipController.clipLoaded()
                    && clipController.frameCount() == 2
                    && clipController.currentFrame() == 1
                    && clipController.positionMicroseconds() == 500'000
                    && clipController.hasAudio()
                    && clipController.audioFormat() == QStringLiteral("wav")
                    && secondFrame.data(MediaClipController::TimeLabelRole).toString()
                        == QStringLiteral("00:00:00.500")
                    && secondFrame.data(MediaClipController::FrameUrlRole).toUrl().isLocalFile(),
                "clip monitor model should expose validated frames, audio, and exact timestamps");
    clipController.stepBackward();
    test.expect(clipController.currentFrame() == 0,
                "clip monitor should support deterministic frame stepping");

    const bool missingClipOpened = clipController.openClipUrl(QUrl::fromLocalFile(
        QDir(extractionParent.path()).filePath(QStringLiteral("missing-clip.json"))));
    test.expect(!missingClipOpened && !clipController.errorMessage().isEmpty(),
                "clip monitor should report an actionable error for an unavailable package");

    const QString validFrame = QDir(QStringLiteral(RETROVDP_GOLDEN_DIR))
                                   .filePath(QStringLiteral("source/tiny-rgba.png"));
    for (const QString& frameName : {QStringLiteral("frame_000001.png"),
                                     QStringLiteral("frame_000002.png")}) {
        const QString framePath = QDir(extractionDestination)
                                      .filePath(QStringLiteral("frames/%1").arg(frameName));
        QFile::remove(framePath);
        QFile::copy(validFrame, framePath);
    }
    const QString conversionDestination = QDir(extractionParent.path())
                                              .filePath(QStringLiteral("target-run"));
    const auto batchConverted = retrovdp::appsupport::convertMediaClip({
        .clipManifestPath = extracted.manifestPath,
        .recipePath = QDir(QStringLiteral(RETROVDP_FIXTURE_DIR))
                          .filePath(QStringLiteral("screen-image-v1.rvdp.json")),
        .destinationDirectory = conversionDestination,
        .conversionWorkers = 1,
    });
    MediaClipController outputController;
    const bool outputOpened = outputController.openClipUrl(
        QUrl::fromLocalFile(batchConverted.monitorManifestPath));
    test.expect(batchConverted && batchConverted.frameCount == 2
                    && QFileInfo::exists(batchConverted.runManifestPath)
                    && QFileInfo::exists(QDir(conversionDestination).filePath(
                        QStringLiteral("previews/frame_000001.png")))
                    && QFileInfo::exists(QDir(conversionDestination).filePath(
                        QStringLiteral("native/frame_000001/frame_000001.TIAP")))
                    && outputOpened && outputController.outputMonitor()
                    && outputController.outputTarget() == QStringLiteral("tms9918a")
                    && outputController.outputMode() == QStringLiteral("bitmap-9918a")
                    && outputController.frameCount() == 2,
                "recipe batch conversion should atomically write native frames and a uniform output monitor");

    const QString parallelDestination = QDir(extractionParent.path())
                                            .filePath(QStringLiteral("target-run-parallel"));
    const auto parallelConverted = retrovdp::appsupport::convertMediaClip({
        .clipManifestPath = extracted.manifestPath,
        .recipePath = QDir(QStringLiteral(RETROVDP_FIXTURE_DIR))
                          .filePath(QStringLiteral("screen-image-v1.rvdp.json")),
        .destinationDirectory = parallelDestination,
        .conversionWorkers = 2,
    });
    const QByteArray serialNative = [] (const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }(QDir(conversionDestination).filePath(
        QStringLiteral("native/frame_000001/frame_000001.TIAP")));
    const QByteArray parallelNative = [] (const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }(QDir(parallelDestination).filePath(
        QStringLiteral("native/frame_000001/frame_000001.TIAP")));
    QFile parallelRunFile(parallelConverted.runManifestPath);
    QJsonObject parallelRun;
    if (parallelRunFile.open(QIODevice::ReadOnly)) {
        parallelRun = QJsonDocument::fromJson(parallelRunFile.readAll()).object();
    }
    test.expect(parallelConverted
                    && parallelConverted.workersUsed
                        == retrovdp::appsupport::resolvedConversionWorkers(2, 2)
                    && !serialNative.isEmpty() && serialNative == parallelNative
                    && parallelRun.value(QStringLiteral("frames")).toArray().size() == 2
                    && parallelRun.value(QStringLiteral("conversion")).toObject()
                           .value(QStringLiteral("workers")).toInt()
                        == parallelConverted.workersUsed,
                "parallel frame conversion should preserve deterministic native output and ordered metadata");

    const auto conflict = retrovdp::appsupport::extractMediaClip(extractionRequest);
    test.expect(!conflict
                    && conflict.errorCode == QStringLiteral("extract-output-exists"),
                "media extraction should reject an existing destination before writing");

    if (test.failures == 0) {
        std::cout << "Media tool discovery validation passed\n";
    }
    return test.failures == 0 ? 0 : 1;
}
