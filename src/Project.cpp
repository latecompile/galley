#include "Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <toml++/toml.hpp>

namespace {

QString qstr(toml::node_view<toml::node> n, const QString &fallback)
{
    if (auto v = n.value<std::string>())
        return QString::fromStdString(*v);
    return fallback;
}

QStringList qlist(toml::node_view<toml::node> n)
{
    QStringList out;
    if (auto *arr = n.as_array())
        for (const auto &e : *arr)
            if (auto v = e.value<std::string>())
                out << QString::fromStdString(*v);
    return out;
}

// Reads the first ATX H1 without rendering the file. Titles are needed for
// every chapter in the sidebar, and rendering 30 files to learn 30 strings
// would be silly.
QString titleOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QFileInfo(path).completeBaseName();
    QTextStream in(&f);
    int guard = 0;
    while (!in.atEnd() && guard++ < 200) {
        QString line = in.readLine();
        static const QRegularExpression h1(QStringLiteral(R"(^#\s+(.+?)\s*$)"));
        auto m = h1.match(line);
        if (m.hasMatch())
            return m.captured(1);
    }
    return QFileInfo(path).completeBaseName();
}

} // namespace

QStringList Project::spineFromBuildScript(const QString &scriptPath)
{
    QFile f(scriptPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(f.readAll());

    // for path in \
    //   README.md \
    //   part1-introduction \
    // do
    static const QRegularExpression loop(
        QStringLiteral(R"(for\s+\w+\s+in\s*\\?\s*\n(.*?)\n\s*do\b)"),
        QRegularExpression::DotMatchesEverythingOption);
    auto m = loop.match(text);
    if (!m.hasMatch())
        return {};

    QStringList out;
    const auto lines = m.captured(1).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(QLatin1Char('\\'));
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        out << line;
    }
    return out;
}

bool Project::isBook(const QString &dir)
{
    return QFile::exists(QDir(dir).filePath(QStringLiteral(".galley/project.toml")));
}

QString Project::galleyDir() const
{
    return QDir(m_root).filePath(QStringLiteral(".galley"));
}

QString Project::absolutePath(const QString &relPath) const
{
    return QDir(m_root).filePath(relPath);
}

bool Project::open(const QString &rootDir, QString *error)
{
    // open() is tried against several candidates in turn, so every attempt
    // starts clean. A failed one leaving `m_imported` set would make the next
    // success rewrite a project.toml that the author may have edited by hand.
    m_name.clear();
    m_spine.clear();
    m_references.clear();
    m_chapters.clear();
    m_spineSource.clear();
    m_spineGuessed = false;
    m_imported = false;
    m_defaultAgent.clear();

    QDir dir(rootDir);
    if (!dir.exists()) {
        *error = QStringLiteral("no such directory: %1").arg(rootDir);
        return false;
    }
    m_root = dir.absolutePath();
    m_name = QFileInfo(m_root).fileName();  // dirName() of "." is "."
    if (m_name.isEmpty())
        m_name = m_root;

    const QString cfg = QDir(galleyDir()).filePath(QStringLiteral("project.toml"));
    if (QFile::exists(cfg)) {
        try {
            auto tbl = toml::parse_file(cfg.toStdString());
            m_name = qstr(tbl["name"], m_name);
            m_spine = qlist(tbl["spine"]);
            m_references = qlist(tbl["references"]);
            m_defaultAgent = qstr(tbl["agent"]["default"], m_defaultAgent);
        } catch (const toml::parse_error &e) {
            *error = QStringLiteral("%1: %2").arg(cfg, QString::fromStdString(std::string(e.description())));
            return false;
        }
    } else {
        m_imported = true;

        // A pandoc book already carries its own title; the directory is
        // usually just called "book".
        for (const QString &candidate :
             {QStringLiteral("metadata.yaml"), QStringLiteral("metadata.yml")}) {
            QFile mf(dir.filePath(candidate));
            if (!mf.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            static const QRegularExpression titleRe(
                QStringLiteral(R"RX(^title:\s*"?([^"\n]+?)"?\s*$)RX"),
                QRegularExpression::MultilineOption);
            auto tm = titleRe.match(QString::fromUtf8(mf.readAll()));
            if (tm.hasMatch()) {
                m_name = tm.captured(1);
                break;
            }
        }

        // Prefer an existing build script's ordering over alphabetical guessing.
        for (const QString &candidate :
             {QStringLiteral("build-pdf.sh"), QStringLiteral("build.sh"),
              QStringLiteral("Makefile")}) {
            const QString path = dir.filePath(candidate);
            if (!QFile::exists(path))
                continue;
            m_spine = spineFromBuildScript(path);
            if (!m_spine.isEmpty()) {
                m_spineSource = candidate;
                break;
            }
        }
        if (m_spine.isEmpty()) {
            // Nothing to borrow an order from: loose files first, then each
            // directory, both alphabetical. A guess, and the reader is told so.
            m_spineGuessed = true;

            QStringList loose = dir.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
            // A README is the front matter of every book that has one.
            const int readme = loose.indexOf(QStringLiteral("README.md"));
            if (readme > 0)
                loose.move(readme, 0);
            m_spine = loose;

            const auto dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &d : dirs)
                if (!d.startsWith(QLatin1Char('.')))
                    m_spine << d;
        }
    }

    resolveChapters();
    if (m_chapters.isEmpty()) {
        *error = QStringLiteral("no Markdown files found under %1").arg(m_root);
        return false;
    }
    return true;
}

void Project::resolveChapters()
{
    m_chapters.clear();
    QDir dir(m_root);

    for (const QString &entry : std::as_const(m_spine)) {
        const QString abs = dir.filePath(entry);
        QFileInfo info(abs);

        if (info.isDir()) {
            // Alphabetical within a part; the spine fixes the order of parts.
            QDir sub(abs);
            const auto files = sub.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
            for (const QString &f : files) {
                const QString rel = entry + QLatin1Char('/') + f;
                m_chapters.append({rel, titleOf(sub.filePath(f))});
            }
        } else if (info.isFile() && entry.endsWith(QStringLiteral(".md"))) {
            m_chapters.append({entry, titleOf(abs)});
        }
    }
}

bool Project::writeReferences(const QStringList &refs, QString *error)
{
    m_references = refs;

    const QString cfg = QDir(galleyDir()).filePath(QStringLiteral("project.toml"));
    QFile f(cfg);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return save(error);  // no file yet: writing a whole one is right

    QString text = QString::fromUtf8(f.readAll());
    f.close();

    QString block = QStringLiteral("references = [\n");
    for (QString r : refs) {
        r.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        r.replace(QLatin1Char('"'), QLatin1String("\\\""));
        block += QStringLiteral("  \"%1\",\n").arg(r);
    }
    block += QLatin1Char(']');

    static const QRegularExpression existing(
        QStringLiteral(R"RX(^[ \t]*references[ \t]*=[ \t]*\[[^\]]*\])RX"),
        QRegularExpression::MultilineOption);
    const auto m = existing.match(text);
    if (m.hasMatch())
        text.replace(m.capturedStart(), m.capturedLength(), block);
    else
        text += QStringLiteral("\n# Material the agent should consult, relative to the "
                               "book root.\n%1\n").arg(block);

    QFile out(cfg);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1: %2").arg(cfg, out.errorString());
        return false;
    }
    out.write(text.toUtf8());
    return true;
}

bool Project::save(QString *error) const
{
    QDir().mkpath(galleyDir());
    const QString cfg = QDir(galleyDir()).filePath(QStringLiteral("project.toml"));

    QString out;
    QTextStream ts(&out);
    ts << "# Galley project. Written on first open; edit freely.\n\n";
    ts << "name = \"" << m_name << "\"\n\n";
    ts << "# Reading order. Directories expand to their *.md sorted\n";
    ts << "# alphabetically; files are taken as-is.\n";
    ts << "spine = [\n";
    for (const QString &s : m_spine)
        ts << "  \"" << s << "\",\n";
    ts << "]\n\n";
    ts << "# Material the agent should consult, relative to the book root.\n";
    ts << "references = [\n";
    for (const QString &s : m_references)
        ts << "  \"" << s << "\",\n";
    ts << "]\n\n";
    if (!m_defaultAgent.isEmpty()) {
        ts << "[agent]\n";
        ts << "default = \"" << m_defaultAgent << "\"\n";
    } else {
        ts << "# Which agent to dispatch to. Unset means the one chosen for the\n";
        ts << "# desktop, or the first profile in ~/.config/galley/agents.toml.\n";
        ts << "# [agent]\n";
        ts << "# default = \"claude\"\n";
    }
    ts.flush();

    QFile f(cfg);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1: %2").arg(cfg, f.errorString());
        return false;
    }
    f.write(out.toUtf8());
    return true;
}
