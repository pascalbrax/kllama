#pragma once
#include <QObject>
class MarkdownRenderer : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString render(const QString &markdown) const;
    Q_INVOKABLE bool openLink(const QString &link) const;
};
