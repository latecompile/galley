#include "AgentProfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <toml++/toml.hpp>

namespace {

// An agent Galley knows how to drive non-interactively.
//
// These run with nobody watching, so an agent that stops to ask permission
// cannot be answered — it simply reports back having changed nothing. Each
// invocation therefore grants file editing up front, and no more than that:
// applying an editorial review reads and writes Markdown and does nothing
// else. Where a tool offers nothing narrower than "allow everything", the
// profile is written commented out rather than quietly granting it.
struct KnownAgent {
    const char *name;
    const char *command;
    bool enabled;
    const char *note;
};

const KnownAgent kKnown[] = {
    {"claude", R"(["claude", "-p", "--permission-mode", "acceptEdits", "{prompt}"])", true, nullptr},
    {"codex", R"(["codex", "exec", "--sandbox", "workspace-write", "{prompt}"])", true, nullptr},
    {"grok", R"(["grok", "-p", "{prompt}", "--permission-mode", "acceptEdits"])", true, nullptr},
    {"gemini", R"(["gemini", "--approval-mode", "auto_edit", "-p", "{prompt}"])", true, nullptr},
    {"opencode", R"(["opencode", "run", "{prompt}"])", true, nullptr},
    {"crush", R"(["crush", "run", "-y", "{prompt}"])", false,
     "crush has no edits-only permission mode; -y allows everything it can do"},
    {"cursor-agent", R"(["cursor-agent", "-p", "--force", "{prompt}"])", false,
     "cursor-agent has no edits-only mode; --force allows commands too"},
};

bool onPath(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

QString defaultsText()
{
    QString out;
    QTextStream ts(&out);
    ts << "# Galley agent profiles.\n"
       << "#\n"
       << "# {prompt} is replaced with the instruction, {brief} with the path to the\n"
       << "# round's brief.md, and {root} with the book root. The process runs with the\n"
       << "# book root as its working directory.\n"
       << "#\n"
       << "# These run non-interactively, so an agent that stops to ask for permission\n"
       << "# cannot be answered and reports back having changed nothing. Each profile\n"
       << "# grants file editing up front and no more: applying an editorial review\n"
       << "# reads and writes Markdown and does nothing else.\n"
       << "#\n"
       << "# Written from what was installed when Galley first ran. Add your own in the\n"
       << "# same shape — the name is yours to choose.\n\n";

    bool wroteAny = false;
    for (const KnownAgent &a : kKnown) {
        const QString name = QString::fromLatin1(a.name);
        if (!onPath(name))
            continue;
        wroteAny = true;
        if (a.note)
            ts << "# " << a.note << "\n";
        const QString prefix = a.enabled ? QString() : QStringLiteral("# ");
        if (!a.enabled)
            ts << "# Uncomment to use it on those terms.\n";
        ts << prefix << "[" << name << "]\n"
           << prefix << "command = " << QString::fromLatin1(a.command) << "\n\n";
    }

    if (!wroteAny) {
        ts << "# Galley found no agent CLI it knows on your PATH. Add one, for example:\n"
           << "#\n"
           << "# [claude]\n"
           << "# command = [\"claude\", \"-p\", \"--permission-mode\", \"acceptEdits\", \"{prompt}\"]\n";
    }

    ts.flush();
    return out;
}

} // namespace

QString AgentProfiles::configPath()
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                            .filePath(QStringLiteral("galley"));
    return QDir(dir).filePath(QStringLiteral("agents.toml"));
}

QString AgentProfiles::omarchyDefault()
{
    // Omarchy records the agent the user chose for the whole desktop. Reading
    // the file rather than running omarchy-default-agent, which would install
    // one if it were missing.
    QFile f(QDir::homePath() + QStringLiteral("/.config/omarchy/defaults/agent"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

QMap<QString, AgentProfile> AgentProfiles::load()
{
    const QString path = configPath();
    if (!QFile::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(defaultsText().toUtf8());
    }

    QMap<QString, AgentProfile> out;
    try {
        auto tbl = toml::parse_file(path.toStdString());
        for (const auto &[key, node] : tbl) {
            auto *sub = node.as_table();
            if (!sub)
                continue;
            AgentProfile p;
            p.name = QString::fromStdString(std::string(key.str()));
            if (auto *arr = (*sub)["command"].as_array())
                for (const auto &e : *arr)
                    if (auto v = e.value<std::string>())
                        p.command << QString::fromStdString(*v);
            if (!p.command.isEmpty())
                out.insert(p.name, p);
        }
    } catch (const toml::parse_error &) {
        // Fall through to an empty map; the caller reports "no agent".
    }
    return out;
}

QStringList AgentProfiles::rescan()
{
    const QString path = configPath();
    load();  // makes sure the file exists before anything is appended to it

    QFile f(path);
    QString existing;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        existing = QString::fromUtf8(f.readAll());
        f.close();
    }

    QString additions;
    QTextStream ts(&additions);
    QStringList added;

    for (const KnownAgent &a : kKnown) {
        const QString name = QString::fromLatin1(a.name);
        if (!onPath(name))
            continue;
        // In any form counts, including commented out: an author who does not
        // want an agent offered should comment it rather than delete it, and
        // a rescan that argued with that would be useless.
        if (existing.contains(QStringLiteral("[%1]").arg(name)))
            continue;

        if (a.note)
            ts << "# " << a.note << "\n";
        const QString prefix = a.enabled ? QString() : QStringLiteral("# ");
        if (!a.enabled)
            ts << "# Uncomment to use it on those terms.\n";
        ts << prefix << "[" << name << "]\n"
           << prefix << "command = " << QString::fromLatin1(a.command) << "\n\n";
        added << name;
    }
    ts.flush();

    if (added.isEmpty())
        return {};

    if (!f.open(QIODevice::Append | QIODevice::Text))
        return {};
    QTextStream out(&f);
    if (!existing.endsWith(QLatin1Char('\n')))
        out << "\n";
    out << "\n# Found on a later rescan.\n\n" << additions;
    return added;
}

QStringList AgentProfiles::expand(const AgentProfile &p, const QString &prompt,
                                  const QString &brief, const QString &root)
{
    QStringList out;
    for (QString arg : p.command) {
        arg.replace(QStringLiteral("{prompt}"), prompt);
        arg.replace(QStringLiteral("{brief}"), brief);
        arg.replace(QStringLiteral("{root}"), root);
        out << arg;
    }
    return out;
}
