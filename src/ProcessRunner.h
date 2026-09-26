#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

// Runs one external command — an agent CLI or a PDF build — streaming its
// output a line at a time so the log pane fills as it goes rather than
// arriving all at once at the end.
class ProcessRunner : public QObject
{
    Q_OBJECT

public:
    explicit ProcessRunner(QObject *parent = nullptr);

    bool running() const;
    void start(const QString &program, const QStringList &args, const QString &cwd);
    void cancel();

signals:
    void started(const QString &commandLine);
    void line(const QString &text);
    // Ticks every second while running. Agents that buffer their output emit
    // nothing at all until they are done, and silence must not be readable as
    // a hang.
    void heartbeat(int seconds);
    void finished(int exitCode, bool crashed);

private:
    void drain();

    QProcess m_proc;
    QByteArray m_buffer;
    QTimer m_ticker;
    QElapsedTimer m_since;
};
