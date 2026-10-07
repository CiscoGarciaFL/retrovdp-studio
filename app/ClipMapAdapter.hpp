#pragma once

#include "retrovdp/media/ClipMap.hpp"

#include <QString>
#include <QStringList>

#include <optional>

namespace retrovdp::appsupport {

enum class ClipMapFormat {
    Auto,
    RetroVdpJson,
    Csv,
    Tsv,
    DaphneFramefile,
};

struct ClipMapReadResult {
    std::optional<media::ClipMapDocument> document;
    ClipMapFormat format{ClipMapFormat::Auto};
    QString errorCode;
    QString error;
    QStringList warnings;

    [[nodiscard]] explicit operator bool() const { return document.has_value(); }
};

struct ClipMapWriteResult {
    bool success{};
    QString outputPath;
    QString errorCode;
    QString error;
    QStringList warnings;

    [[nodiscard]] explicit operator bool() const { return success; }
};

[[nodiscard]] std::optional<ClipMapFormat> clipMapFormat(const QString& name);
[[nodiscard]] QString clipMapFormatName(ClipMapFormat format);

[[nodiscard]] ClipMapReadResult readClipMap(
    const QString& path,
    ClipMapFormat format = ClipMapFormat::Auto,
    std::optional<media::Rational> frameRate = std::nullopt);

[[nodiscard]] ClipMapWriteResult writeClipMap(
    const media::ClipMapDocument& document,
    const QString& path,
    ClipMapFormat format = ClipMapFormat::Auto,
    bool overwrite = false);

[[nodiscard]] QString resolveClipMapLocator(const QString& mapPath,
                                            const media::ClipMapDocument& document,
                                            const std::string& locator);

} // namespace retrovdp::appsupport
