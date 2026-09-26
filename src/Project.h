#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct Chapter {
    QString relPath;  // relative to the book root
    QString title;
};

// A book: a directory of Markdown files plus a reading order.
//
// Described by .galley/project.toml. When that file is absent the spine is
// recovered from an existing pandoc build script, which is how adopting
// Galley on a book that already builds is one confirmation rather than
// retyping every path.
class Project
{
public:
    bool open(const QString &rootDir, QString *error);
    bool save(QString *error) const;

    // Rewrites just the `references` array in project.toml, leaving every
    // other line alone. save() regenerates the whole file from parsed fields,
    // which would discard comments and any key Galley does not know about —
    // fine for writing the file the first time, not for editing one the
    // author has since made their own.
    bool writeReferences(const QStringList &refs, QString *error);

    QString root() const { return m_root; }
    QString name() const { return m_name; }
    QString galleyDir() const;
    const QList<Chapter> &chapters() const { return m_chapters; }
    QStringList spine() const { return m_spine; }
    QStringList references() const { return m_references; }
    QString defaultAgent() const { return m_defaultAgent; }

    QString absolutePath(const QString &relPath) const;

    // True when project.toml did not exist and was inferred on open.
    bool wasImported() const { return m_imported; }

    // Where the reading order came from: a build script's name, or empty when
    // it was guessed from the directory. Worth telling the reader, because a
    // guessed order is the one thing about an imported book most likely to be
    // wrong.
    QString spineSource() const { return m_spineSource; }
    bool spineGuessed() const { return m_spineGuessed; }

    // Has this directory been opened as a book before? Galley will import
    // anything with Markdown in it when told to, so an open nobody asked for
    // — no argument, just a working directory — requires this instead.
    static bool isBook(const QString &dir);

    // Pulls the `for path in ... do` list out of a pandoc build script. The
    // reading order is worth borrowing; running the script is not Galley's
    // business, and a book's own build can fail in ways Galley cannot explain.
    static QStringList spineFromBuildScript(const QString &scriptPath);

private:
    void resolveChapters();

    QString m_root;
    QString m_name;
    QStringList m_spine;
    QStringList m_references;
    QString m_defaultAgent;
    QList<Chapter> m_chapters;
    bool m_imported = false;
    QString m_spineSource;
    bool m_spineGuessed = false;
};
