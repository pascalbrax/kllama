#pragma once
#include <QByteArray>
#include <QPromise>
#include <QString>
#include <QVariantList>

struct IngestResult {
    QString text, hash, error, kind = "text", image, warning;
    QVariantList sources;
    qint64 size = 0;
    int width = 0, height = 0;
};
IngestResult extractFile(const QByteArray &bytes, const QString &path, QPromise<IngestResult> &promise);
