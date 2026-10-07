#include "MediaExtraction.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace retrovdp::appsupport {
namespace {

MediaExtractionResult failure(QString code, QString message)
{
    MediaExtractionResult result;
    result.errorCode = std::move(code);
    result.error = std::move(message);
    return result;
}

QString secondsArgument(std::int64_t microseconds)
{
    const auto whole = microseconds / 1'000'000;
    const auto fraction = microseconds % 1'000'000;
    return QStringLiteral("%1.%2")
        .arg(whole)
        .arg(fraction, 6, 10, QLatin1Char('0'));
}

QString rationalText(media::Rational value)
{
    return QStringLiteral("%1/%2").arg(value.numerator).arg(value.denominator);
}

QString sizingName(ExtractionSizingMode value)
{
    switch (value) {
    case ExtractionSizingMode::Native: return QStringLiteral("native");
    case ExtractionSizingMode::Fit: return QStringLiteral("fit");
    case ExtractionSizingMode::Fill: return QStringLiteral("fill");
    case ExtractionSizingMode::Stretch: return QStringLiteral("stretch");
    }
    return {};
}

QString audioFormatName(AudioExtractionFormat value)
{
    switch (value) {
    case AudioExtractionFormat::None: return QStringLiteral("none");
    case AudioExtractionFormat::Wav: return QStringLiteral("wav");
    case AudioExtractionFormat::Flac: return QStringLiteral("flac");
    case AudioExtractionFormat::OggVorbis: return QStringLiteral("ogg-vorbis");
    }
    return {};
}

QString audioExtension(AudioExtractionFormat value)
{
    switch (value) {
    case AudioExtractionFormat::Wav: return QStringLiteral("wav");
    case AudioExtractionFormat::Flac: return QStringLiteral("flac");
    case AudioExtractionFormat::OggVorbis: return QStringLiteral("ogg");
    case AudioExtractionFormat::None: break;
    }
    return {};
}

const media::VideoStreamDescriptor* videoStream(
    const media::MediaTimeline& timeline, int requested)
{
    if (requested >= 0) {
        for (const auto& stream : timeline.videoStreams) {
            if (stream.index == requested) return &stream;
        }
        return nullptr;
    }
    for (const auto& stream : timeline.videoStreams) {
        if (stream.defaultStream) return &stream;
    }
    return timeline.videoStreams.empty() ? nullptr : &timeline.videoStreams.front();
}

const media::AudioStreamDescriptor* audioStream(
    const media::MediaTimeline& timeline, std::optional<int> requested)
{
    if (requested) {
        for (const auto& stream : timeline.audioStreams) {
            if (stream.index == *requested) return &stream;
        }
        return nullptr;
    }
    for (const auto& stream : timeline.audioStreams) {
        if (stream.defaultStream) return &stream;
    }
    return timeline.audioStreams.empty() ? nullptr : &timeline.audioStreams.front();
}

QString videoFilter(const MediaExtractionRequest& request,
                    media::Rational framesPerSecond)
{
    QStringList filters;
    filters.push_back(QStringLiteral("fps=%1").arg(rationalText(framesPerSecond)));
    if (request.sizingMode == ExtractionSizingMode::Native) {
        return filters.join(QLatin1Char(','));
    }
    const QString size = QStringLiteral("%1:%2")
                             .arg(request.outputWidth)
                             .arg(request.outputHeight);
    if (request.sizingMode == ExtractionSizingMode::Stretch) {
        filters.push_back(QStringLiteral("scale=%1:flags=lanczos").arg(size));
    } else if (request.sizingMode == ExtractionSizingMode::Fit) {
        filters.push_back(QStringLiteral(
            "scale=%1:force_original_aspect_ratio=decrease:flags=lanczos").arg(size));
        filters.push_back(QStringLiteral(
            "pad=%1:(ow-iw)/2:(oh-ih)/2:color=black").arg(size));
    } else if (request.sizingMode == ExtractionSizingMode::Fill) {
        filters.push_back(QStringLiteral(
            "scale=%1:force_original_aspect_ratio=increase:flags=lanczos").arg(size));
        filters.push_back(QStringLiteral("crop=%1").arg(size));
    }
    return filters.join(QLatin1Char(','));
}

bool runProcess(const QString& executable,
                const QStringList& arguments,
                const MediaExtractionLimits& limits,
                QString& error)
{
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start();
    const int timeout = std::clamp(limits.timeoutMilliseconds, 1000, 24 * 60 * 60 * 1000);
    if (!process.waitForStarted(std::min(timeout, 5000))) {
        error = QStringLiteral("FFmpeg could not be started: %1").arg(process.errorString());
        return false;
    }
    QByteArray output;
    QElapsedTimer elapsed;
    elapsed.start();
    while (!process.waitForFinished(100)) {
        output += process.readAllStandardOutput();
        output += process.readAllStandardError();
        if (static_cast<std::size_t>(output.size()) > limits.maximumProcessOutputBytes) {
            process.kill();
            process.waitForFinished(1000);
            error = QStringLiteral("FFmpeg diagnostic output exceeded the configured limit.");
            return false;
        }
        if (elapsed.elapsed() >= timeout) {
            process.kill();
            process.waitForFinished(1000);
            error = QStringLiteral("FFmpeg did not finish within %1 ms.").arg(timeout);
            return false;
        }
    }
    output += process.readAllStandardOutput();
    output += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString details = QString::fromLocal8Bit(output).trimmed();
        if (details.size() > 3000) details = details.right(3000);
        error = details.isEmpty()
            ? QStringLiteral("FFmpeg failed with exit code %1.").arg(process.exitCode())
            : QStringLiteral("FFmpeg failed: %1").arg(details);
        return false;
    }
    return true;
}

bool writeManifest(const QString& path, const QJsonObject& root, QString& error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Could not create clip metadata: %1").arg(file.errorString());
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        error = QStringLiteral("Could not commit clip metadata: %1").arg(file.errorString());
        return false;
    }
    return true;
}

std::int64_t frameOffsetMicroseconds(std::int64_t ordinal,
                                     media::Rational framesPerSecond)
{
    const std::int64_t scaledDenominator = 1'000'000LL
        * framesPerSecond.denominator;
    return (ordinal / framesPerSecond.numerator) * scaledDenominator
        + (ordinal % framesPerSecond.numerator) * scaledDenominator
            / framesPerSecond.numerator;
}

} // namespace

MediaExtractionResult extractMediaClip(const MediaExtractionRequest& request,
                                       const MediaExtractionLimits& limits)
{
    if (request.timeline == nullptr) {
        return failure(QStringLiteral("extract-missing-timeline"),
                       QStringLiteral("Media extraction requires probed timeline metadata."));
    }
    if (request.ffmpegExecutable.trimmed().isEmpty()) {
        return failure(QStringLiteral("ffmpeg-unavailable"),
                       QStringLiteral("FFmpeg executable is unavailable."));
    }
    const QFileInfo source(request.sourcePath);
    if (!source.exists() || !source.isFile()) {
        return failure(QStringLiteral("media-source-not-found"),
                       QStringLiteral("Media source is not a readable file."));
    }
    const QFileInfo destination(QDir::cleanPath(request.destinationDirectory));
    if (destination.exists()) {
        return failure(QStringLiteral("extract-output-exists"),
                       QStringLiteral("Extraction destination already exists: %1")
                           .arg(destination.absoluteFilePath()));
    }
    if (request.startMicroseconds < 0
        || request.startMicroseconds >= request.timeline->durationMicroseconds) {
        return failure(QStringLiteral("extract-invalid-range"),
                       QStringLiteral("Extraction start is outside the source timeline."));
    }
    const std::int64_t availableDuration = request.timeline->durationMicroseconds
        - request.startMicroseconds;
    const std::int64_t duration = request.durationMicroseconds.value_or(availableDuration);
    if (duration <= 0 || duration > availableDuration) {
        return failure(QStringLiteral("extract-invalid-range"),
                       QStringLiteral("Extraction duration is outside the source timeline."));
    }
    const auto* selectedVideo = videoStream(*request.timeline, request.videoStreamIndex);
    if (selectedVideo == nullptr) {
        return failure(QStringLiteral("extract-video-stream-unavailable"),
                       QStringLiteral("The selected video stream is unavailable."));
    }
    media::Rational fps = request.framesPerSecond;
    if (!media::isValidPositiveRational(fps)) fps = selectedVideo->averageFrameRate;
    if (!media::isValidPositiveRational(fps)) fps = selectedVideo->nativeFrameRate;
    if (!media::isValidPositiveRational(fps)) {
        return failure(QStringLiteral("extract-frame-rate-unavailable"),
                       QStringLiteral("A positive extraction frame rate is required."));
    }
    fps = media::normalizedRational(fps);
    if (fps.numerator > 1'000'000 || fps.denominator > 1'000'000
        || static_cast<long double>(fps.numerator)
                / static_cast<long double>(fps.denominator) > 1000.0L) {
        return failure(QStringLiteral("extract-invalid-frame-rate"),
                       QStringLiteral("Extraction frame rate is outside supported limits."));
    }
    if (request.sizingMode != ExtractionSizingMode::Native
        && (request.outputWidth == 0 || request.outputHeight == 0
            || request.outputWidth > 16'384 || request.outputHeight > 16'384)) {
        return failure(QStringLiteral("extract-invalid-size"),
                       QStringLiteral("Scaled extraction requires dimensions from 1 through 16384."));
    }

    const long double estimated = static_cast<long double>(duration)
        * static_cast<long double>(fps.numerator)
        / (1'000'000.0L * static_cast<long double>(fps.denominator));
    if (estimated > static_cast<long double>(limits.maximumFrames)) {
        return failure(QStringLiteral("extract-frame-limit"),
                       QStringLiteral("Requested extraction exceeds the configured frame limit."));
    }

    const QDir parent(destination.absolutePath());
    if (!QDir().mkpath(parent.absolutePath())) {
        return failure(QStringLiteral("extract-output-parent"),
                       QStringLiteral("Could not create the extraction parent directory."));
    }
    QTemporaryDir staging(parent.filePath(QStringLiteral(".retrovdp-extract-XXXXXX")));
    if (!staging.isValid()) {
        return failure(QStringLiteral("extract-staging-failed"),
                       QStringLiteral("Could not create a temporary extraction directory."));
    }
    QDir stagingDir(staging.path());
    if (!stagingDir.mkpath(QStringLiteral("frames"))) {
        return failure(QStringLiteral("extract-staging-failed"),
                       QStringLiteral("Could not create the frame output directory."));
    }

    const QString framePattern = stagingDir.filePath(
        QStringLiteral("frames/frame_%06d.png"));
    QStringList frameArguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-ss"), secondsArgument(request.startMicroseconds),
        QStringLiteral("-i"), source.absoluteFilePath(),
        QStringLiteral("-t"), secondsArgument(duration),
        QStringLiteral("-map"), QStringLiteral("0:%1").arg(selectedVideo->index),
        QStringLiteral("-an"), QStringLiteral("-vf"), videoFilter(request, fps),
        QStringLiteral("-fps_mode"), QStringLiteral("passthrough"),
        QStringLiteral("-start_number"), QStringLiteral("1"),
        QStringLiteral("-compression_level"), QStringLiteral("6"),
        framePattern,
    };
    QString processError;
    if (!runProcess(request.ffmpegExecutable, frameArguments, limits, processError)) {
        return failure(QStringLiteral("frame-extraction-failed"), processError);
    }

    const QStringList frameFiles = QDir(stagingDir.filePath(QStringLiteral("frames")))
        .entryList({QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name);
    if (frameFiles.isEmpty()) {
        return failure(QStringLiteral("frame-extraction-empty"),
                       QStringLiteral("FFmpeg produced no source frames."));
    }
    if (static_cast<std::size_t>(frameFiles.size()) > limits.maximumFrames) {
        return failure(QStringLiteral("extract-frame-limit"),
                       QStringLiteral("FFmpeg produced more frames than the configured limit."));
    }

    QString relativeAudioPath;
    if (request.audioFormat != AudioExtractionFormat::None) {
        const auto* selectedAudio = audioStream(*request.timeline, request.audioStreamIndex);
        if (selectedAudio == nullptr) {
            return failure(QStringLiteral("extract-audio-stream-unavailable"),
                           QStringLiteral("The selected audio stream is unavailable."));
        }
        if (!stagingDir.mkpath(QStringLiteral("audio"))) {
            return failure(QStringLiteral("extract-staging-failed"),
                           QStringLiteral("Could not create the audio output directory."));
        }
        relativeAudioPath = QStringLiteral("audio/source.%1")
                                .arg(audioExtension(request.audioFormat));
        QStringList audioArguments{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-ss"), secondsArgument(request.startMicroseconds),
            QStringLiteral("-i"), source.absoluteFilePath(),
            QStringLiteral("-t"), secondsArgument(duration),
            QStringLiteral("-map"), QStringLiteral("0:%1").arg(selectedAudio->index),
            QStringLiteral("-vn"),
        };
        if (request.audioFormat == AudioExtractionFormat::Wav) {
            audioArguments.append({QStringLiteral("-c:a"), QStringLiteral("pcm_s16le")});
        } else if (request.audioFormat == AudioExtractionFormat::Flac) {
            audioArguments.append({QStringLiteral("-c:a"), QStringLiteral("flac")});
        } else if (request.audioFormat == AudioExtractionFormat::OggVorbis) {
            audioArguments.append({QStringLiteral("-c:a"), QStringLiteral("libvorbis"),
                                   QStringLiteral("-q:a"), QStringLiteral("5")});
        }
        audioArguments.push_back(stagingDir.filePath(relativeAudioPath));
        if (!runProcess(request.ffmpegExecutable, audioArguments, limits, processError)) {
            return failure(QStringLiteral("audio-extraction-failed"), processError);
        }
    }

    QJsonArray frames;
    for (qsizetype index = 0; index < frameFiles.size(); ++index) {
        const auto ordinal = static_cast<std::int64_t>(index);
        const auto frameOffset = frameOffsetMicroseconds(ordinal, fps);
        const auto nextFrameOffset = frameOffsetMicroseconds(ordinal + 1, fps);
        if (frameOffset >= duration || nextFrameOffset <= frameOffset) {
            return failure(QStringLiteral("frame-timing-invalid"),
                           QStringLiteral("FFmpeg produced frames outside the requested range."));
        }
        const auto pts = request.startMicroseconds + frameOffset;
        const auto frameDuration = std::min(nextFrameOffset, duration) - frameOffset;
        frames.push_back(QJsonObject{
            {QStringLiteral("ordinal"), ordinal},
            {QStringLiteral("number"), ordinal + 1},
            {QStringLiteral("file"), QStringLiteral("frames/%1").arg(frameFiles[index])},
            {QStringLiteral("ptsUs"), pts},
            {QStringLiteral("durationUs"), frameDuration},
        });
    }
    QJsonObject audio;
    if (!relativeAudioPath.isEmpty()) {
        const auto* selectedAudio = audioStream(*request.timeline, request.audioStreamIndex);
        audio = {
            {QStringLiteral("file"), relativeAudioPath},
            {QStringLiteral("format"), audioFormatName(request.audioFormat)},
            {QStringLiteral("sourceStream"), selectedAudio->index},
            {QStringLiteral("startUs"), request.startMicroseconds},
            {QStringLiteral("durationUs"), duration},
        };
    }
    const QJsonObject root{
        {QStringLiteral("kind"), QStringLiteral("retrovdp-media-clip")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("source"), QJsonObject{
            {QStringLiteral("path"), QDir::fromNativeSeparators(source.absoluteFilePath())},
            {QStringLiteral("timelineId"), QString::fromStdString(request.timeline->id.value())},
            {QStringLiteral("format"), QString::fromStdString(request.timeline->formatName)},
        }},
        {QStringLiteral("range"), QJsonObject{
            {QStringLiteral("startUs"), request.startMicroseconds},
            {QStringLiteral("durationUs"), duration},
        }},
        {QStringLiteral("extraction"), QJsonObject{
            {QStringLiteral("videoStream"), selectedVideo->index},
            {QStringLiteral("framesPerSecond"), rationalText(fps)},
            {QStringLiteral("sizing"), sizingName(request.sizingMode)},
            {QStringLiteral("width"), static_cast<qint64>(request.outputWidth)},
            {QStringLiteral("height"), static_cast<qint64>(request.outputHeight)},
            {QStringLiteral("audioFormat"), audioFormatName(request.audioFormat)},
        }},
        {QStringLiteral("frames"), frames},
        {QStringLiteral("audio"), audio},
    };
    const QString stagingManifest = stagingDir.filePath(QStringLiteral("clip.json"));
    QString writeError;
    if (!writeManifest(stagingManifest, root, writeError)) {
        return failure(QStringLiteral("clip-manifest-write-failed"), writeError);
    }

    staging.setAutoRemove(false);
    if (!QDir(destination.absolutePath()).rename(staging.path(), destination.fileName())) {
        staging.setAutoRemove(true);
        return failure(QStringLiteral("extract-commit-failed"),
                       QStringLiteral("Could not commit the completed clip package."));
    }

    MediaExtractionResult result;
    result.outputDirectory = destination.absoluteFilePath();
    result.manifestPath = QDir(result.outputDirectory).filePath(QStringLiteral("clip.json"));
    result.frameCount = static_cast<std::size_t>(frameFiles.size());
    if (!relativeAudioPath.isEmpty()) {
        result.audioPath = QDir(result.outputDirectory).filePath(relativeAudioPath);
    }
    return result;
}

} // namespace retrovdp::appsupport
