#include "Round.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

#include <toml++/toml.hpp>

namespace {

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString roundDir(const QString &galleyDir, int number)
{
    return QDir(galleyDir).filePath(
        QStringLiteral("rounds/%1").arg(number, 4, 10, QLatin1Char('0')));
}

QString tstr(const toml::table &t, const char *key, const QString &fallback = {})
{
    if (auto v = t[key].value<std::string>())
        return QString::fromStdString(*v);
    return fallback;
}

} // namespace

QJsonObject Comment::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("file")] = file;
    o[QStringLiteral("block")] = block;
    o[QStringLiteral("quote")] = quote;
    o[QStringLiteral("start")] = start;
    o[QStringLiteral("end")] = end;
    o[QStringLiteral("scope")] = scope;
    o[QStringLiteral("note")] = note;
    o[QStringLiteral("refs")] = QJsonArray::fromStringList(refs);
    o[QStringLiteral("status")] = status;
    if (stale)
        o[QStringLiteral("stale")] = true;
    if (!agentStatus.isEmpty())
        o[QStringLiteral("agent_status")] = agentStatus;
    if (!agentNote.isEmpty())
        o[QStringLiteral("agent_note")] = agentNote;
    o[QStringLiteral("created_at")] = createdAt;
    if (!carriedFrom.isEmpty())
        o[QStringLiteral("carried_from")] = carriedFrom;
    if (sourceStart >= 0) {
        QJsonArray r;
        r << sourceStart << sourceEnd;
        o[QStringLiteral("source_range")] = r;
    }
    return o;
}

Comment Comment::fromJson(const QJsonObject &o)
{
    Comment c;
    c.id = o[QStringLiteral("id")].toString();
    c.file = o[QStringLiteral("file")].toString();
    c.block = o[QStringLiteral("block")].toInt(-1);
    c.quote = o[QStringLiteral("quote")].toString();
    c.start = o[QStringLiteral("start")].toInt();
    c.end = o[QStringLiteral("end")].toInt();
    c.scope = o[QStringLiteral("scope")].toString();
    c.note = o[QStringLiteral("note")].toString();
    const auto refs = o[QStringLiteral("refs")].toArray();
    for (const auto &r : refs)
        c.refs << r.toString();
    c.status = o[QStringLiteral("status")].toString(QStringLiteral("open"));
    c.stale = o[QStringLiteral("stale")].toBool();
    c.agentStatus = o[QStringLiteral("agent_status")].toString();
    c.agentNote = o[QStringLiteral("agent_note")].toString();
    c.createdAt = o[QStringLiteral("created_at")].toString();
    c.carriedFrom = o[QStringLiteral("carried_from")].toString();
    const auto sr = o[QStringLiteral("source_range")].toArray();
    if (sr.size() == 2) {
        c.sourceStart = sr[0].toInt();
        c.sourceEnd = sr[1].toInt();
    }
    return c;
}

QList<int> Round::allNumbers(const QString &galleyDir)
{
    QList<int> out;
    QDir d(QDir(galleyDir).filePath(QStringLiteral("rounds")));
    if (!d.exists())
        return out;
    const auto entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &e : entries) {
        bool ok = false;
        int n = e.toInt(&ok);
        if (ok)
            out << n;
    }
    std::sort(out.begin(), out.end());
    return out;
}

Round Round::load(const QString &galleyDir, int number)
{
    Round r;
    r.galleyDir = galleyDir;
    r.number = number;

    const QString d = roundDir(galleyDir, number);
    const QString meta = QDir(d).filePath(QStringLiteral("round.toml"));
    if (QFile::exists(meta)) {
        try {
            auto t = toml::parse_file(meta.toStdString());
            r.state = tstr(t, "state", r.state);
            r.snapshot = tstr(t, "snapshot");
            r.agent = tstr(t, "agent");
            r.openedAt = tstr(t, "opened_at");
            r.dispatchedAt = tstr(t, "dispatched_at");
            r.closedAt = tstr(t, "closed_at");
        } catch (const toml::parse_error &) {
            // A corrupt round file should not take the app down; the comments
            // beside it are the part that matters.
        }
    }

    QFile cf(QDir(d).filePath(QStringLiteral("comments.json")));
    if (cf.open(QIODevice::ReadOnly)) {
        const auto arr = QJsonDocument::fromJson(cf.readAll()).array();
        for (const auto &v : arr)
            r.comments << Comment::fromJson(v.toObject());
    }
    return r;
}

Round Round::loadOrCreateOpen(const QString &galleyDir)
{
    const QList<int> numbers = allNumbers(galleyDir);
    if (!numbers.isEmpty()) {
        Round last = load(galleyDir, numbers.last());
        if (last.isOpen())
            return last;
    }

    Round r;
    r.galleyDir = galleyDir;
    r.number = numbers.isEmpty() ? 1 : numbers.last() + 1;
    r.openedAt = nowIso();
    return r;
}

QString Round::dir() const { return roundDir(galleyDir, number); }
QString Round::briefPath() const { return QDir(dir()).filePath(QStringLiteral("brief.md")); }
QString Round::logPath() const { return QDir(dir()).filePath(QStringLiteral("agent.log")); }
QString Round::diffPath() const { return QDir(dir()).filePath(QStringLiteral("result.diff")); }
QString Round::reportPath() const { return QDir(dir()).filePath(QStringLiteral("applied.json")); }

int Round::nextCommentSeq() const
{
    int max = 0;
    for (const Comment &c : comments) {
        const QString n = c.id.mid(2);
        bool ok = false;
        int v = n.toInt(&ok);
        if (ok && v > max)
            max = v;
    }
    return max + 1;
}

QString Round::addComment(Comment c)
{
    c.id = QStringLiteral("c-%1").arg(nextCommentSeq(), 4, 10, QLatin1Char('0'));
    if (c.createdAt.isEmpty())
        c.createdAt = nowIso();
    comments.append(c);
    return c.id;
}

bool Round::updateComment(const QString &id, const QJsonObject &patch)
{
    for (Comment &c : comments) {
        if (c.id != id)
            continue;
        if (patch.contains(QStringLiteral("note")))
            c.note = patch[QStringLiteral("note")].toString();
        if (patch.contains(QStringLiteral("scope")))
            c.scope = patch[QStringLiteral("scope")].toString();
        if (patch.contains(QStringLiteral("status")))
            c.status = patch[QStringLiteral("status")].toString();
        if (patch.contains(QStringLiteral("refs"))) {
            c.refs.clear();
            const auto refs = patch[QStringLiteral("refs")].toArray();
            for (const auto &r : refs)
                c.refs << r.toString();
        }
        return true;
    }
    return false;
}

bool Round::removeComment(const QString &id)
{
    for (int i = 0; i < comments.size(); ++i) {
        if (comments.at(i).id == id) {
            comments.removeAt(i);
            return true;
        }
    }
    return false;
}

const Comment *Round::find(const QString &id) const
{
    for (const Comment &c : comments)
        if (c.id == id)
            return &c;
    return nullptr;
}

int Round::openCount() const
{
    int n = 0;
    for (const Comment &c : comments)
        if (c.status == QStringLiteral("open") || c.status == QStringLiteral("carried"))
            ++n;
    return n;
}

QJsonObject Round::toJson() const
{
    QJsonArray arr;
    for (const Comment &c : comments)
        arr << c.toJson();

    QJsonObject o;
    o[QStringLiteral("number")] = number;
    o[QStringLiteral("state")] = state;
    o[QStringLiteral("snapshot")] = snapshot;
    o[QStringLiteral("agent")] = agent;
    o[QStringLiteral("opened_at")] = openedAt;
    o[QStringLiteral("dispatched_at")] = dispatchedAt;
    o[QStringLiteral("closed_at")] = closedAt;
    o[QStringLiteral("comments")] = arr;
    return o;
}

bool Round::save(QString *error) const
{
    const QString d = dir();
    if (!QDir().mkpath(d)) {
        *error = QStringLiteral("cannot create %1").arg(d);
        return false;
    }

    QString meta;
    QTextStream ts(&meta);
    ts << "number    = " << number << "\n";
    ts << "state     = \"" << state << "\"\n";
    ts << "opened_at = \"" << openedAt << "\"\n";
    if (!snapshot.isEmpty())
        ts << "snapshot  = \"" << snapshot << "\"\n";
    if (!agent.isEmpty())
        ts << "agent     = \"" << agent << "\"\n";
    if (!dispatchedAt.isEmpty())
        ts << "dispatched_at = \"" << dispatchedAt << "\"\n";
    if (!closedAt.isEmpty())
        ts << "closed_at = \"" << closedAt << "\"\n";
    ts.flush();

    QFile mf(QDir(d).filePath(QStringLiteral("round.toml")));
    if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write round.toml: %1").arg(mf.errorString());
        return false;
    }
    mf.write(meta.toUtf8());
    mf.close();

    QJsonArray arr;
    for (const Comment &c : comments)
        arr << c.toJson();

    QFile cf(QDir(d).filePath(QStringLiteral("comments.json")));
    if (!cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write comments.json: %1").arg(cf.errorString());
        return false;
    }
    cf.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}
