#include "MediaToolDiscovery.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace retrovdp::appsupport {
namespace {

QString platformExecutableName(const QString& name)
{
#ifdef Q_OS_WIN
    return name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
        ? name : name + QStringLiteral(".exe");
#else
    return name;
#endif
}

QString normalizedExistingExecutable(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return {};
#ifndef Q_OS_WIN
    if (!info.isExecutable()) return {};
#endif
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString resolveExecutable(const QString& toolName, const QString& requested)
{
    if (!requested.trimmed().isEmpty()) {
        const QString explicitValue = QDir::fromNativeSeparators(requested.trimmed());
        const QFileInfo explicitInfo(explicitValue);
        if (explicitInfo.isAbsolute()
            || explicitValue.contains(QLatin1Char('/'))) {
            return normalizedExistingExecutable(explicitInfo.absoluteFilePath());
        }
        return normalizedExistingExecutable(
            QStandardPaths::findExecutable(explicitValue));
    }

    const QString besideApplication = QDir(QCoreApplication::applicationDirPath())
        .filePath(platformExecutableName(toolName));
    if (const QString resolved = normalizedExistingExecutable(besideApplication);
        !resolved.isEmpty()) {
        return resolved;
    }
    return normalizedExistingExecutable(QStandardPaths::findExecutable(toolName));
}

QString firstNonEmptyLine(const QByteArray& output)
{
    const QString text = QString::fromLocal8Bit(output);
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) return trimmed;
    }
    return {};
}

MediaExecutableInfo probeExecutable(const QString& name,
                                    const QString& requested,
                                    int timeoutMilliseconds)
{
    MediaExecutableInfo result;
    result.name = name;
    result.requestedPath = requested.trimmed();
    result.resolvedPath = resolveExecutable(name, requested);
    if (result.resolvedPath.isEmpty()) {
        result.error = result.requestedPath.isEmpty()
            ? QStringLiteral("%1 was not found beside the application or on PATH.")
                  .arg(name)
            : QStringLiteral("The configured %1 executable was not found: %2")
                  .arg(name, result.requestedPath);
        return result;
    }

    QProcess process;
    process.setProgram(result.resolvedPath);
    process.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-version")});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(timeoutMilliseconds)) {
        result.error = QStringLiteral("%1 could not be started: %2")
                           .arg(name, process.errorString());
        return result;
    }
    if (!process.waitForFinished(timeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("%1 did not answer the version check within %2 ms.")
                           .arg(name)
                           .arg(timeoutMilliseconds);
        return result;
    }

    const QString identity = firstNonEmptyLine(process.readAll());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QStringLiteral("%1 version check failed with exit code %2%3.")
                           .arg(name)
                           .arg(process.exitCode())
                           .arg(identity.isEmpty()
                                    ? QString()
                                    : QStringLiteral(": %1").arg(identity));
        return result;
    }
    if (!identity.startsWith(name + QStringLiteral(" version"),
                             Qt::CaseInsensitive)) {
        result.error = QStringLiteral(
            "The configured executable did not identify itself as %1: %2")
                           .arg(name, identity.isEmpty()
                                          ? QStringLiteral("no version output")
                                          : identity);
        return result;
    }

    result.version = identity;
    result.available = true;
    return result;
}

} // namespace

MediaToolStatus probeMediaTools(const QString& ffmpegOverride,
                                const QString& ffprobeOverride,
                                int timeoutMilliseconds)
{
    const int boundedTimeout = std::clamp(timeoutMilliseconds, 100, 30'000);
    return {
        .ffmpeg = probeMediaExecutable(QStringLiteral("ffmpeg"), ffmpegOverride,
                                       boundedTimeout),
        .ffprobe = probeMediaExecutable(QStringLiteral("ffprobe"), ffprobeOverride,
                                        boundedTimeout),
    };
}

MediaExecutableInfo probeMediaExecutable(const QString& name,
                                         const QString& executableOverride,
                                         int timeoutMilliseconds)
{
    if (name != QStringLiteral("ffmpeg") && name != QStringLiteral("ffprobe")) {
        MediaExecutableInfo result;
        result.name = name;
        result.requestedPath = executableOverride;
        result.error = QStringLiteral("Unsupported media executable name: %1").arg(name);
        return result;
    }
    return probeExecutable(name, executableOverride,
                           std::clamp(timeoutMilliseconds, 100, 30'000));
}

} // namespace retrovdp::appsupport
