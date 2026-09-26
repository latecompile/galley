#include "ProcessRunner.h"

ProcessRunner::ProcessRunner(QObject *parent) : QObject(parent)
{
    m_proc.setProcessChannelMode(QProcess::MergedChannels);

    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this,
            [this] { emit heartbeat(int(m_since.elapsed() / 1000)); });

    connect(&m_proc, &QProcess::readyRead, this, &ProcessRunner::drain);
    connect(&m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_ticker.stop();
        drain();
        if (!m_buffer.isEmpty()) {
            emit line(QString::fromUtf8(m_buffer));
            m_buffer.clear();
        }
        emit finished(code, status == QProcess::CrashExit);
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_ticker.stop();
            emit line(QStringLiteral("galley: could not start %1 — is it on your PATH?")
                          .arg(m_proc.program()));
            emit finished(127, true);
        }
    });
}

bool ProcessRunner::running() const
{
    return m_proc.state() != QProcess::NotRunning;
}

void ProcessRunner::start(const QString &program, const QStringList &args, const QString &cwd)
{
    if (running())
        return;
    m_buffer.clear();
    m_proc.setWorkingDirectory(cwd);
    m_since.start();
    m_ticker.start();

    QStringList shown = args;
    for (QString &a : shown)
        if (a.contains(QLatin1Char(' ')))
            a = QLatin1Char('"') + a + QLatin1Char('"');
    emit started(program + QLatin1Char(' ') + shown.join(QLatin1Char(' ')));

    m_proc.start(program, args);

    // Agents that accept piped input block waiting for it. There is never any
    // here — the whole instruction is in argv and the brief is a file.
    m_proc.closeWriteChannel();
}

void ProcessRunner::cancel()
{
    if (!running())
        return;
    m_proc.terminate();
    if (!m_proc.waitForFinished(2000))
        m_proc.kill();
}

void ProcessRunner::drain()
{
    m_buffer.append(m_proc.readAll());
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray one = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (one.endsWith('\r'))
            one.chop(1);
        emit line(QString::fromUtf8(one));
    }
}
