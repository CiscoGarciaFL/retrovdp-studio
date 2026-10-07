#include "ClipMapAdapter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

#include <limits>

namespace retrovdp::appsupport {
namespace {

constexpr qint64 maximumMapBytes = 16 * 1024 * 1024;
constexpr qsizetype maximumEntries = 100'000;

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value);
}

std::string toUtf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

ClipMapFormat detectedFormat(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("json")) return ClipMapFormat::RetroVdpJson;
    if (suffix == QStringLiteral("csv")) return ClipMapFormat::Csv;
    if (suffix == QStringLiteral("tsv")) return ClipMapFormat::Tsv;
    return ClipMapFormat::DaphneFramefile;
}

ClipMapReadResult failure(ClipMapFormat format, QString code, QString message)
{
    return {.document = std::nullopt, .format = format,
            .errorCode = std::move(code), .error = std::move(message), .warnings = {}};
}

QString validationMessage(const std::vector<media::ClipMapValidationIssue>& issues)
{
    if (issues.empty()) return {};
    return QStringLiteral("%1: %2")
        .arg(fromUtf8(issues.front().field), fromUtf8(issues.front().message));
}

QJsonObject rationalJson(media::Rational value)
{
    return {{QStringLiteral("numerator"), static_cast<qint64>(value.numerator)},
            {QStringLiteral("denominator"), static_cast<qint64>(value.denominator)}};
}

std::optional<media::Rational> parseRationalJson(const QJsonValue& value)
{
    if (value.isNull() || value.isUndefined()) return std::nullopt;
    if (!value.isObject()) return media::Rational{};
    const QJsonObject object = value.toObject();
    const media::Rational result{
        object.value(QStringLiteral("numerator")).toInteger(),
        object.value(QStringLiteral("denominator")).toInteger(),
    };
    return result;
}

ClipMapReadResult readJson(const QByteArray& bytes, ClipMapFormat format)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        return failure(format, QStringLiteral("clip-map-json-invalid"),
                       QStringLiteral("Invalid clip-map JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("kind")).toString() != QStringLiteral("retrovdp-clip-map")
        || root.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        return failure(format, QStringLiteral("clip-map-schema-unsupported"),
                       QStringLiteral("Expected a retrovdp-clip-map document with schemaVersion 1."));
    }

    media::ClipMapDocument document;
    document.name = toUtf8(root.value(QStringLiteral("name")).toString());
    document.mediaRoot = toUtf8(root.value(QStringLiteral("mediaRoot")).toString());
    document.defaultRecipeLocator = toUtf8(root.value(QStringLiteral("defaultRecipe")).toString());
    document.defaultTarget = toUtf8(root.value(QStringLiteral("defaultTarget")).toString());
    document.frameRate = parseRationalJson(root.value(QStringLiteral("frameRate")));
    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    if (entries.size() > maximumEntries) {
        return failure(format, QStringLiteral("clip-map-too-many-entries"),
                       QStringLiteral("Clip map exceeds the %1-entry safety limit.").arg(maximumEntries));
    }
    document.entries.reserve(static_cast<std::size_t>(entries.size()));
    for (qsizetype index = 0; index < entries.size(); ++index) {
        if (!entries[index].isObject()) {
            return failure(format, QStringLiteral("clip-map-entry-invalid"),
                           QStringLiteral("Entry %1 is not an object.").arg(index));
        }
        const QJsonObject object = entries[index].toObject();
        if (!object.value(QStringLiteral("startFrame")).isDouble()) {
            return failure(format, QStringLiteral("clip-map-frame-invalid"),
                           QStringLiteral("Entry %1 needs an integer startFrame.").arg(index));
        }
        media::ClipMapEntry entry;
        entry.id = toUtf8(object.value(QStringLiteral("id")).toString());
        entry.label = toUtf8(object.value(QStringLiteral("label")).toString());
        entry.startFrame = object.value(QStringLiteral("startFrame")).toInteger();
        if (object.contains(QStringLiteral("endFrame"))
            && !object.value(QStringLiteral("endFrame")).isNull()) {
            if (!object.value(QStringLiteral("endFrame")).isDouble()) {
                return failure(format, QStringLiteral("clip-map-frame-invalid"),
                               QStringLiteral("Entry %1 has an invalid endFrame.").arg(index));
            }
            entry.endFrame = object.value(QStringLiteral("endFrame")).toInteger();
        }
        entry.sourceLocator = toUtf8(object.value(QStringLiteral("source")).toString());
        entry.audioLocator = toUtf8(object.value(QStringLiteral("audio")).toString());
        entry.recipeLocator = toUtf8(object.value(QStringLiteral("recipe")).toString());
        entry.target = toUtf8(object.value(QStringLiteral("target")).toString());
        document.entries.push_back(std::move(entry));
    }
    const auto issues = media::validate(document);
    if (!issues.empty()) {
        return failure(format, QStringLiteral("clip-map-invalid"), validationMessage(issues));
    }
    return {.document = std::move(document), .format = format,
            .errorCode = {}, .error = {}, .warnings = {}};
}

QList<QStringList> parseDelimited(const QString& text, QChar delimiter, QString& error)
{
    QList<QStringList> records;
    QStringList record;
    QString field;
    bool quoted = false;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar ch = text[index];
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (index + 1 < text.size() && text[index + 1] == QLatin1Char('"')) {
                    field += ch;
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
        } else if (ch == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
        } else if (ch == delimiter) {
            record.push_back(field);
            field.clear();
        } else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && index + 1 < text.size()
                && text[index + 1] == QLatin1Char('\n')) ++index;
            record.push_back(field);
            field.clear();
            records.push_back(record);
            record.clear();
        } else {
            field += ch;
        }
    }
    if (quoted) {
        error = QStringLiteral("Delimited clip map has an unterminated quoted field.");
        return {};
    }
    if (!field.isEmpty() || !record.isEmpty()) {
        record.push_back(field);
        records.push_back(record);
    }
    return records;
}

std::optional<media::Rational> parseRationalText(const QString& text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char('/'));
    if (parts.size() != 2) return std::nullopt;
    bool numeratorOk = false;
    bool denominatorOk = false;
    media::Rational value{parts[0].toLongLong(&numeratorOk),
                          parts[1].toLongLong(&denominatorOk)};
    if (!numeratorOk || !denominatorOk || !media::isValidPositiveRational(value)) {
        return std::nullopt;
    }
    return media::normalizedRational(value);
}

ClipMapReadResult readDelimited(const QByteArray& bytes, ClipMapFormat format, QChar delimiter)
{
    QString parseError;
    const QList<QStringList> records = parseDelimited(QString::fromUtf8(bytes), delimiter, parseError);
    if (!parseError.isEmpty()) {
        return failure(format, QStringLiteral("clip-map-delimited-invalid"), parseError);
    }
    media::ClipMapDocument document;
    QHash<QString, int> columns;
    bool foundHeader = false;
    for (const QStringList& record : records) {
        if (record.size() == 1 && record.front().trimmed().isEmpty()) continue;
        const QString first = record.value(0).trimmed();
        if (!foundHeader && first.startsWith(QLatin1Char('#'))) {
            const QString value = record.value(1);
            if (first == QStringLiteral("#name")) document.name = toUtf8(value);
            else if (first == QStringLiteral("#media_root")) document.mediaRoot = toUtf8(value);
            else if (first == QStringLiteral("#frame_rate")) {
                document.frameRate = parseRationalText(value);
                if (!document.frameRate) {
                    return failure(format, QStringLiteral("clip-map-frame-rate-invalid"),
                                   QStringLiteral("Invalid #frame_rate value: %1").arg(value));
                }
            } else if (first == QStringLiteral("#default_recipe")) {
                document.defaultRecipeLocator = toUtf8(value);
            } else if (first == QStringLiteral("#default_target")) {
                document.defaultTarget = toUtf8(value);
            }
            continue;
        }
        if (!foundHeader) {
            for (int index = 0; index < record.size(); ++index) {
                columns.insert(record[index].trimmed().toLower(), index);
            }
            if (!columns.contains(QStringLiteral("start_frame"))
                || !columns.contains(QStringLiteral("source"))) {
                return failure(format, QStringLiteral("clip-map-columns-missing"),
                               QStringLiteral("Delimited clip maps require start_frame and source columns."));
            }
            foundHeader = true;
            continue;
        }
        if (document.entries.size() >= static_cast<std::size_t>(maximumEntries)) {
            return failure(format, QStringLiteral("clip-map-too-many-entries"),
                           QStringLiteral("Clip map exceeds the entry safety limit."));
        }
        const auto value = [&record, &columns](const QString& name) {
            return record.value(columns.value(name, -1));
        };
        bool startOk = false;
        media::ClipMapEntry entry;
        entry.startFrame = value(QStringLiteral("start_frame")).trimmed().toLongLong(&startOk);
        if (!startOk) {
            return failure(format, QStringLiteral("clip-map-frame-invalid"),
                           QStringLiteral("Invalid start_frame in data row %1.")
                               .arg(document.entries.size() + 1));
        }
        const QString end = value(QStringLiteral("end_frame")).trimmed();
        if (!end.isEmpty()) {
            bool endOk = false;
            entry.endFrame = end.toLongLong(&endOk);
            if (!endOk) {
                return failure(format, QStringLiteral("clip-map-frame-invalid"),
                               QStringLiteral("Invalid end_frame in data row %1.")
                                   .arg(document.entries.size() + 1));
            }
        }
        entry.id = toUtf8(value(QStringLiteral("id")).trimmed());
        if (entry.id.empty()) {
            entry.id = QStringLiteral("clip-%1").arg(document.entries.size() + 1, 4, 10,
                                                     QLatin1Char('0')).toStdString();
        }
        entry.label = toUtf8(value(QStringLiteral("label")));
        entry.sourceLocator = toUtf8(value(QStringLiteral("source")).trimmed());
        entry.audioLocator = toUtf8(value(QStringLiteral("audio")).trimmed());
        entry.recipeLocator = toUtf8(value(QStringLiteral("recipe")).trimmed());
        entry.target = toUtf8(value(QStringLiteral("target")).trimmed());
        document.entries.push_back(std::move(entry));
    }
    if (!foundHeader) {
        return failure(format, QStringLiteral("clip-map-header-missing"),
                       QStringLiteral("Delimited clip map does not contain a header row."));
    }
    const auto issues = media::validate(document);
    if (!issues.empty()) {
        return failure(format, QStringLiteral("clip-map-invalid"), validationMessage(issues));
    }
    return {.document = std::move(document), .format = format,
            .errorCode = {}, .error = {}, .warnings = {}};
}

ClipMapReadResult readDaphne(const QByteArray& bytes,
                             ClipMapFormat format,
                             std::optional<media::Rational> frameRate)
{
    QString text = QString::fromUtf8(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.size() < 2 || lines.front().isEmpty()) {
        return failure(format, QStringLiteral("daphne-framefile-invalid"),
                       QStringLiteral("A Daphne/Hypseus framefile needs a media-root line and at least one entry."));
    }
    media::ClipMapDocument document;
    document.mediaRoot = toUtf8(lines.front());
    document.frameRate = frameRate;
    const QRegularExpression entryPattern(
        QStringLiteral("^\\s*([+-]?\\d+)\\s+(\\S+)(?:\\s+.*)?$"));
    for (qsizetype lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
        const QString line = lines[lineIndex];
        if (line.trimmed().isEmpty()) continue;
        const auto match = entryPattern.match(line);
        if (!match.hasMatch()) {
            return failure(format, QStringLiteral("daphne-framefile-entry-invalid"),
                           QStringLiteral("Expected a signed frame number and filename on line %1.")
                               .arg(lineIndex + 1));
        }
        if (document.entries.size() >= static_cast<std::size_t>(maximumEntries)) {
            return failure(format, QStringLiteral("clip-map-too-many-entries"),
                           QStringLiteral("Clip map exceeds the entry safety limit."));
        }
        bool ok = false;
        media::ClipMapEntry entry;
        entry.startFrame = match.captured(1).toLongLong(&ok);
        if (!ok) {
            return failure(format, QStringLiteral("daphne-framefile-frame-invalid"),
                           QStringLiteral("Frame number is outside the supported range on line %1.")
                               .arg(lineIndex + 1));
        }
        entry.id = QStringLiteral("clip-%1").arg(document.entries.size() + 1, 4, 10,
                                                 QLatin1Char('0')).toStdString();
        entry.sourceLocator = toUtf8(match.captured(2));
        document.entries.push_back(std::move(entry));
    }
    const auto issues = media::validate(document);
    if (!issues.empty()) {
        return failure(format, QStringLiteral("daphne-framefile-invalid"), validationMessage(issues));
    }
    ClipMapReadResult result{.document = std::move(document), .format = format,
                             .errorCode = {}, .error = {}, .warnings = {}};
    if (!frameRate) {
        result.warnings.push_back(QStringLiteral(
            "Daphne/Hypseus framefiles do not declare FPS; supply --mapping-fps before time-based extraction."));
    }
    return result;
}

QString quotedField(QString value, QChar delimiter)
{
    if (!value.contains(delimiter) && !value.contains(QLatin1Char('"'))
        && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'))) {
        return value;
    }
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QByteArray delimitedBytes(const media::ClipMapDocument& document, QChar delimiter)
{
    QStringList lines;
    const auto meta = [&lines, delimiter](const QString& key, const std::string& value) {
        if (!value.empty()) {
            lines.push_back(key + delimiter + quotedField(fromUtf8(value), delimiter));
        }
    };
    meta(QStringLiteral("#name"), document.name);
    meta(QStringLiteral("#media_root"), document.mediaRoot);
    if (document.frameRate) {
        lines.push_back(QStringLiteral("#frame_rate") + delimiter
                        + QString::number(document.frameRate->numerator) + QLatin1Char('/')
                        + QString::number(document.frameRate->denominator));
    }
    meta(QStringLiteral("#default_recipe"), document.defaultRecipeLocator);
    meta(QStringLiteral("#default_target"), document.defaultTarget);
    lines.push_back(QStringList{QStringLiteral("id"), QStringLiteral("label"),
                                QStringLiteral("start_frame"), QStringLiteral("end_frame"),
                                QStringLiteral("source"), QStringLiteral("audio"),
                                QStringLiteral("recipe"), QStringLiteral("target")}
                        .join(delimiter));
    for (const auto& entry : document.entries) {
        QStringList fields{fromUtf8(entry.id), fromUtf8(entry.label),
                           QString::number(entry.startFrame),
                           entry.endFrame ? QString::number(*entry.endFrame) : QString{},
                           fromUtf8(entry.sourceLocator), fromUtf8(entry.audioLocator),
                           fromUtf8(entry.recipeLocator), fromUtf8(entry.target)};
        for (QString& field : fields) field = quotedField(field, delimiter);
        lines.push_back(fields.join(delimiter));
    }
    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

QByteArray jsonBytes(const media::ClipMapDocument& document)
{
    QJsonObject root{{QStringLiteral("kind"), QStringLiteral("retrovdp-clip-map")},
                     {QStringLiteral("schemaVersion"), 1},
                     {QStringLiteral("name"), fromUtf8(document.name)},
                     {QStringLiteral("mediaRoot"), fromUtf8(document.mediaRoot)},
                     {QStringLiteral("defaultRecipe"), fromUtf8(document.defaultRecipeLocator)},
                     {QStringLiteral("defaultTarget"), fromUtf8(document.defaultTarget)}};
    if (document.frameRate) root.insert(QStringLiteral("frameRate"), rationalJson(*document.frameRate));
    else root.insert(QStringLiteral("frameRate"), QJsonValue::Null);
    QJsonArray entries;
    for (const auto& entry : document.entries) {
        QJsonObject object{{QStringLiteral("id"), fromUtf8(entry.id)},
                           {QStringLiteral("label"), fromUtf8(entry.label)},
                           {QStringLiteral("startFrame"), static_cast<qint64>(entry.startFrame)},
                           {QStringLiteral("source"), fromUtf8(entry.sourceLocator)},
                           {QStringLiteral("audio"), fromUtf8(entry.audioLocator)},
                           {QStringLiteral("recipe"), fromUtf8(entry.recipeLocator)},
                           {QStringLiteral("target"), fromUtf8(entry.target)}};
        if (entry.endFrame) object.insert(QStringLiteral("endFrame"), static_cast<qint64>(*entry.endFrame));
        entries.push_back(object);
    }
    root.insert(QStringLiteral("entries"), entries);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace

std::optional<ClipMapFormat> clipMapFormat(const QString& name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("auto")) return ClipMapFormat::Auto;
    if (normalized == QStringLiteral("json") || normalized == QStringLiteral("native-json")) {
        return ClipMapFormat::RetroVdpJson;
    }
    if (normalized == QStringLiteral("csv")) return ClipMapFormat::Csv;
    if (normalized == QStringLiteral("tsv")) return ClipMapFormat::Tsv;
    if (normalized == QStringLiteral("daphne") || normalized == QStringLiteral("hypseus")
        || normalized == QStringLiteral("framefile")) {
        return ClipMapFormat::DaphneFramefile;
    }
    return std::nullopt;
}

QString clipMapFormatName(ClipMapFormat format)
{
    switch (format) {
    case ClipMapFormat::Auto: return QStringLiteral("auto");
    case ClipMapFormat::RetroVdpJson: return QStringLiteral("json");
    case ClipMapFormat::Csv: return QStringLiteral("csv");
    case ClipMapFormat::Tsv: return QStringLiteral("tsv");
    case ClipMapFormat::DaphneFramefile: return QStringLiteral("daphne");
    }
    return {};
}

ClipMapReadResult readClipMap(const QString& path,
                              ClipMapFormat format,
                              std::optional<media::Rational> frameRate)
{
    if (format == ClipMapFormat::Auto) format = detectedFormat(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(format, QStringLiteral("clip-map-open-failed"),
                       QStringLiteral("Could not open clip map: %1").arg(file.errorString()));
    }
    if (file.size() > maximumMapBytes) {
        return failure(format, QStringLiteral("clip-map-too-large"),
                       QStringLiteral("Clip map exceeds the 16 MiB safety limit."));
    }
    const QByteArray bytes = file.readAll();
    switch (format) {
    case ClipMapFormat::RetroVdpJson: return readJson(bytes, format);
    case ClipMapFormat::Csv: return readDelimited(bytes, format, QLatin1Char(','));
    case ClipMapFormat::Tsv: return readDelimited(bytes, format, QLatin1Char('\t'));
    case ClipMapFormat::DaphneFramefile: return readDaphne(bytes, format, frameRate);
    case ClipMapFormat::Auto: break;
    }
    return failure(format, QStringLiteral("clip-map-format-invalid"),
                   QStringLiteral("Unsupported clip-map input format."));
}

ClipMapWriteResult writeClipMap(const media::ClipMapDocument& document,
                                const QString& path,
                                ClipMapFormat format,
                                bool overwrite)
{
    if (format == ClipMapFormat::Auto) format = detectedFormat(path);
    if (!overwrite && QFileInfo::exists(path)) {
        return {.success = false, .outputPath = {},
                .errorCode = QStringLiteral("clip-map-output-exists"),
                .error = QStringLiteral("Clip-map output already exists: %1")
                             .arg(QFileInfo(path).absoluteFilePath()),
                .warnings = {}};
    }
    const auto issues = media::validate(document);
    if (!issues.empty()) {
        return {.success = false, .outputPath = {},
                .errorCode = QStringLiteral("clip-map-invalid"),
                .error = validationMessage(issues), .warnings = {}};
    }
    QByteArray bytes;
    QStringList warnings;
    if (format == ClipMapFormat::RetroVdpJson) {
        bytes = jsonBytes(document);
    } else if (format == ClipMapFormat::Csv || format == ClipMapFormat::Tsv) {
        bytes = delimitedBytes(document, format == ClipMapFormat::Csv
                                             ? QLatin1Char(',') : QLatin1Char('\t'));
    } else if (format == ClipMapFormat::DaphneFramefile) {
        if (document.mediaRoot.empty()) {
            return {.success = false, .outputPath = {},
                    .errorCode = QStringLiteral("daphne-media-root-missing"),
                    .error = QStringLiteral("Daphne/Hypseus export requires mediaRoot."),
                    .warnings = {}};
        }
        QString text = fromUtf8(document.mediaRoot) + QLatin1Char('\n');
        bool losesMetadata = document.frameRate.has_value()
            || !document.defaultRecipeLocator.empty() || !document.defaultTarget.empty();
        for (const auto& entry : document.entries) {
            const QString source = fromUtf8(entry.sourceLocator);
            if (source.contains(QRegularExpression(QStringLiteral("\\s")))) {
                return {.success = false, .outputPath = {},
                        .errorCode = QStringLiteral("daphne-source-invalid"),
                        .error = QStringLiteral("Daphne/Hypseus source names cannot contain whitespace: %1")
                                     .arg(source),
                        .warnings = {}};
            }
            text += QString::number(entry.startFrame) + QLatin1Char(' ') + source + QLatin1Char('\n');
            losesMetadata = losesMetadata || entry.endFrame.has_value() || !entry.label.empty()
                || !entry.audioLocator.empty() || !entry.recipeLocator.empty() || !entry.target.empty();
        }
        bytes = text.toUtf8();
        if (losesMetadata) {
            warnings.push_back(QStringLiteral(
                "Daphne/Hypseus framefiles store only mediaRoot, startFrame, and source; other fields were omitted."));
        }
    } else {
        return {.success = false, .outputPath = {},
                .errorCode = QStringLiteral("clip-map-format-invalid"),
                .error = QStringLiteral("Unsupported clip-map output format."),
                .warnings = {}};
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {.success = false, .outputPath = {},
                .errorCode = QStringLiteral("clip-map-write-failed"),
                .error = QStringLiteral("Could not create clip map: %1").arg(file.errorString()),
                .warnings = {}};
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return {.success = false, .outputPath = {},
                .errorCode = QStringLiteral("clip-map-write-failed"),
                .error = QStringLiteral("Could not commit clip map: %1").arg(file.errorString()),
                .warnings = {}};
    }
    return {.success = true, .outputPath = QFileInfo(path).absoluteFilePath(),
            .errorCode = {}, .error = {}, .warnings = std::move(warnings)};
}

QString resolveClipMapLocator(const QString& mapPath,
                              const media::ClipMapDocument& document,
                              const std::string& locator)
{
    const QString value = fromUtf8(locator);
    if (value.isEmpty() || QFileInfo(value).isAbsolute()) return QDir::cleanPath(value);
    QDir base = QFileInfo(mapPath).absoluteDir();
    const QString root = fromUtf8(document.mediaRoot);
    if (!root.isEmpty()) {
        const QString resolvedRoot = QFileInfo(root).isAbsolute()
            ? QDir::cleanPath(root) : base.absoluteFilePath(root);
        base = QDir(resolvedRoot);
    }
    return QDir::cleanPath(base.absoluteFilePath(value));
}

} // namespace retrovdp::appsupport
