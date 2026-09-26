#include "Footnotes.h"

#include <QCryptographicHash>
#include <QRegularExpression>

namespace {

// Continuation lines in a definition are indented; the content is what is
// left once that indent is removed.
QByteArray dedent(const QByteArray &line)
{
    int i = 0;
    int removed = 0;
    while (i < line.size() && removed < 4) {
        if (line[i] == ' ') {
            removed += 1;
            i += 1;
        } else if (line[i] == '\t') {
            removed += 4;
            i += 1;
        } else {
            break;
        }
    }
    return line.mid(i);
}

} // namespace

QList<Footnote> Footnotes::extract(const QByteArray &source, const QList<SourceBlock> &all,
                                   QList<SourceBlock> *body)
{
    QList<Footnote> notes;
    body->clear();

    static const QRegularExpression head(QStringLiteral(R"RX(^\s{0,3}\[\^([^\]\s^]+)\]:[ \t]*)RX"));

    for (const SourceBlock &b : all) {
        if (!b.footnoteDef) {
            if (!b.refDef)
                body->append(b);
            continue;
        }

        const QByteArray raw = source.mid(b.start, b.end - b.start);
        const QList<QByteArray> lines = raw.split('\n');
        if (lines.isEmpty())
            continue;

        auto m = head.match(QString::fromUtf8(lines.first()));
        if (!m.hasMatch()) {
            // Scanner and parser disagree about what this is; treat it as
            // ordinary prose rather than silently dropping it.
            body->append(b);
            continue;
        }

        Footnote note;
        note.label = m.captured(1);
        note.block = b;

        QList<QByteArray> content;
        content << lines.first().mid(m.capturedEnd(0));
        for (int i = 1; i < lines.size(); ++i)
            content << dedent(lines.at(i));
        note.markdown = content.join('\n').trimmed();

        // A repeated label is a defect in the book; the first wins, as in pandoc.
        bool seen = false;
        for (const Footnote &existing : notes)
            if (existing.label == note.label)
                seen = true;
        if (!seen)
            notes.append(note);
    }

    return notes;
}

QByteArray Footnotes::bodySource(const QByteArray &source, const QList<SourceBlock> &all)
{
    QByteArray out;
    for (const SourceBlock &b : all) {
        if (b.footnoteDef)
            continue;
        out.append(source.mid(b.start, b.end - b.start));
        out.append("\n\n");
    }
    return out;
}

QString Footnotes::idScope(const QString &relPath)
{
    const QByteArray digest =
        QCryptographicHash::hash(relPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    return QString::fromLatin1(digest);
}
