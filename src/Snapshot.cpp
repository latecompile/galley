#include "Snapshot.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>

namespace {

// Short, synchronous git calls. Anything long-running goes through
// ProcessRunner instead.
// `acceptExit1` is for `diff --no-index`, where exit 1 means "there are
// differences" rather than failure.
bool git(const QString &root, const QStringList &args, QString *out, bool acceptExit1 = false)
{
    QProcess p;
    p.setWorkingDirectory(root);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForStarted(3000))
        return false;
    if (!p.waitForFinished(15000)) {
        p.kill();
        return false;
    }
    if (out)
        *out = QString::fromUtf8(p.readAll());
    if (p.exitStatus() != QProcess::NormalExit)
        return false;
    return p.exitCode() == 0 || (acceptExit1 && p.exitCode() == 1);
}

QString manifestPath(const QString &galleyDir, int roundNumber)
{
    return QDir(galleyDir).filePath(
        QStringLiteral("snapshots/%1.json").arg(roundNumber, 4, 10, QLatin1Char('0')));
}

QJsonObject hashTree(const QString &root)
{
    QJsonObject manifest;
    QDirIterator it(root, {QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    QDir base(root);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = base.relativeFilePath(path);
        if (rel.startsWith(QStringLiteral(".galley/")))
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(&f);
        manifest[rel] = QString::fromLatin1(h.result().toHex());
    }
    return manifest;
}

} // namespace

bool Snapshot::isGitRepo(const QString &root)
{
    QString out;
    return git(root, {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")}, &out)
        && out.trimmed() == QStringLiteral("true");
}

bool Snapshot::isDirty(const QString &root)
{
    QString out;
    if (!git(root, {QStringLiteral("status"), QStringLiteral("--porcelain"),
                    QStringLiteral("--"), QStringLiteral(".")}, &out))
        return false;
    return !out.trimmed().isEmpty();
}

QString Snapshot::capture(const QString &root, const QString &galleyDir, int roundNumber)
{
    // The manifest is written whether or not this is a repo: it is the only
    // record of the tree as the agent found it.
    const QJsonObject manifest = hashTree(root);
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Indented);

    const QString path = manifestPath(galleyDir, roundNumber);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(json);

    if (isGitRepo(root)) {
        // `stash create` writes a commit object for the working tree as it
        // stands and returns its sha. It moves no ref, touches no index and
        // changes no file — so Galley still writes nothing to the repository
        // — but it gives a base to diff against that is this round's own
        // starting point rather than the last commit. Without it, every round
        // between two commits reports the previous rounds' changes as well.
        QString stash;
        if (git(root, {QStringLiteral("stash"), QStringLiteral("create")}, &stash)
            && !stash.trimmed().isEmpty())
            return QStringLiteral("tree:%1").arg(stash.trimmed());

        // Nothing to stash means the tree is exactly HEAD.
        QString sha;
        if (git(root, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &sha))
            return QStringLiteral("git:%1").arg(sha.trimmed());
    }
    return QStringLiteral("hash:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex().left(16)));
}

QStringList Snapshot::changedFiles(const QString &root, const QString &galleyDir,
                                   int roundNumber)
{
    QFile f(manifestPath(galleyDir, roundNumber));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject before = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject after = hashTree(root);

    QStringList changed;
    for (auto it = after.begin(); it != after.end(); ++it)
        if (!before.contains(it.key()) || before.value(it.key()) != it.value())
            changed << it.key();
    for (auto it = before.begin(); it != before.end(); ++it)
        if (!after.contains(it.key()))
            changed << it.key();
    changed.sort();
    return changed;
}

QString Snapshot::diff(const QString &root, const QString &galleyDir,
                       const QString &snapshot, int roundNumber)
{
    const QStringList changed = changedFiles(root, galleyDir, roundNumber);
    if (changed.isEmpty())
        return QStringLiteral("No files changed.\n");

    if (isGitRepo(root)) {
        // A file the agent created is untracked, so it is invisible to
        // `git diff` no matter what it is diffed against. Adding it to the
        // index would make it visible and would also be a write to the
        // repository, which Galley does not do; `diff --no-index` shows it
        // without touching anything.
        QSet<QString> untracked;
        QString listing;
        if (git(root, {QStringLiteral("ls-files"), QStringLiteral("--others"),
                       QStringLiteral("--exclude-standard")}, &listing)) {
            const auto lines = listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &l : lines)
                untracked.insert(l.trimmed());
        }

        QStringList tracked;
        QStringList added;
        for (const QString &p : changed)
            (untracked.contains(p) ? added : tracked).append(p);

        QString out;

        if (!tracked.isEmpty()) {
            // Against the tree as this round found it, so the diff is the
            // round's own work and not everything since the last commit.
            const QString base = snapshot.section(QLatin1Char(':'), 1);
            QStringList args{QStringLiteral("diff")};
            if (!base.isEmpty()
                && (snapshot.startsWith(QStringLiteral("tree:"))
                    || snapshot.startsWith(QStringLiteral("git:"))))
                args << base;
            args << QStringLiteral("--");
            args += tracked;

            QString part;
            if (git(root, args, &part))
                out += part;
        }

        for (const QString &p : added) {
            QString part;
            if (git(root,
                    {QStringLiteral("diff"), QStringLiteral("--no-index"),
                     QStringLiteral("--"), QStringLiteral("/dev/null"), p},
                    &part, true))
                out += part;
        }

        if (!out.trimmed().isEmpty())
            return out;

        return QStringLiteral("%1 file(s) changed, but git produced no diff.\n")
            .arg(changed.size());
    }

    QString out;
    for (const QString &p : changed)
        out += QStringLiteral("changed  %1\n").arg(p);
    out += QStringLiteral("\n(No line-level diff without git.)\n");
    return out;
}
