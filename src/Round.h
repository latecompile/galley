#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// One anchored range of text plus a note. Paragraph, sentence and section are
// sizes of range, not separate kinds of thing.
struct Comment {
    QString id;
    QString file;      // relative to the book root
    int block = -1;    // top-level block index within that file
    QString quote;     // verbatim selected text, rendered form
    int start = 0;     // char offset of the selection within the block's text
    int end = 0;
    QString scope;     // sentence | paragraph | section | file | book
    QString note;
    QStringList refs;
    // Where this comment sits in the workflow:
    // open | carried | sent | applied | partial | declined | rejected.
    QString status = QStringLiteral("open");

    // What the agent said it did — applied | partial | declined, or empty for
    // "it said nothing". Kept apart from `status` because the two answer
    // different questions: a round that reopens returns its comments to
    // `open`, but the agent's account of why it declined is still true.
    QString agentStatus;
    QString agentNote;

    // The text this comment quotes is no longer anywhere in its chapter,
    // which normally means the author has just edited it themselves. Not an
    // error, and not a status — the comment is still open, it simply no
    // longer points at anything.
    bool stale = false;
    QString createdAt;
    QString carriedFrom;  // id of the comment this was cloned from, if any
    int sourceStart = -1; // byte range in the .md, when block mapping held
    int sourceEnd = -1;

    QJsonObject toJson() const;
    static Comment fromJson(const QJsonObject &o);
};

// One read-through. Frozen against a content snapshot at dispatch, then
// archived. Galley never re-anchors comments onto rewritten prose; a new
// read-through opens a new round instead.
class Round
{
public:
    static Round loadOrCreateOpen(const QString &galleyDir);
    static Round load(const QString &galleyDir, int number);
    static QList<int> allNumbers(const QString &galleyDir);

    bool save(QString *error) const;
    QString dir() const;
    QString briefPath() const;
    QString logPath() const;
    QString diffPath() const;
    QString reportPath() const;

    QString addComment(Comment c);
    bool updateComment(const QString &id, const QJsonObject &patch);
    bool removeComment(const QString &id);
    const Comment *find(const QString &id) const;

    QJsonObject toJson() const;
    bool isOpen() const { return state == QStringLiteral("open"); }
    int openCount() const;

    QString galleyDir;
    int number = 1;
    QString state = QStringLiteral("open");
    QString snapshot;
    QString agent;
    QString openedAt;
    QString dispatchedAt;
    QString closedAt;
    QList<Comment> comments;

private:
    int nextCommentSeq() const;
};
