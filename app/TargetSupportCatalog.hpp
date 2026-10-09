#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class TargetSupportCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap catalog READ catalog CONSTANT)
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(QVariantList targets READ targets CONSTANT)
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)

public:
    explicit TargetSupportCatalog(const QString& catalogPath,
                                  QObject* parent = nullptr);

    QVariantMap catalog() const;
    QVariantList categories() const;
    QVariantList targets() const;
    bool ready() const;
    QString errorMessage() const;

private:
    QVariantMap catalog_;
    QVariantList categories_;
    QVariantList targets_;
    QString errorMessage_;
};
