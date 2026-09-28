#pragma once

#include <QObject>
#include <QJsonObject>

class QProcess;
class QTimer;

// Asks one installed agent CLI for the models it currently knows about.
// Refreshes are deliberately explicit: the UI reads the cache until the user
// presses Refresh, and the terminal's --models command is itself that request.
class ModelDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit ModelDiscovery(QObject *parent = nullptr);

    static QString cachePath();
    static QJsonObject cached(const QString &agent);
    // Pure parsing seam for CLI fixtures; errors retain the catalog shape.
    static QJsonObject parse(const QString &agent, const QByteArray &output);
    static QString format(const QJsonObject &catalog);

public slots:
    void refresh(const QString &agent);

signals:
    void finished(const QString &json);

private:
    void complete(const QJsonObject &catalog, bool save);

    QProcess *m_process;
    QTimer *m_timer;
    QString m_agent;
    bool m_done = true;
    bool m_timedOut = false;
};
