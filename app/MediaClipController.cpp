#include "MediaClipController.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {

constexpr qint64 maximumManifestBytes = 64LL * 1024LL * 1024LL;
constexpr qsizetype maximumFrames = 100'000;
constexpr qint64 maximumClipDurationMicroseconds = 366LL * 24LL * 60LL * 60LL
    * 1'000'000LL;

std::optional<qint64> jsonInteger(const QJsonValue& value)
{
    if (!value.isDouble()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<qint64>::min())
        || number >= 9'007'199'254'740'992.0) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

QString containedExistingFile(const QDir& clipDirectory,
                              const QString& relativePath)
{
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)) return {};
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    if (normalized == QStringLiteral("..")
        || normalized.startsWith(QStringLiteral("../"))) {
        return {};
    }
    const QFileInfo file(clipDirectory.filePath(normalized));
    if (!file.exists() || !file.isFile()) return {};
    const QString clipCanonical = QFileInfo(clipDirectory.absolutePath()).canonicalFilePath();
    const QString fileCanonical = file.canonicalFilePath();
    if (clipCanonical.isEmpty() || fileCanonical.isEmpty()) return {};
    const QString prefix = QDir::cleanPath(clipCanonical) + QLatin1Char('/');
#ifdef Q_OS_WIN
    if (!QDir::fromNativeSeparators(fileCanonical).startsWith(
            QDir::fromNativeSeparators(prefix), Qt::CaseInsensitive)) {
        return {};
    }
#else
    if (!fileCanonical.startsWith(prefix)) return {};
#endif
    return fileCanonical;
}

} // namespace

MediaClipController::MediaClipController(QObject* parent)
    : QAbstractListModel(parent)
{
    playbackTimer_.setInterval(10);
    playbackTimer_.setTimerType(Qt::PreciseTimer);
    connect(&playbackTimer_, &QTimer::timeout,
            this, &MediaClipController::updatePlayback);
}

int MediaClipController::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : frameCount();
}

QVariant MediaClipController::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= frameCount()) return {};
    const Frame& frame = frames_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NumberRole: return frame.number;
    case OrdinalRole: return frame.ordinal;
    case FileRole: return frame.relativeFile;
    case FrameUrlRole: return QUrl::fromLocalFile(frame.absoluteFile);
    case PtsMicrosecondsRole: return frame.ptsMicroseconds;
    case DurationMicrosecondsRole: return frame.durationMicroseconds;
    case TimeLabelRole: return timeLabel(frame.ptsMicroseconds);
    default: return {};
    }
}

QHash<int, QByteArray> MediaClipController::roleNames() const
{
    return {
        {OrdinalRole, "ordinal"},
        {NumberRole, "number"},
        {FileRole, "file"},
        {FrameUrlRole, "frameUrl"},
        {PtsMicrosecondsRole, "ptsMicroseconds"},
        {DurationMicrosecondsRole, "durationMicroseconds"},
        {TimeLabelRole, "timeLabel"},
    };
}

QUrl MediaClipController::currentFrameUrl() const
{
    if (currentFrame_ < 0 || currentFrame_ >= frameCount()) return {};
    return QUrl::fromLocalFile(
        frames_[static_cast<std::size_t>(currentFrame_)].absoluteFile);
}

qint64 MediaClipController::positionMicroseconds() const
{
    if (currentFrame_ < 0 || currentFrame_ >= frameCount()) return startMicroseconds_;
    return frames_[static_cast<std::size_t>(currentFrame_)].ptsMicroseconds;
}

bool MediaClipController::openClipUrl(const QUrl& url)
{
    pause();
    errorMessage_.clear();
    statusMessage_.clear();
    emit messagesChanged();
    if (!url.isLocalFile()) {
        setError(tr("Clip packages must be opened from a local clip.json file."));
        return false;
    }
    const QFileInfo manifestInfo(url.toLocalFile());
    if (!manifestInfo.exists() || !manifestInfo.isFile()) {
        setError(tr("The selected clip metadata file does not exist."));
        return false;
    }
    if (manifestInfo.size() < 2 || manifestInfo.size() > maximumManifestBytes) {
        setError(tr("The selected clip metadata exceeds the supported size limit."));
        return false;
    }
    QFile file(manifestInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Could not open clip metadata: %1").arg(file.errorString()));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.read(maximumManifestBytes + 1), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(tr("Clip metadata is not valid JSON: %1").arg(parseError.errorString()));
        return false;
    }
    const QJsonObject root = document.object();
    const QString packageKind = root.value(QStringLiteral("kind")).toString();
    if ((packageKind != QStringLiteral("retrovdp-media-clip")
         && packageKind != QStringLiteral("retrovdp-media-output"))
        || root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        setError(tr("The selected file is not a supported RetroVDP clip package."));
        return false;
    }
    const QJsonArray frameValues = root.value(QStringLiteral("frames")).toArray();
    if (frameValues.isEmpty() || frameValues.size() > maximumFrames) {
        setError(tr("A clip package must contain between 1 and %1 frames.")
                     .arg(maximumFrames));
        return false;
    }
    const QJsonObject range = root.value(QStringLiteral("range")).toObject();
    const auto clipStart = jsonInteger(range.value(QStringLiteral("startUs")));
    const auto clipDuration = jsonInteger(range.value(QStringLiteral("durationUs")));
    if (!clipStart || !clipDuration || *clipStart < 0 || *clipDuration <= 0
        || *clipDuration > maximumClipDurationMicroseconds
        || *clipStart > std::numeric_limits<qint64>::max() - *clipDuration) {
        setError(tr("The clip range is missing or outside supported limits."));
        return false;
    }

    const QDir clipDirectory = manifestInfo.absoluteDir();
    std::vector<Frame> parsedFrames;
    parsedFrames.reserve(static_cast<std::size_t>(frameValues.size()));
    qint64 previousPts = -1;
    for (qsizetype index = 0; index < frameValues.size(); ++index) {
        const QJsonObject object = frameValues[index].toObject();
        const auto ordinal = jsonInteger(object.value(QStringLiteral("ordinal")));
        const auto number = jsonInteger(object.value(QStringLiteral("number")));
        const auto pts = jsonInteger(object.value(QStringLiteral("ptsUs")));
        const auto frameDuration = jsonInteger(
            object.value(QStringLiteral("durationUs")));
        const QString relativeFile = object.value(QStringLiteral("file")).toString();
        const QString absoluteFile = containedExistingFile(clipDirectory, relativeFile);
        if (!ordinal || !number || !pts || !frameDuration
            || *ordinal != index || *number <= 0 || *pts < *clipStart
            || *pts >= *clipStart + *clipDuration || *pts <= previousPts
            || *frameDuration <= 0
            || *frameDuration > *clipStart + *clipDuration - *pts
            || absoluteFile.isEmpty()) {
            setError(tr("Frame %1 has invalid timing, numbering, or file metadata.")
                         .arg(index + 1));
            return false;
        }
        parsedFrames.push_back({
            .ordinal = static_cast<int>(*ordinal),
            .number = *number,
            .relativeFile = QDir::fromNativeSeparators(relativeFile),
            .absoluteFile = absoluteFile,
            .ptsMicroseconds = *pts,
            .durationMicroseconds = *frameDuration,
        });
        previousPts = *pts;
    }

    QString parsedAudioPath;
    QString parsedAudioFormat;
    const QJsonObject audio = root.value(QStringLiteral("audio")).toObject();
    if (!audio.isEmpty()) {
        parsedAudioPath = containedExistingFile(
            clipDirectory, audio.value(QStringLiteral("file")).toString());
        parsedAudioFormat = audio.value(QStringLiteral("format")).toString();
        if (parsedAudioPath.isEmpty() || parsedAudioFormat.isEmpty()) {
            setError(tr("The clip audio metadata refers to an invalid file."));
            return false;
        }
    }

    beginResetModel();
    clearClipData();
    frames_ = std::move(parsedFrames);
    clipDirectory_ = clipDirectory.absolutePath();
    manifestPath_ = manifestInfo.absoluteFilePath();
    clipName_ = manifestInfo.dir().dirName();
    const QJsonObject source = root.value(QStringLiteral("source")).toObject();
    sourcePath_ = source.value(QStringLiteral("path")).toString();
    audioPath_ = parsedAudioPath;
    audioFormat_ = parsedAudioFormat;
    const QJsonObject extraction = root.value(QStringLiteral("extraction")).toObject();
    framesPerSecond_ = extraction.value(QStringLiteral("framesPerSecond")).toString();
    sizingMode_ = extraction.value(QStringLiteral("sizing")).toString();
    outputMonitor_ = packageKind == QStringLiteral("retrovdp-media-output");
    const QJsonObject outputTarget = root.value(QStringLiteral("output")).toObject()
                                         .value(QStringLiteral("target")).toObject();
    outputTarget_ = outputTarget.value(QStringLiteral("target")).toString();
    outputMode_ = outputTarget.value(QStringLiteral("mode")).toString();
    outputFormat_ = outputTarget.value(QStringLiteral("format")).toString();
    frameWidth_ = extraction.value(QStringLiteral("width")).toInt();
    frameHeight_ = extraction.value(QStringLiteral("height")).toInt();
    startMicroseconds_ = *clipStart;
    durationMicroseconds_ = *clipDuration;
    currentFrame_ = 0;
    endResetModel();
    statusMessage_ = tr("Loaded %1 frames from %2.")
                         .arg(frameCount())
                         .arg(clipName_);
    emit clipChanged();
    emit currentFrameChanged();
    emit messagesChanged();
    return true;
}

void MediaClipController::closeClip()
{
    pause();
    if (frames_.empty() && errorMessage_.isEmpty() && statusMessage_.isEmpty()) return;
    beginResetModel();
    clearClipData();
    endResetModel();
    emit clipChanged();
    emit currentFrameChanged();
    emit messagesChanged();
}

void MediaClipController::play()
{
    if (frames_.empty() || playbackTimer_.isActive()) return;
    if (currentFrame_ >= frameCount() - 1) setCurrentFrame(0, false);
    playbackOriginMicroseconds_ = positionMicroseconds();
    playbackClock_.restart();
    playbackTimer_.start();
    statusMessage_ = tr("Playing source frames.");
    emit playingChanged();
    emit messagesChanged();
}

void MediaClipController::pause()
{
    if (!playbackTimer_.isActive()) return;
    playbackTimer_.stop();
    statusMessage_ = tr("Playback paused.");
    emit playingChanged();
    emit messagesChanged();
}

void MediaClipController::togglePlayback()
{
    playing() ? pause() : play();
}

void MediaClipController::seekToFrame(int index)
{
    if (frames_.empty()) return;
    setCurrentFrame(std::clamp(index, 0, frameCount() - 1), true);
}

void MediaClipController::stepForward()
{
    if (frames_.empty()) return;
    pause();
    setCurrentFrame(std::min(currentFrame_ + 1, frameCount() - 1), false);
}

void MediaClipController::stepBackward()
{
    if (frames_.empty()) return;
    pause();
    setCurrentFrame(std::max(currentFrame_ - 1, 0), false);
}

void MediaClipController::clearClipData()
{
    frames_.clear();
    clipDirectory_.clear();
    manifestPath_.clear();
    clipName_.clear();
    sourcePath_.clear();
    audioPath_.clear();
    audioFormat_.clear();
    framesPerSecond_.clear();
    sizingMode_.clear();
    outputMonitor_ = false;
    outputTarget_.clear();
    outputMode_.clear();
    outputFormat_.clear();
    startMicroseconds_ = 0;
    durationMicroseconds_ = 0;
    frameWidth_ = 0;
    frameHeight_ = 0;
    currentFrame_ = -1;
    statusMessage_.clear();
    errorMessage_.clear();
}

void MediaClipController::setCurrentFrame(int index, bool resetPlaybackClock)
{
    if (index == currentFrame_ || index < 0 || index >= frameCount()) return;
    currentFrame_ = index;
    if (resetPlaybackClock && playing()) {
        playbackOriginMicroseconds_ = positionMicroseconds();
        playbackClock_.restart();
    }
    emit currentFrameChanged();
}

void MediaClipController::setError(QString message)
{
    errorMessage_ = std::move(message);
    statusMessage_.clear();
    emit messagesChanged();
}

void MediaClipController::updatePlayback()
{
    if (frames_.empty()) {
        pause();
        return;
    }
    const qint64 target = playbackOriginMicroseconds_
        + playbackClock_.elapsed() * 1000;
    const qint64 clipEnd = startMicroseconds_ + durationMicroseconds_;
    if (target >= clipEnd) {
        setCurrentFrame(frameCount() - 1, false);
        pause();
        return;
    }
    const auto found = std::upper_bound(
        frames_.begin(), frames_.end(), target,
        [](qint64 time, const Frame& frame) {
            return time < frame.ptsMicroseconds;
        });
    const int index = found == frames_.begin()
        ? 0
        : static_cast<int>(std::distance(frames_.begin(), found) - 1);
    setCurrentFrame(index, false);
}

QString MediaClipController::timeLabel(qint64 absoluteMicroseconds) const
{
    const qint64 relative = std::max<qint64>(
        0, absoluteMicroseconds - startMicroseconds_);
    const qint64 totalMilliseconds = relative / 1000;
    const qint64 hours = totalMilliseconds / 3'600'000;
    const qint64 minutes = (totalMilliseconds / 60'000) % 60;
    const qint64 seconds = (totalMilliseconds / 1000) % 60;
    const qint64 milliseconds = totalMilliseconds % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}
