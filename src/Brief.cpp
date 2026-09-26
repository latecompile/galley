#include "Brief.h"

#include "Project.h"
#include "Round.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {

// The source Markdown behind a comment's block, when the block mapping held.
QByteArray sourceBlock(const Project &p, const Comment &c)
{
    if (c.sourceStart < 0 || c.sourceEnd <= c.sourceStart)
        return {};
    QFile f(p.absolutePath(c.file));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    if (c.sourceEnd > all.size())
        return {};
    return all.mid(c.sourceStart, c.sourceEnd - c.sourceStart);
}

QString blockQuote(const QString &text)
{
    QStringList out;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &l : lines)
        out << (l.isEmpty() ? QStringLiteral(">") : QStringLiteral("> ") + l);
    return out.join(QLatin1Char('\n'));
}

// Already dealt with: rejected by the author, or resolved — including by the
// author editing the text themselves in write mode. Re-sending either wastes
// the agent's time and invites it to undo a decision.
bool settled(const Comment &c)
{
    return c.status == QStringLiteral("rejected") || c.status == QStringLiteral("applied");
}

// In this brief at all: not already dealt with, and picked if a selection was
// made.
bool included(const Comment &c, const QStringList &only)
{
    if (settled(c))
        return false;
    return only.isEmpty() || only.contains(c.id);
}

QString scopeLabel(const QString &scope)
{
    if (scope == QStringLiteral("sentence")) return QStringLiteral("Sentence");
    if (scope == QStringLiteral("section"))  return QStringLiteral("Section");
    if (scope == QStringLiteral("footnote")) return QStringLiteral("Footnote");
    if (scope == QStringLiteral("file"))     return QStringLiteral("Whole chapter");
    return QStringLiteral("Paragraph");
}

} // namespace

QString Brief::generate(const Project &project, const Round &round,
                        const QStringList &only)
{
    const QString reportRelPath =
        QDir(project.root()).relativeFilePath(round.reportPath());

    QString out;
    QTextStream ts(&out);

    ts << "# Editorial review — " << project.name() << ", round " << round.number << "\n\n";
    ts << "You are revising a book written in Markdown. Below are reader comments\n";
    ts << "from one read-through, grouped by file. Edit the Markdown files in place.\n";
    ts << "Preserve the existing voice, structure and formatting conventions.\n";
    ts << "Do not commit.\n\n";
    ts << "Each comment quotes the passage it refers to. Locate that passage by\n";
    ts << "reading; the quote is the rendered text, so Markdown emphasis and link\n";
    ts << "syntax will differ slightly from the source.\n\n";

    const QStringList refs = project.references();
    if (!refs.isEmpty()) {
        ts << "Reference material for the whole book:\n\n";
        for (const QString &r : refs)
            ts << "- `" << r << "`\n";
        ts << "\n";
    }

    // Group by file, in reading order, so the agent walks the book forwards.
    QStringList order;
    for (const Chapter &ch : project.chapters())
        order << ch.relPath;

    // The contents. A comment routinely reaches past its own paragraph —
    // "we already say this in a later chapter", "this contradicts part 4" —
    // and without a map the agent has to go hunting for what the author could
    // simply have been shown.
    ts << "## The book\n\n";
    ts << "In reading order. Chapters with comments this round are marked.\n\n";
    for (const Chapter &ch : project.chapters()) {
        int n = 0;
        for (const Comment &c : round.comments)
            if (c.file == ch.relPath && included(c, only))
                ++n;
        ts << "- `" << ch.relPath << "` — " << ch.title;
        if (n > 0)
            ts << QStringLiteral("  ← %1 comment%2 below").arg(n).arg(n == 1 ? "" : "s");
        ts << "\n";
    }
    ts << "\n";

    QList<const Comment *> book;
    for (const Comment &c : round.comments) {
        if (!included(c, only))
            continue;
        if (c.scope == QStringLiteral("book") || c.file.isEmpty())
            book.append(&c);
    }

    for (const QString &file : std::as_const(order)) {
        QList<const Comment *> here;
        for (const Comment &c : round.comments) {
            if (c.file == file && c.scope != QStringLiteral("book") && included(c, only))
                here.append(&c);
        }
        if (here.isEmpty())
            continue;

        ts << "---\n\n## `" << file << "`\n\n";
        int n = 0;
        for (const Comment *c : std::as_const(here)) {
            ++n;
            ts << "### " << n << ". " << scopeLabel(c->scope) << " · `" << c->id << "`";
            if (c->status == QStringLiteral("carried"))
                ts << " — carried over from an earlier round, still not addressed";
            ts << "\n\n";

            if (!c->quote.isEmpty())
                ts << blockQuote(c->quote) << "\n\n";

            if (c->stale)
                ts << "*The quoted text is no longer in the chapter — it has been "
                      "edited since this comment was written. Judge whether the "
                      "comment still applies.*\n\n";

            ts << "**Comment:** " << c->note << "\n\n";

            const QByteArray src = sourceBlock(project, *c);
            if (!src.isEmpty() && src.size() <= 600
                && QString::fromUtf8(src).trimmed() != c->quote.trimmed()) {
                ts << "<details><summary>Source block</summary>\n\n";
                ts << "````markdown\n" << QString::fromUtf8(src).trimmed() << "\n````\n\n";
                ts << "</details>\n\n";
            }

            if (!c->refs.isEmpty()) {
                ts << "**See also:**";
                for (const QString &r : c->refs) {
                    const bool link = r.startsWith(QStringLiteral("http://"))
                        || r.startsWith(QStringLiteral("https://"));
                    ts << (link ? QStringLiteral(" %1").arg(r) : QStringLiteral(" `%1`").arg(r));
                }
                ts << "\n\n";
            }
        }
    }

    if (!book.isEmpty()) {
        ts << "---\n\n## Whole book\n\n";
        for (const Comment *c : std::as_const(book))
            ts << "- `" << c->id << "` " << c->note << "\n";
        ts << "\n";
    }

    // Only the agent knows which change answered which comment. Asking is far
    // more reliable than inferring it from the diff afterwards, and it is the
    // one thing a reviewer cannot check for themselves at a glance.
    ts << "---\n\n## When you are done\n\n";
    ts << "Write `" << reportRelPath << "` — a JSON object keyed by the comment\n";
    ts << "ids above, saying what you did with each one:\n\n";
    ts << "```json\n";
    ts << "{\n";
    ts << "  \"c-0001\": { \"status\": \"applied\",  \"note\": \"Demoted to a footnote.\" },\n";
    ts << "  \"c-0002\": { \"status\": \"partial\",  \"note\": \"Tightened the opening; left the example.\" },\n";
    ts << "  \"c-0003\": { \"status\": \"declined\", \"note\": \"The claim is correct as written — see vault.rb:40.\" }\n";
    ts << "}\n";
    ts << "```\n\n";
    ts << "Every comment must appear exactly once. `declined` is a legitimate\n";
    ts << "answer and a useful one — say why, and do not change the text to\n";
    ts << "avoid saying it.\n";

    ts.flush();
    return out;
}

bool Brief::write(const Project &project, const Round &round, const QStringList &only,
                  QString *error)
{
    QDir().mkpath(round.dir());
    QFile f(round.briefPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1: %2").arg(round.briefPath(), f.errorString());
        return false;
    }
    f.write(generate(project, round, only).toUtf8());
    return true;
}
