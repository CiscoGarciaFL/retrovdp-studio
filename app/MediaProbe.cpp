#include "MediaProbe.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>

namespace retrovdp::appsupport {
namespace {

MediaProbeResult failure(QString code, QString message)
{
    MediaProbeResult result;
    result.errorCode = std::move(code);
    result.error = std::move(message);
    return result;
}

std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

std::optional<std::int64_t> integerText(const QJsonValue& value)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number < static_cast<double>(std::numeric_limits<std::int64_t>::min())
            || number > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(number);
    }
    if (!value.isString()) return std::nullopt;
    const QByteArray text = value.toString().trimmed().toLatin1();
    std::int64_t parsed{};
    const auto result = std::from_chars(text.constBegin(), text.constEnd(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.constEnd()) return std::nullopt;
    return parsed;
}

std::optional<std::int64_t> secondsToMicroseconds(const QJsonValue& value)
{
    QString text = value.isString()
        ? value.toString().trimmed()
        : QString::number(value.toDouble(), 'f', 6);
    if (text.isEmpty()) return std::nullopt;

    bool negative = false;
    if (text.front() == QLatin1Char('-')) {
        negative = true;
        text.remove(0, 1);
    } else if (text.front() == QLatin1Char('+')) {
        text.remove(0, 1);
    }
    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() > 2 || parts.front().isEmpty()) return std::nullopt;
    bool wholeOk = false;
    const qint64 whole = parts.front().toLongLong(&wholeOk);
    if (!wholeOk || whole < 0
        || whole > (std::numeric_limits<std::int64_t>::max() / 1'000'000)) {
        return std::nullopt;
    }
    QString fraction = parts.size() == 2 ? parts.back() : QString();
    if (fraction.contains(QRegularExpression(QStringLiteral("[^0-9]")))) {
        return std::nullopt;
    }
    fraction = fraction.left(6).leftJustified(6, QLatin1Char('0'));
    bool fractionOk = false;
    const qint64 micros = fraction.isEmpty() ? 0 : fraction.toLongLong(&fractionOk);
    if (!fraction.isEmpty() && !fractionOk) return std::nullopt;
    if (whole > (std::numeric_limits<std::int64_t>::max() - micros) / 1'000'000) {
        return std::nullopt;
    }
    const qint64 combined = whole * 1'000'000 + micros;
    return negative ? -combined : combined;
}

std::optional<media::Rational> parseRational(const QString& text,
                                             QChar separator = QLatin1Char('/'))
{
    const QStringList parts = text.trimmed().split(separator);
    if (parts.size() != 2) return std::nullopt;
    bool numeratorOk = false;
    bool denominatorOk = false;
    media::Rational value{
        parts[0].toLongLong(&numeratorOk),
        parts[1].toLongLong(&denominatorOk),
    };
    if (!numeratorOk || !denominatorOk || !media::isValidPositiveRational(value)) {
        return std::nullopt;
    }
    return media::normalizedRational(value);
}

std::optional<std::uint32_t> positiveUnsigned(const QJsonValue& value)
{
    const auto parsed = integerText(value);
    if (!parsed || *parsed <= 0
        || *parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*parsed);
}

bool dispositionDefault(const QJsonObject& stream)
{
    return stream.value(QStringLiteral("disposition")).toObject()
               .value(QStringLiteral("default")).toInt() == 1;
}

std::string tagText(const QJsonObject& value, const QString& name)
{
    return utf8(value.value(QStringLiteral("tags")).toObject().value(name).toString());
}

QString timelineIdText(const QFileInfo& source)
{
    QString value = source.completeBaseName().toLower();
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                  QStringLiteral("-"));
    while (value.startsWith(QLatin1Char('-'))) value.remove(0, 1);
    while (value.endsWith(QLatin1Char('-'))) value.chop(1);
    value = value.left(static_cast<qsizetype>(media::MediaTimelineId::maximumSize));
    while (value.endsWith(QLatin1Char('-'))) value.chop(1);
    return value.isEmpty() ? QStringLiteral("source") : value;
}

QString processErrorText(const QByteArray& errorOutput)
{
    QString result = QString::fromLocal8Bit(errorOutput).trimmed();
    if (result.size() > 2000) result = result.left(2000) + QStringLiteral("…");
    return result;
}

} // namespace

MediaProbeResult probeMediaFile(const QString& sourcePath,
                                const QString& ffprobeExecutable,
                                const MediaProbeLimits& limits)
{
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile()) {
        return failure(QStringLiteral("media-source-not-found"),
                       QStringLiteral("Media source is not a readable file: %1")
                           .arg(sourcePath));
    }
    if (ffprobeExecutable.trimmed().isEmpty()) {
        return failure(QStringLiteral("ffprobe-unavailable"),
                       QStringLiteral("FFprobe executable is unavailable."));
    }

    QProcess process;
    process.setProgram(ffprobeExecutable);
    process.setArguments({
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
        QStringLiteral("-show_chapters"), source.absoluteFilePath(),
    });
    process.start();
    const int timeout = std::clamp(limits.timeoutMilliseconds, 100, 300'000);
    if (!process.waitForStarted(std::min(timeout, 5000))) {
        return failure(QStringLiteral("ffprobe-start-failed"),
                       QStringLiteral("FFprobe could not be started: %1")
                           .arg(process.errorString()));
    }

    QByteArray output;
    QByteArray errorOutput;
    QElapsedTimer elapsed;
    elapsed.start();
    while (!process.waitForFinished(100)) {
        output += process.readAllStandardOutput();
        errorOutput += process.readAllStandardError();
        if (static_cast<std::size_t>(output.size()) > limits.maximumJsonBytes) {
            process.kill();
            process.waitForFinished(1000);
            return failure(QStringLiteral("ffprobe-output-limit"),
                           QStringLiteral("FFprobe metadata exceeded the configured limit."));
        }
        if (elapsed.elapsed() >= timeout) {
            process.kill();
            process.waitForFinished(1000);
            return failure(QStringLiteral("ffprobe-timeout"),
                           QStringLiteral("FFprobe did not finish within %1 ms.").arg(timeout));
        }
    }
    output += process.readAllStandardOutput();
    errorOutput += process.readAllStandardError();
    if (static_cast<std::size_t>(output.size()) > limits.maximumJsonBytes) {
        return failure(QStringLiteral("ffprobe-output-limit"),
                       QStringLiteral("FFprobe metadata exceeded the configured limit."));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString details = processErrorText(errorOutput);
        return failure(QStringLiteral("ffprobe-failed"),
                       details.isEmpty()
                           ? QStringLiteral("FFprobe failed with exit code %1.")
                                 .arg(process.exitCode())
                           : QStringLiteral("FFprobe failed: %1").arg(details));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failure(QStringLiteral("ffprobe-invalid-json"),
                       QStringLiteral("FFprobe returned invalid JSON: %1")
                           .arg(parseError.errorString()));
    }
    const QJsonObject root = document.object();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    const QJsonArray chapters = root.value(QStringLiteral("chapters")).toArray();
    if (streams.size() > std::clamp(limits.maximumStreams, 1, 4096)) {
        return failure(QStringLiteral("media-stream-limit"),
                       QStringLiteral("Media source declares too many streams."));
    }
    if (chapters.size() > std::clamp(limits.maximumChapters, 0, 1'000'000)) {
        return failure(QStringLiteral("media-chapter-limit"),
                       QStringLiteral("Media source declares too many chapters."));
    }

    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    std::int64_t duration = secondsToMicroseconds(
        format.value(QStringLiteral("duration"))).value_or(0);
    const auto timelineId = media::MediaTimelineId::create(utf8(timelineIdText(source)));
    if (!timelineId) {
        return failure(QStringLiteral("media-invalid-id"),
                       QStringLiteral("A stable timeline ID could not be derived from the source name."));
    }
    media::MediaTimeline timeline{
        .id = *timelineId,
        .sourceKind = media::MediaSourceKind::SingleFile,
        .formatName = utf8(format.value(QStringLiteral("format_name")).toString()),
        .durationMicroseconds = 0,
        .segments = {},
        .videoStreams = {},
        .audioStreams = {},
        .chapters = {},
    };

    for (const QJsonValue& streamValue : streams) {
        const QJsonObject stream = streamValue.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        const auto streamIndex = integerText(stream.value(QStringLiteral("index")));
        const auto timeBase = parseRational(stream.value(QStringLiteral("time_base")).toString());
        if (!streamIndex || *streamIndex < 0 || *streamIndex > std::numeric_limits<int>::max()
            || !timeBase) {
            continue;
        }
        const std::int64_t streamStart = secondsToMicroseconds(
            stream.value(QStringLiteral("start_time"))).value_or(0);
        const std::int64_t streamDuration = secondsToMicroseconds(
            stream.value(QStringLiteral("duration"))).value_or(duration);
        const std::int64_t positiveStreamDuration = std::max<std::int64_t>(0, streamDuration);
        const std::int64_t streamEnd = streamStart
                > std::numeric_limits<std::int64_t>::max() - positiveStreamDuration
            ? std::numeric_limits<std::int64_t>::max()
            : streamStart + positiveStreamDuration;
        duration = std::max(duration, streamEnd);

        if (type == QStringLiteral("video")) {
            const auto width = positiveUnsigned(stream.value(QStringLiteral("width")));
            const auto height = positiveUnsigned(stream.value(QStringLiteral("height")));
            if (!width || !height) continue;
            media::VideoStreamDescriptor descriptor;
            descriptor.index = static_cast<int>(*streamIndex);
            descriptor.codecName = utf8(stream.value(QStringLiteral("codec_name")).toString());
            descriptor.width = *width;
            descriptor.height = *height;
            descriptor.pixelFormat = utf8(stream.value(QStringLiteral("pix_fmt")).toString());
            descriptor.fieldOrder = utf8(stream.value(QStringLiteral("field_order")).toString());
            descriptor.colorRange = utf8(stream.value(QStringLiteral("color_range")).toString());
            descriptor.colorSpace = utf8(stream.value(QStringLiteral("color_space")).toString());
            descriptor.colorTransfer = utf8(stream.value(QStringLiteral("color_transfer")).toString());
            descriptor.colorPrimaries = utf8(stream.value(QStringLiteral("color_primaries")).toString());
            descriptor.sampleAspectRatio = parseRational(
                stream.value(QStringLiteral("sample_aspect_ratio")).toString(),
                QLatin1Char(':')).value_or(media::Rational{1, 1});
            descriptor.averageFrameRate = parseRational(
                stream.value(QStringLiteral("avg_frame_rate")).toString()).value_or(media::Rational{});
            descriptor.nativeFrameRate = parseRational(
                stream.value(QStringLiteral("r_frame_rate")).toString()).value_or(media::Rational{});
            descriptor.timeBase = *timeBase;
            descriptor.startMicroseconds = streamStart;
            descriptor.durationMicroseconds = std::max<std::int64_t>(0, streamDuration);
            descriptor.frameCount = integerText(stream.value(QStringLiteral("nb_frames")));
            descriptor.defaultStream = dispositionDefault(stream);
            timeline.videoStreams.push_back(std::move(descriptor));
        } else if (type == QStringLiteral("audio")) {
            const auto sampleRate = positiveUnsigned(stream.value(QStringLiteral("sample_rate")));
            const auto channels = positiveUnsigned(stream.value(QStringLiteral("channels")));
            if (!sampleRate || !channels) continue;
            media::AudioStreamDescriptor descriptor;
            descriptor.index = static_cast<int>(*streamIndex);
            descriptor.codecName = utf8(stream.value(QStringLiteral("codec_name")).toString());
            descriptor.sampleRate = *sampleRate;
            descriptor.channels = *channels;
            descriptor.channelLayout = utf8(stream.value(QStringLiteral("channel_layout")).toString());
            descriptor.sampleFormat = utf8(stream.value(QStringLiteral("sample_fmt")).toString());
            descriptor.language = tagText(stream, QStringLiteral("language"));
            descriptor.timeBase = *timeBase;
            descriptor.startMicroseconds = streamStart;
            descriptor.durationMicroseconds = std::max<std::int64_t>(0, streamDuration);
            descriptor.defaultStream = dispositionDefault(stream);
            timeline.audioStreams.push_back(std::move(descriptor));
        }
    }

    timeline.durationMicroseconds = duration;
    if (duration <= 0) {
        return failure(QStringLiteral("media-duration-unavailable"),
                       QStringLiteral("FFprobe did not report a positive media duration."));
    }
    const QString canonical = source.canonicalFilePath().isEmpty()
        ? source.absoluteFilePath() : source.canonicalFilePath();
    timeline.segments.push_back({
        .sourceLocator = utf8(QDir::fromNativeSeparators(canonical)),
        .durationMicroseconds = duration,
    });
    for (const QJsonValue& chapterValue : chapters) {
        const QJsonObject chapter = chapterValue.toObject();
        const auto start = secondsToMicroseconds(chapter.value(QStringLiteral("start_time")));
        const auto end = secondsToMicroseconds(chapter.value(QStringLiteral("end_time")));
        if (!start || !end) continue;
        timeline.chapters.push_back({
            .id = static_cast<int>(integerText(chapter.value(QStringLiteral("id"))).value_or(-1)),
            .startMicroseconds = *start,
            .endMicroseconds = *end,
            .title = tagText(chapter, QStringLiteral("title")),
        });
    }

    const auto issues = media::validate(timeline);
    if (!issues.empty()) {
        return failure(QStringLiteral("media-metadata-invalid"),
                       QStringLiteral("FFprobe metadata is invalid at %1: %2")
                           .arg(QString::fromStdString(issues.front().field),
                                QString::fromStdString(issues.front().message)));
    }
    MediaProbeResult result;
    result.timeline = std::move(timeline);
    return result;
}

} // namespace retrovdp::appsupport
