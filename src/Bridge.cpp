#include "Bridge.h"

#include "AgentProfiles.h"
#include "Brief.h"
#include "ModelDiscovery.h"
#include "PdfExporter.h"
#include "ProcessRunner.h"
#include "Snapshot.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QDesktopServices>
#include <QProcess>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace {

QString toJson(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString toJson(const QJsonArray &a)
{
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QJsonObject parse(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

// What the agent said it did, keyed by comment id. Only these three answers
// mean anything; anything else is treated as no answer at all.
QHash<QString, QPair<QString, QString>> readReport(const QString &path)
{
    QHash<QString, QPair<QString, QString>> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        QString status = entry[QStringLiteral("status")].toString().toLower();
        if (status != QStringLiteral("applied") && status != QStringLiteral("partial")
            && status != QStringLiteral("declined"))
            continue;
        out.insert(it.key(), {status, entry[QStringLiteral("note")].toString()});
    }
    return out;
}

// The selection a dispatch was given. An empty list means everything.
QStringList idsFrom(const QString &json)
{
    QStringList out;
    const auto arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto &v : arr)
        out << v.toString();
    return out;
}

// Hands a file or a link to the desktop, detached and with its output thrown
// away. QDesktopServices starts the viewer as a child that inherits Galley's
// stdout and stderr, so whatever GTK has to say about its own widgets ends up
// looking like Galley said it.
void openDetached(const QUrl &target)
{
    QProcess launcher;
    launcher.setProgram(QStringLiteral("xdg-open"));
    launcher.setArguments({target.isLocalFile() ? target.toLocalFile() : target.toString()});
    launcher.setStandardOutputFile(QProcess::nullDevice());
    launcher.setStandardErrorFile(QProcess::nullDevice());
    if (launcher.startDetached())
        return;

    // No xdg-open, or it would not start: let Qt work it out, noise and all.
    openDetached(target);
}

// What the book says, else what the desktop says, else whatever there is.
// Only ever a name there is actually a profile for.
QString resolveAgent(const QMap<QString, AgentProfile> &profiles, const QString &fromProject)
{
    if (profiles.contains(fromProject))
        return fromProject;
    const QString desktop = AgentProfiles::omarchyDefault();
    if (profiles.contains(desktop))
        return desktop;
    return profiles.isEmpty() ? QString() : profiles.firstKey();
}

QString prettyDuration(int seconds)
{
    if (seconds < 60)
        return QStringLiteral("%1s").arg(seconds);
    return QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

Bridge::Bridge(Project *project, Theme *theme, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_theme(theme)
    , m_agent(new ProcessRunner(this))
    , m_models(new ModelDiscovery(this))
    , m_pdf(new PdfExporter(this))
{
    m_round = Round::loadOrCreateOpen(m_project->galleyDir());

    connect(m_theme, &Theme::changed, this, [this](const QJsonObject &o) {
        emit themeChanged(toJson(o));
    });
    connect(m_models, &ModelDiscovery::finished, this, &Bridge::modelsRefreshed);

    for (ProcessRunner *r : {m_agent}) {
        connect(r, &ProcessRunner::started, this, [this](const QString &cmd) {
            m_log.clear();
            m_secsAtLastLine = 0;
            m_log << QStringLiteral("$ %1").arg(cmd);
            emit runStarted(cmd);
            emit logLine(m_log.last());
        });
        connect(r, &ProcessRunner::line, this, [this](const QString &l) {
            m_log << l;
            emit logLine(l);
        });
        // `claude -p` and friends buffer everything until they finish, so a
        // correct run and a wedged one look identical. Say so periodically.
        connect(r, &ProcessRunner::heartbeat, this, [this](int secs) {
            emit runProgress(secs, prettyDuration(secs));
            if (secs - m_secsAtLastLine < 30)
                return;
            m_secsAtLastLine = secs;
            const QString note =
                QStringLiteral("… still running (%1), no output yet — many agents "
                               "print nothing until they finish")
                    .arg(prettyDuration(secs));
            m_log << note;
            emit logLine(note);
        });
    }

    connect(m_pdf, &PdfExporter::finished, this, [this](bool ok, const QString &path) {
        emit runFinished(ok ? 0 : 1, QString());
        if (!ok) {
            emit status(QStringLiteral("error"), QStringLiteral("Could not write the proof PDF."));
            return;
        }
        emit status(QStringLiteral("ok"),
                    QStringLiteral("Proof written to %1")
                        .arg(QDir(m_project->root()).relativeFilePath(path)));
        openDetached(QUrl::fromLocalFile(path));
    });

    connect(m_agent, &ProcessRunner::finished, this, &Bridge::onAgentFinished);
}

Document *Bridge::document(const QString &relPath)
{
    if (auto *d = m_docs.value(relPath))
        return d;
    auto *d = new Document;
    if (!d->load(m_project->absolutePath(relPath), relPath)) {
        emit status(QStringLiteral("error"), d->error());
        delete d;
        return nullptr;
    }
    m_docs.insert(relPath, d);
    return d;
}

void Bridge::persist()
{
    QString err;
    if (!m_round.save(&err))
        emit status(QStringLiteral("error"), err);
    emit roundChanged(roundJson());
}

QString Bridge::projectJson()
{
    QJsonArray chapters;
    int i = 0;
    for (const Chapter &c : m_project->chapters()) {
        chapters << QJsonObject{
            {QStringLiteral("index"), i++},
            {QStringLiteral("file"), c.relPath},
            {QStringLiteral("title"), c.title},
        };
    }

    QJsonArray agents;
    QJsonObject agentLabels;
    const auto profiles = AgentProfiles::load();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        agents << it.key();
        agentLabels[it.key()] = AgentProfiles::label(it.value());
    }

    const QString chosen = resolveAgent(profiles, m_project->defaultAgent());

    return toJson(QJsonObject{
        {QStringLiteral("name"), m_project->name()},
        {QStringLiteral("root"), m_project->root()},
        {QStringLiteral("chapters"), chapters},
        {QStringLiteral("agents"), agents},
        {QStringLiteral("agentLabels"), agentLabels},
        {QStringLiteral("defaultAgent"), chosen},
        {QStringLiteral("desktopAgent"), AgentProfiles::omarchyDefault()},
        {QStringLiteral("references"), QJsonArray::fromStringList(m_project->references())},
        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("imported"), m_project->wasImported()},
        {QStringLiteral("spineSource"), m_project->spineSource()},
        {QStringLiteral("spineGuessed"), m_project->spineGuessed()},
    });
}

QString Bridge::chapterJson(int index)
{
    const auto &chapters = m_project->chapters();
    if (index < 0 || index >= chapters.size())
        return toJson(QJsonObject{{QStringLiteral("error"), QStringLiteral("no such chapter")}});

    const Chapter &ch = chapters.at(index);
    Document *d = document(ch.relPath);
    if (!d)
        return toJson(QJsonObject{{QStringLiteral("error"), QStringLiteral("cannot render")}});

    return toJson(QJsonObject{
        {QStringLiteral("index"), index},
        {QStringLiteral("file"), ch.relPath},
        {QStringLiteral("title"), d->title()},
        {QStringLiteral("html"), d->html()},
        {QStringLiteral("blockCount"), d->blockCount()},
        {QStringLiteral("mapped"), d->blocksMapped()},
        {QStringLiteral("notices"), QJsonArray::fromStringList(d->notices())},
    });
}

QString Bridge::roundJson()
{
    QJsonObject o = m_round.toJson();
    o[QStringLiteral("openCount")] = m_round.openCount();
    return toJson(o);
}

QString Bridge::themeJson()
{
    return toJson(m_theme->json());
}

QString Bridge::historyJson()
{
    QJsonArray out;
    for (int n : Round::allNumbers(m_project->galleyDir())) {
        Round r = Round::load(m_project->galleyDir(), n);
        out << QJsonObject{
            {QStringLiteral("number"), r.number},
            {QStringLiteral("state"), r.state},
            {QStringLiteral("agent"), r.agent},
            {QStringLiteral("openedAt"), r.openedAt},
            {QStringLiteral("closedAt"), r.closedAt},
            {QStringLiteral("count"), int(r.comments.size())},
            {QStringLiteral("comments"), r.toJson()[QStringLiteral("comments")]},
        };
    }
    return toJson(out);
}

QString Bridge::preflightJson()
{
    const bool repo = Snapshot::isGitRepo(m_project->root());
    return toJson(QJsonObject{
        {QStringLiteral("comments"), m_round.openCount()},
        {QStringLiteral("gitRepo"), repo},
        {QStringLiteral("dirty"), repo && Snapshot::isDirty(m_project->root())},
        {QStringLiteral("running"), m_agent->running() || m_pdf->busy()},
        {QStringLiteral("round"), m_round.number},
    });
}

void Bridge::setProjectReferences(const QString &json)
{
    QStringList refs;
    const auto arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto &v : arr)
        refs << v.toString();

    QString error;
    if (!m_project->writeReferences(refs, &error)) {
        emit status(QStringLiteral("error"), error);
        return;
    }
    emit bookChanged(projectJson());
}

QString Bridge::rescanAgents()
{
    const QStringList added = AgentProfiles::rescan();
    QJsonArray names = QJsonArray::fromStringList(added);
    return toJson(QJsonObject{{QStringLiteral("added"), names},
                              {QStringLiteral("path"), AgentProfiles::configPath()}});
}

QString Bridge::modelPickerJson()
{
    QJsonArray agents;
    for (const QString &name : AgentProfiles::installedModelAgents()) {
        agents << QJsonObject{{QStringLiteral("name"), name},
                              {QStringLiteral("catalog"), ModelDiscovery::cached(name)}};
    }
    return toJson(QJsonObject{{QStringLiteral("agents"), agents},
                              {QStringLiteral("cachePath"), ModelDiscovery::cachePath()}});
}

void Bridge::refreshModels(const QString &agent)
{
    m_models->refresh(agent);
}

QString Bridge::modelProfileName(const QString &agent, const QString &model,
                                 const QString &effort)
{
    bool exists = false;
    QString error;
    const QString name = AgentProfiles::modelProfileName(agent, model.trimmed(),
                                                          effort.trimmed(), &exists, &error);
    if (name.isEmpty())
        return toJson(QJsonObject{{QStringLiteral("error"), error}});
    return toJson(QJsonObject{{QStringLiteral("name"), name},
                              {QStringLiteral("exists"), exists}});
}

QString Bridge::addModelProfile(const QString &agent, const QString &model,
                                const QString &effort)
{
    bool existed = false;
    QString error;
    const QString name = AgentProfiles::addModel(agent, model, effort, &existed, &error);
    if (name.isEmpty())
        return toJson(QJsonObject{{QStringLiteral("ok"), false},
                                  {QStringLiteral("error"), error}});

    const AgentProfile profile = AgentProfiles::load().value(name);
    emit bookChanged(projectJson());
    return toJson(QJsonObject{{QStringLiteral("ok"), true},
                              {QStringLiteral("name"), name},
                              {QStringLiteral("label"), AgentProfiles::label(profile)},
                              {QStringLiteral("existed"), existed},
                              {QStringLiteral("path"), AgentProfiles::configPath()}});
}

QString Bridge::briefPreview(const QString &idsJson)
{
    return Brief::generate(*m_project, m_round, idsFrom(idsJson));
}

QString Bridge::sourceForBlock(const QString &file, int block)
{
    Document *d = document(file);
    if (!d)
        return {};
    return QString::fromUtf8(d->sourceForBlock(block));
}

namespace {

// A window of text around the hit, cut at word boundaries so a snippet does
// not begin mid-word.
QJsonObject snippetFor(const QString &text, int at, int len)
{
    const int pad = 60;
    int from = qMax(0, at - pad);
    int to = qMin(text.size(), at + len + pad);
    while (from > 0 && !text.at(from - 1).isSpace())
        --from;
    while (to < text.size() && !text.at(to).isSpace())
        ++to;

    return QJsonObject{
        {QStringLiteral("before"),
         (from > 0 ? QStringLiteral("…") : QString()) + text.mid(from, at - from)},
        {QStringLiteral("match"), text.mid(at, len)},
        {QStringLiteral("after"),
         text.mid(at + len, to - at - len) + (to < text.size() ? QStringLiteral("…") : QString())},
    };
}

} // namespace

QString Bridge::search(const QString &query)
{
    const QString needle = query.trimmed();
    QJsonArray chapters;
    int total = 0;

    if (needle.size() < 2)
        return toJson(QJsonObject{{QStringLiteral("query"), needle},
                                  {QStringLiteral("total"), 0},
                                  {QStringLiteral("chapters"), chapters}});

    const auto &all = m_project->chapters();
    for (int i = 0; i < all.size() && total < 300; ++i) {
        Document *d = document(all.at(i).relPath);
        if (!d)
            continue;

        QJsonArray hits;
        const QStringList blocks = d->blockText();
        for (int b = 0; b < blocks.size() && total < 300; ++b) {
            const QString &text = blocks.at(b);
            int at = text.indexOf(needle, 0, Qt::CaseInsensitive);
            while (at >= 0 && total < 300) {
                QJsonObject hit = snippetFor(text, at, needle.size());
                hit[QStringLiteral("block")] = b;
                hits << hit;
                ++total;
                // One hit per block is enough to find your way there; more
                // would bury the other chapters.
                break;
            }
            Q_UNUSED(at);
        }

        if (!hits.isEmpty())
            chapters << QJsonObject{
                {QStringLiteral("index"), i},
                {QStringLiteral("file"), all.at(i).relPath},
                {QStringLiteral("title"), all.at(i).title},
                {QStringLiteral("hits"), hits},
            };
    }

    return toJson(QJsonObject{{QStringLiteral("query"), needle},
                              {QStringLiteral("total"), total},
                              {QStringLiteral("chapters"), chapters}});
}

QString Bridge::chapterSource(int index)
{
    const auto &chapters = m_project->chapters();
    if (index < 0 || index >= chapters.size())
        return toJson(QJsonObject{{QStringLiteral("error"), QStringLiteral("no such chapter")}});

    const QString path = m_project->absolutePath(chapters.at(index).relPath);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return toJson(QJsonObject{{QStringLiteral("error"), f.errorString()}});

    return toJson(QJsonObject{
        {QStringLiteral("index"), index},
        {QStringLiteral("file"), chapters.at(index).relPath},
        {QStringLiteral("text"), QString::fromUtf8(f.readAll())},
        // Carried back on save, so an edit cannot silently overwrite a change
        // made to the same file from somewhere else in the meantime.
        {QStringLiteral("seenAt"), QFileInfo(path).lastModified().toString(Qt::ISODateWithMs)},
        {QStringLiteral("writable"), !(m_agent->running() || m_pdf->busy())},
    });
}

QString Bridge::saveChapter(int index, const QString &text, const QString &seenAt)
{
    const auto &chapters = m_project->chapters();
    if (index < 0 || index >= chapters.size())
        return toJson(QJsonObject{{QStringLiteral("error"), QStringLiteral("no such chapter")}});

    // An agent is editing these very files; two writers would lose work.
    if (m_agent->running() || m_pdf->busy())
        return toJson(QJsonObject{
            {QStringLiteral("error"),
             QStringLiteral("something is running — wait for it to finish")}});

    const QString rel = chapters.at(index).relPath;
    const QString path = m_project->absolutePath(rel);

    QFileInfo info(path);
    if (info.exists() && !seenAt.isEmpty()
        && info.lastModified().toString(Qt::ISODateWithMs) != seenAt)
        return toJson(QJsonObject{
            {QStringLiteral("error"),
             QStringLiteral("%1 changed outside Galley since you opened it — "
                            "reopen the chapter before saving").arg(rel)}});

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return toJson(QJsonObject{{QStringLiteral("error"), f.errorString()}});
    // Text files end with a newline. Without this every save the author makes
    // shows up in the next round's diff as a spurious last-line change.
    QString body = text;
    if (!body.isEmpty() && !body.endsWith(QLatin1Char('\n')))
        body.append(QLatin1Char('\n'));
    f.write(body.toUtf8());
    f.close();

    // The chapter has to be re-read before anything is said about it.
    if (Document *old = m_docs.take(rel))
        delete old;
    Document *d = document(rel);
    if (!d)
        return toJson(QJsonObject{{QStringLiteral("error"), QStringLiteral("cannot re-render")}});

    // A comment quotes rendered text. If that text is nowhere in the chapter
    // any more, the comment no longer points at anything — usually because
    // the author has just fixed it by hand. Whitespace is normalised on both
    // sides because a selection and a re-render need not agree about it.
    const QString haystack = d->plainText().simplified();
    QJsonArray nowStale;
    bool touched = false;
    for (Comment &c : m_round.comments) {
        if (c.file != rel || c.quote.isEmpty())
            continue;
        if (c.status != QStringLiteral("open") && c.status != QStringLiteral("carried"))
            continue;
        const bool gone = !haystack.contains(c.quote.simplified());
        if (gone != c.stale) {
            c.stale = gone;
            touched = true;
        }
        if (gone)
            nowStale << c.toJson();
    }
    if (touched)
        persist();

    return toJson(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("file"), rel},
        {QStringLiteral("seenAt"), QFileInfo(path).lastModified().toString(Qt::ISODateWithMs)},
        {QStringLiteral("stale"), nowStale},
    });
}

void Bridge::refreshStale(Round &round)
{
    QHash<QString, QString> textOf;
    for (Comment &c : round.comments) {
        if (c.quote.isEmpty() || c.file.isEmpty())
            continue;
        if (c.status != QStringLiteral("open") && c.status != QStringLiteral("carried"))
            continue;
        if (!textOf.contains(c.file)) {
            Document *d = document(c.file);
            textOf.insert(c.file, d ? d->plainText().simplified() : QString());
        }
        const QString hay = textOf.value(c.file);
        c.stale = !hay.isEmpty() && !hay.contains(c.quote.simplified());
    }
}

void Bridge::openBook()
{
    if (m_agent->running() || m_pdf->busy()) {
        emit status(QStringLiteral("error"), QStringLiteral("something is already running"));
        return;
    }

    QSettings settings;
    forever {
        const QString picked = QFileDialog::getExistingDirectory(
            m_dialogParent, QStringLiteral("Open a book — a folder of Markdown files"),
            QFileInfo(m_project->root()).absolutePath());
        if (picked.isEmpty())
            return;
        if (picked == m_project->root())
            return;  // already looking at it

        QString error;
        if (m_project->open(picked, &error)) {
            settings.setValue(QStringLiteral("lastBook"), m_project->root());
            if (m_project->wasImported()) {
                QString saveError;
                if (!m_project->save(&saveError))
                    emit status(QStringLiteral("error"), saveError);
            }

            // Everything cached belongs to the book that just went away.
            qDeleteAll(m_docs);
            m_docs.clear();
            m_round = Round::loadOrCreateOpen(m_project->galleyDir());
            m_lastResultRound = 0;

            emit bookChanged(projectJson());
            emit roundChanged(roundJson());
            return;
        }
        QMessageBox::warning(m_dialogParent, QStringLiteral("Galley"), error);
    }
}

QString Bridge::pickReferences()
{
    const QStringList picked = QFileDialog::getOpenFileNames(
        m_dialogParent, QStringLiteral("Reference material"), m_project->root());

    // Relative to the book where possible, so the stored comment stays
    // portable and means the same thing on another machine.
    QJsonArray out;
    const QDir root(m_project->root());
    for (const QString &p : picked) {
        const QString rel = root.relativeFilePath(p);
        out << (rel.startsWith(QStringLiteral("../../")) ? p : rel);
    }
    return toJson(out);
}

QString Bridge::checkReferences(const QString &json)
{
    QJsonArray out;
    const auto refs = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto &v : refs) {
        const QString ref = v.toString();
        QString kind = QStringLiteral("missing");
        if (ref.startsWith(QStringLiteral("http://")) || ref.startsWith(QStringLiteral("https://"))) {
            kind = QStringLiteral("link");
        } else {
            const QFileInfo info(m_project->absolutePath(ref));
            if (info.isDir())
                kind = QStringLiteral("dir");
            else if (info.exists())
                kind = QStringLiteral("file");
        }
        out << QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("kind"), kind}};
    }
    return toJson(out);
}

QString Bridge::addComment(const QString &json)
{
    const QJsonObject o = parse(json);
    Comment c = Comment::fromJson(o);
    c.status = QStringLiteral("open");

    // Attach the source byte range when this file's block mapping held, so
    // the brief can show the agent exact Markdown rather than rendered text.
    if (Document *d = document(c.file); d && d->blocksMapped() && c.block >= 0) {
        const QByteArray src = d->sourceForBlock(c.block);
        if (!src.isEmpty()) {
            QFile f(m_project->absolutePath(c.file));
            if (f.open(QIODevice::ReadOnly)) {
                const int at = f.readAll().indexOf(src);
                if (at >= 0) {
                    c.sourceStart = at;
                    c.sourceEnd = at + src.size();
                }
            }
        }
    }

    const QString id = m_round.addComment(c);
    persist();
    return id;
}

void Bridge::updateComment(const QString &id, const QString &json)
{
    if (m_round.updateComment(id, parse(json)))
        persist();
}

void Bridge::deleteComment(const QString &id)
{
    if (m_round.removeComment(id))
        persist();
}

void Bridge::setCommentStatus(const QString &id, const QString &statusValue)
{
    QJsonObject patch{{QStringLiteral("status"), statusValue}};
    if (m_round.updateComment(id, patch))
        persist();
}

void Bridge::carryForward(int fromRound, const QString &id)
{
    Round src = Round::load(m_project->galleyDir(), fromRound);
    const Comment *orig = src.find(id);
    if (!orig) {
        emit status(QStringLiteral("error"),
                    QStringLiteral("no comment %1 in round %2").arg(id).arg(fromRound));
        return;
    }
    Comment c = *orig;
    c.carriedFrom = QStringLiteral("%1/%2").arg(fromRound).arg(id);
    c.status = QStringLiteral("carried");
    c.createdAt.clear();
    // Deliberately keeps the quote and drops nothing else: the verbatim text
    // is what lets the agent find the passage after a rewrite moved it.
    m_round.addComment(c);
    persist();
    emit status(QStringLiteral("ok"),
                QStringLiteral("Carried %1 forward into round %2").arg(id).arg(m_round.number));
}

void Bridge::dispatch(const QString &agentName, const QString &idsJson)
{
    const QStringList only = idsFrom(idsJson);

    if (m_agent->running() || m_pdf->busy()) {
        emit status(QStringLiteral("error"), QStringLiteral("something is already running"));
        return;
    }
    if (m_round.openCount() == 0) {
        emit status(QStringLiteral("error"), QStringLiteral("no comments in this round"));
        return;
    }

    const auto profiles = AgentProfiles::load();
    const QString wanted =
        agentName.isEmpty() ? resolveAgent(profiles, m_project->defaultAgent()) : agentName;
    if (!profiles.contains(wanted)) {
        emit status(QStringLiteral("error"),
                    QStringLiteral("no agent profile named '%1' in %2")
                        .arg(wanted, AgentProfiles::configPath()));
        return;
    }

    m_round.snapshot = Snapshot::capture(m_project->root(), m_project->galleyDir(), m_round.number);
    m_round.agent = wanted;
    m_round.dispatchedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_round.state = QStringLiteral("dispatched");
    // Only what was actually sent is marked sent. The rest stay open and are
    // carried into the next round when this one closes.
    for (Comment &c : m_round.comments) {
        if (c.status != QStringLiteral("open") && c.status != QStringLiteral("carried"))
            continue;
        if (only.isEmpty() || only.contains(c.id))
            c.status = QStringLiteral("sent");
    }

    QString err;
    if (!Brief::write(*m_project, m_round, only, &err)) {
        emit status(QStringLiteral("error"), err);
        return;
    }
    if (!m_round.save(&err)) {
        emit status(QStringLiteral("error"), err);
        return;
    }
    emit roundChanged(roundJson());

    const QString briefRel = QDir(m_project->root()).relativeFilePath(m_round.briefPath());
    const QString prompt =
        QStringLiteral("Apply the editorial review in %1. Edit the Markdown files in "
                       "place. Do not commit.")
            .arg(briefRel);

    const QStringList argv = AgentProfiles::expand(profiles.value(wanted), prompt,
                                                   briefRel, m_project->root());
    m_dispatchedRound = m_round.number;
    m_agent->start(argv.first(), argv.mid(1), m_project->root());
}

void Bridge::onAgentFinished(int exitCode, bool crashed)
{
    Round r = Round::load(m_project->galleyDir(), m_dispatchedRound);

    QFile lf(r.logPath());
    if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        lf.write((m_log.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());

    // Whether this run changed anything is a question for the manifest taken
    // at dispatch, not for the diff text: the tree may well have differed
    // from HEAD before the agent ever started.
    const QStringList touched =
        Snapshot::changedFiles(m_project->root(), m_project->galleyDir(), r.number);
    const QString diff = Snapshot::diff(m_project->root(), m_project->galleyDir(),
                                        r.snapshot, r.number);
    QFile df(r.diffPath());
    if (df.open(QIODevice::WriteOnly | QIODevice::Truncate))
        df.write(diff.toUtf8());

    // The agent is the only party that knows which change answered which
    // comment, so it was asked to say. Its answers are merged either way:
    // "declined, because the claim is correct" is worth keeping even when the
    // round stays open.
    const auto report = readReport(r.reportPath());
    for (Comment &c : r.comments) {
        if (!report.contains(c.id))
            continue;
        c.agentStatus = report.value(c.id).first;
        c.agentNote = report.value(c.id).second;
    }

    QString err;
    const bool exited = (exitCode == 0 && !crashed);
    const bool changed = !touched.isEmpty();
    const bool ok = exited && changed;

    if (ok) {
        // A comment the agent said nothing about stays "sent", which means
        // nobody knows what happened to it — not that it was applied.
        for (Comment &c : r.comments)
            if (c.status == QStringLiteral("sent") && !c.agentStatus.isEmpty())
                c.status = c.agentStatus;

        r.state = QStringLiteral("closed");
        r.closedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        r.save(&err);

        // The rewrite invalidates every rendered chapter. Dropping the cache
        // is the whole of the "re-anchor" story: the next read is a new round
        // against new text.
        qDeleteAll(m_docs);
        m_docs.clear();

        m_round = Round::loadOrCreateOpen(m_project->galleyDir());

        // Anything not sent this time moves to the next round. Its status
        // stays "open", not "carried": it was deferred, not ignored, and the
        // brief should not tell the agent otherwise.
        for (const Comment &c : std::as_const(r.comments)) {
            if (c.status != QStringLiteral("open") && c.status != QStringLiteral("carried"))
                continue;
            Comment next = c;
            next.carriedFrom = QStringLiteral("%1/%2").arg(r.number).arg(c.id);
            next.createdAt.clear();
            next.status = QStringLiteral("open");
            next.agentStatus.clear();
            next.agentNote.clear();
            next.stale = false;
            m_round.addComment(next);
        }

        // Those deferred comments quote text the agent may have just
        // rewritten, so their anchors have to be re-checked against the book
        // as it now stands.
        refreshStale(m_round);
        m_round.save(&err);
    } else {
        // A run that changed nothing must not burn the round. Reopen it with
        // every comment back where it was, so the fix is to retry rather than
        // to carry a whole read-through forward by hand.
        r.state = QStringLiteral("open");
        r.dispatchedAt.clear();
        for (Comment &c : r.comments)
            if (c.status == QStringLiteral("sent"))
                c.status = c.carriedFrom.isEmpty() ? QStringLiteral("open")
                                                   : QStringLiteral("carried");
        r.save(&err);
        m_round = r;
    }

    QString message;
    if (ok) {
        message = QStringLiteral("Round %1 applied. Round %2 is open.")
                      .arg(m_dispatchedRound)
                      .arg(m_round.number);
    } else if (!exited) {
        message = QStringLiteral("Agent exited with %1 — round %2 is still open, "
                                 "nothing was lost. See the log (Ctrl+L).")
                      .arg(exitCode)
                      .arg(r.number);
    } else {
        // "Changed nothing" has two very different causes, and guessing the
        // wrong one sends the reader to the wrong place.
        int explained = 0;
        for (const Comment &c : r.comments)
            if (!c.agentStatus.isEmpty())
                ++explained;

        message = explained > 0
            ? QStringLiteral("The agent changed nothing and said why — round %1 is still "
                             "open, with its reasons against each comment.")
                  .arg(r.number)
            : QStringLiteral("The agent finished but changed nothing, and said nothing "
                             "about why — round %1 is still open. Check the log (Ctrl+L): "
                             "it most likely needed a permission its profile does not grant.")
                  .arg(r.number);
    }

    m_lastResultRound = m_dispatchedRound;

    emit roundChanged(roundJson());
    emit runFinished(crashed ? -1 : exitCode, ok ? diff : QString());
    emit status(ok ? QStringLiteral("ok") : QStringLiteral("error"), message);
}

// roundNumber 0 means "the one this session last dispatched".
QString Bridge::resultJson(int roundNumber)
{
    const int wanted = roundNumber > 0 ? roundNumber : m_lastResultRound;
    if (wanted == 0)
        return toJson(QJsonObject{{QStringLiteral("round"), 0}});

    const Round r = Round::load(m_project->galleyDir(), wanted);

    QString diff;
    QFile df(r.diffPath());
    if (df.open(QIODevice::ReadOnly))
        diff = QString::fromUtf8(df.readAll());

    // A comment that was never sent is neither answered nor unanswered, and
    // counting it as unreported would accuse the agent of ignoring something
    // it was never shown.
    int applied = 0, partial = 0, declined = 0, unreported = 0, deferred = 0;
    for (const Comment &c : r.comments) {
        if (c.agentStatus == QStringLiteral("applied")) ++applied;
        else if (c.agentStatus == QStringLiteral("partial")) ++partial;
        else if (c.agentStatus == QStringLiteral("declined")) ++declined;
        else if (c.status == QStringLiteral("sent")) ++unreported;
        else ++deferred;
    }

    QJsonArray files;
    for (const Chapter &ch : m_project->chapters()) {
        QJsonArray here;
        for (const Comment &c : r.comments)
            if (c.file == ch.relPath)
                here << c.toJson();
        if (!here.isEmpty())
            files << QJsonObject{{QStringLiteral("file"), ch.relPath},
                                 {QStringLiteral("comments"), here}};
    }
    QJsonArray book;
    for (const Comment &c : r.comments)
        if (c.file.isEmpty() || c.scope == QStringLiteral("book"))
            book << c.toJson();

    return toJson(QJsonObject{
        {QStringLiteral("round"), r.number},
        {QStringLiteral("state"), r.state},
        {QStringLiteral("agent"), r.agent},
        {QStringLiteral("diff"), diff},
        {QStringLiteral("reported"), applied + partial + declined > 0},
        {QStringLiteral("files"), files},
        {QStringLiteral("book"), book},
        {QStringLiteral("summary"), QJsonObject{{QStringLiteral("applied"), applied},
                                                {QStringLiteral("partial"), partial},
                                                {QStringLiteral("declined"), declined},
                                                {QStringLiteral("unreported"), unreported},
                                                {QStringLiteral("deferred"), deferred}}},
    });
}

void Bridge::proof()
{
    if (m_agent->running() || m_pdf->busy()) {
        emit status(QStringLiteral("error"), QStringLiteral("something is already running"));
        return;
    }

    QFile css(QStringLiteral(":/print.css"));
    if (!css.open(QIODevice::ReadOnly)) {
        emit status(QStringLiteral("error"), QStringLiteral("print stylesheet missing"));
        return;
    }

    QString body;
    for (const Chapter &ch : m_project->chapters()) {
        Document *d = document(ch.relPath);
        if (!d)
            continue;
        body += QStringLiteral("<section class=\"chapter\">\n") + d->html()
            + QStringLiteral("\n</section>\n");
    }
    if (body.isEmpty()) {
        emit status(QStringLiteral("error"), QStringLiteral("nothing to print"));
        return;
    }

    const QString html = QStringLiteral(
                             "<!doctype html><html><head><meta charset=\"utf-8\">"
                             "<title>%1</title><style>%2</style></head><body>%3</body></html>")
                             .arg(m_project->name(), QString::fromUtf8(css.readAll()), body);

    QDir().mkpath(m_project->galleyDir());
    const QString out = QDir(m_project->galleyDir()).filePath(QStringLiteral("proof.pdf"));

    emit runStarted(QStringLiteral("proof PDF"));
    emit logLine(QStringLiteral("Rendering %1 chapters to %2 …")
                     .arg(m_project->chapters().size())
                     .arg(QDir(m_project->root()).relativeFilePath(out)));
    m_pdf->write(html, out);
}

void Bridge::openLink(const QString &url)
{
    const QUrl target(url);
    if (target.scheme() != QStringLiteral("https") && target.scheme() != QStringLiteral("http"))
        return;
    QDesktopServices::openUrl(target);
}

void Bridge::quit()
{
    // Quitting would take the agent's process with it, mid-edit.
    if (m_agent->running() || m_pdf->busy()) {
        emit status(QStringLiteral("error"),
                    QStringLiteral("Something is running. Wait for it to finish."));
        return;
    }
    QCoreApplication::quit();
}

void Bridge::cancel()
{
    m_agent->cancel();
}
