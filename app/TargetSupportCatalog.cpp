#include "TargetSupportCatalog.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

TargetSupportCatalog::TargetSupportCatalog(const QString& catalogPath,
                                           QObject* parent)
    : QObject(parent)
{
    QFile catalogFile(catalogPath);
    if (!catalogFile.open(QIODevice::ReadOnly)) {
        errorMessage_ = tr("Unable to open the bundled target catalog: %1")
                            .arg(catalogFile.errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        catalogFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        errorMessage_ = tr("The bundled target catalog is invalid: %1")
                            .arg(parseError.errorString());
        return;
    }
    if (!document.isObject()) {
        errorMessage_ = tr("The bundled target catalog must contain a JSON object.");
        return;
    }

    const QJsonObject root = document.object();
    const QJsonValue targetsValue = root.value(QStringLiteral("targets"));
    if (!targetsValue.isArray()) {
        errorMessage_ = tr("The bundled target catalog has no targets array.");
        return;
    }

    catalog_ = root.toVariantMap();
    targets_ = targetsValue.toArray().toVariantList();
}

QVariantMap TargetSupportCatalog::catalog() const
{
    return catalog_;
}

QVariantList TargetSupportCatalog::targets() const
{
    return targets_;
}

bool TargetSupportCatalog::ready() const
{
    return errorMessage_.isEmpty();
}

QString TargetSupportCatalog::errorMessage() const
{
    return errorMessage_;
}
