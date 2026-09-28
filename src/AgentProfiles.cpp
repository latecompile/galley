#include "AgentProfiles.h"

#include <QDir>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
    {"grok", R"(["grok", "-p", "{prompt}", "--permission-mode", "acceptEdits", "--allow", "Write", "--allow", "Edit"])", true, nullptr},
    {"gemini", R"(["gemini", "--approval-mode", "auto_edit", "-p", "{prompt}"])", true, nullptr},
    {"opencode", R"(["opencode", "run", "{prompt}"])", true, nullptr},
    {"crush", R"(["crush", "run", "-y", "{prompt}"])", false,
     "crush has no edits-only permission mode; -y allows everything it can do"},
    {"cursor-agent", R"(["cursor-agent", "-p", "--force", "{prompt}"])", false,
     "cursor-agent has no edits-only mode; --force allows commands too"},
};

struct ModelAgent {
    const char *name;
    QStringList (*command)();
};

QStringList claudeModelCommand()
{
    return {QStringLiteral("claude"), QStringLiteral("-p"), QStringLiteral("--model"),
            QStringLiteral("{model}"), QStringLiteral("--effort"), QStringLiteral("{effort}"),
            QStringLiteral("--permission-mode"), QStringLiteral("acceptEdits"),
            QStringLiteral("{prompt}")};
}

QStringList codexModelCommand()
{
    return {QStringLiteral("codex"), QStringLiteral("exec"), QStringLiteral("--sandbox"),
            QStringLiteral("workspace-write"), QStringLiteral("-m"), QStringLiteral("{model}"),
            QStringLiteral("-c"), QStringLiteral("model_reasoning_effort=\"{effort}\""),
            QStringLiteral("{prompt}")};
}

QStringList grokModelCommand()
{
    return {QStringLiteral("grok"), QStringLiteral("-p"), QStringLiteral("{prompt}"),
            QStringLiteral("-m"), QStringLiteral("{model}"),
            QStringLiteral("--reasoning-effort"), QStringLiteral("{effort}"),
            QStringLiteral("--permission-mode"), QStringLiteral("acceptEdits"),
            QStringLiteral("--allow"), QStringLiteral("Write"), QStringLiteral("--allow"),
            QStringLiteral("Edit")};
}

QStringList opencodeModelCommand()
{
    return {QStringLiteral("opencode"), QStringLiteral("run"), QStringLiteral("-m"),
            QStringLiteral("{model}"), QStringLiteral("--variant"), QStringLiteral("{effort}"),
            QStringLiteral("{prompt}")};
}

QStringList geminiModelCommand()
{
    return {QStringLiteral("gemini"), QStringLiteral("--approval-mode"),
            QStringLiteral("auto_edit"), QStringLiteral("-m"), QStringLiteral("{model}"),
            QStringLiteral("-p"), QStringLiteral("{prompt}")};
}

const ModelAgent kModelAgents[] = {
    {"claude", claudeModelCommand},
    {"codex", codexModelCommand},
    {"grok", grokModelCommand},
    {"opencode", opencodeModelCommand},
    {"gemini", geminiModelCommand},
};

bool onPath(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

const ModelAgent *modelAgent(const QString &name)
{
    for (const ModelAgent &agent : kModelAgents)
        if (name == QLatin1String(agent.name))
            return &agent;
    return nullptr;
}

QString slugPart(const QString &value)
{
    QString out;
    bool dash = false;
    for (const QChar c : value.toLower()) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) {
            out += c;
            dash = false;
        } else if (!out.isEmpty() && !dash) {
            out += QLatin1Char('-');
            dash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

QString tomlString(const QString &value)
{
    QString out = value;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    out.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    out.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

QString commandToml(const QStringList &command)
{
    QStringList quoted;
    for (const QString &arg : command)
        quoted << tomlString(arg);
    return QStringLiteral("[%1]").arg(quoted.join(QStringLiteral(", ")));
}

QString defaultsText()
{
    QString out;
    QTextStream ts(&out);
    ts << "# Galley agent profiles.\n"
       << "#\n"
       << "# {prompt} is replaced with the instruction, {brief} with the path to the\n"
       << "# round's brief.md, {root} with the book root, and optional {model} and\n"
       << "# {effort} values come from their profile keys. The process runs with the\n"
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

bool readProfiles(toml::table *table, QByteArray *bytes, QString *error)
{
    const QString path = AgentProfiles::configPath();
    if (!QFile::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        const QByteArray defaults = defaultsText().toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || file.write(defaults) != defaults.size() || !file.flush()) {
            if (error)
                *error = QStringLiteral("could not create %1: %2").arg(path, file.errorString());
            return false;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("could not read %1: %2").arg(path, file.errorString());
        return false;
    }
    *bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = QStringLiteral("could not read %1: %2").arg(path, file.errorString());
        return false;
    }
    try {
        *table = toml::parse(std::string(bytes->constData(), bytes->size()), path.toStdString());
    } catch (const toml::parse_error &e) {
        if (error)
            *error = QStringLiteral("invalid TOML in %1: %2")
                         .arg(path, QString::fromUtf8(e.description().data(), e.description().size()));
        return false;
    }
    return true;
}

QMap<QString, AgentProfile> profilesFrom(const toml::table &table)
{
    QMap<QString, AgentProfile> out;
    for (const auto &[key, node] : table) {
        const auto *sub = node.as_table();
        if (!sub)
            continue;
        AgentProfile profile;
        profile.name = QString::fromStdString(std::string(key.str()));
        if (auto v = (*sub)["model"].value<std::string>())
            profile.model = QString::fromStdString(*v);
        if (auto v = (*sub)["effort"].value<std::string>())
            profile.effort = QString::fromStdString(*v);
        if (const auto *arr = (*sub)["command"].as_array())
            for (const auto &e : *arr)
                if (auto v = e.value<std::string>())
                    profile.command << QString::fromStdString(*v);
        if (!profile.command.isEmpty())
            out.insert(profile.name, profile);
    }
    return out;
}

QString profileName(const toml::table &table, const QString &agent, const QString &model,
                    const QString &effort, bool *alreadyExists)
{
    const auto profiles = profilesFrom(table);
    for (const AgentProfile &profile : profiles) {
        if (profile.command.first() == agent && profile.model == model && profile.effort == effort) {
            if (alreadyExists)
                *alreadyExists = true;
            return profile.name;
        }
    }

    QStringList parts{slugPart(agent), slugPart(model)};
    if (!effort.isEmpty())
        parts << slugPart(effort);
    parts.removeAll(QString());
    const QString base = parts.isEmpty() ? QStringLiteral("model-profile")
                                         : parts.join(QLatin1Char('-'));
    QString candidate = base;
    int suffix = 2;
    // Every TOML key reserves a name, including unfinished profiles and
    // tables that load() deliberately does not offer for dispatch.
    while (table.contains(candidate.toStdString()))
        candidate = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    return candidate;
}

bool appendProfile(const QByteArray &existing, const QByteArray &block, QString *error)
{
    const QString path = AgentProfiles::configPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append | QIODevice::Unbuffered)) {
        if (error)
            *error = QStringLiteral("could not append to %1: %2").arg(path, file.errorString());
        return false;
    }
    if (!file.seek(0) || file.readAll() != existing) {
        if (error)
            *error = QStringLiteral("%1 changed while adding the profile; try again").arg(path);
        return false;
    }
    if (file.write(block) != block.size() || !file.flush()) {
        const QString detail = file.errorString();
        // Remove only our incomplete append, preserving all existing bytes.
        const bool restored = file.resize(existing.size()) && file.flush();
        if (error)
            *error = QStringLiteral("could not append to %1: %2%3")
                         .arg(path, detail, restored ? QString()
                             : QStringLiteral("; could not remove the incomplete append"));
        return false;
    }
    return true;
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
    toml::table table;
    QByteArray bytes;
    return readProfiles(&table, &bytes, nullptr) ? profilesFrom(table)
                                                : QMap<QString, AgentProfile>();
}

QStringList AgentProfiles::installedModelAgents()
{
    QStringList out;
    for (const ModelAgent &agent : kModelAgents)
        if (onPath(QLatin1String(agent.name)))
            out << QLatin1String(agent.name);
    return out;
}

bool AgentProfiles::supportsModelAgent(const QString &agent)
{
    return modelAgent(agent) != nullptr;
}

QString AgentProfiles::label(const AgentProfile &profile)
{
    const QString name = QLatin1Char('-') + slugPart(profile.name) + QLatin1Char('-');
    const auto includedInName = [&name](const QString &value) {
        const QString slug = slugPart(value);
        return !slug.isEmpty()
            && name.contains(QLatin1Char('-') + slug + QLatin1Char('-'));
    };
    QStringList parts{profile.name};
    if (!profile.model.isEmpty() && !includedInName(profile.model))
        parts << profile.model;
    if (!profile.effort.isEmpty() && !includedInName(profile.effort))
        parts << profile.effort;
    return parts.join(QStringLiteral(" · "));
}

QString AgentProfiles::modelProfileName(const QString &agent, const QString &model,
                                        const QString &effort, bool *alreadyExists, QString *error)
{
    if (alreadyExists)
        *alreadyExists = false;

    toml::table table;
    QByteArray bytes;
    if (!readProfiles(&table, &bytes, error))
        return {};
    return profileName(table, agent, model, effort, alreadyExists);
}

QString AgentProfiles::addModel(const QString &agent, const QString &model,
                                const QString &effort, bool *alreadyExisted, QString *error)
{
    if (alreadyExisted)
        *alreadyExisted = false;
    const ModelAgent *known = modelAgent(agent);
    if (!known) {
        if (error)
            *error = QStringLiteral("unknown model-capable agent '%1'").arg(agent);
        return {};
    }
    if (!onPath(agent)) {
        if (error)
            *error = QStringLiteral("%1 is not installed on PATH").arg(agent);
        return {};
    }
    if (model.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("a model name is required");
        return {};
    }
    if (agent == QStringLiteral("gemini") && !effort.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("gemini does not expose a reasoning-effort option");
        return {};
    }

    toml::table table;
    QByteArray existing;
    if (!readProfiles(&table, &existing, error))
        return {};
    bool exists = false;
    const QString name = profileName(table, agent, model.trimmed(), effort.trimmed(), &exists);
    if (exists) {
        if (alreadyExisted)
            *alreadyExisted = true;
        return name;
    }

    QString block;
    QTextStream out(&block);
    if (!existing.isEmpty() && !existing.endsWith('\n'))
        out << "\n";
    if (!existing.isEmpty())
        out << "\n";
    out << "# Added by Galley's model picker, "
        << QDate::currentDate().toString(Qt::ISODate) << "\n";
    out << "[" << name << "]\n";
    out << "model = " << tomlString(model.trimmed()) << "\n";
    if (!effort.trimmed().isEmpty())
        out << "effort = " << tomlString(effort.trimmed()) << "\n";
    out << "command = " << commandToml(known->command()) << "\n";
    out.flush();
    return appendProfile(existing, block.toUtf8(), error) ? name : QString();
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
        const bool missingModel = p.model.isEmpty() && arg.contains(QStringLiteral("{model}"));
        const bool missingEffort = p.effort.isEmpty() && arg.contains(QStringLiteral("{effort}"));
        if (missingModel || missingEffort) {
            // A placeholder occupies the value position of the flag before it.
            // Dropping both also covers codex's `-c key={effort}` form.
            const bool modelFlag = !out.isEmpty()
                && (out.last() == QStringLiteral("--model") || out.last() == QStringLiteral("-m"));
            if (!out.isEmpty() && ((missingModel && modelFlag)
                                   || (missingEffort && out.last().startsWith(QLatin1Char('-')))))
                out.removeLast();
            continue;
        }
        static const QRegularExpression placeholder(
            QStringLiteral("\\{(prompt|brief|root|model|effort)\\}"));
        QString expanded;
        qsizetype cursor = 0;
        auto matches = placeholder.globalMatch(arg);
        while (matches.hasNext()) {
            const auto match = matches.next();
            expanded += arg.mid(cursor, match.capturedStart() - cursor);
            const QString key = match.captured(1);
            if (key == QStringLiteral("prompt")) expanded += prompt;
            else if (key == QStringLiteral("brief")) expanded += brief;
            else if (key == QStringLiteral("root")) expanded += root;
            else if (key == QStringLiteral("model")) expanded += p.model;
            else if (key == QStringLiteral("effort")) expanded += p.effort;
            cursor = match.capturedEnd();
        }
        expanded += arg.mid(cursor);
        out << expanded;
    }
    return out;
}
