#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// How to invoke each agent CLI.
//
// Galley shells out to the agent the author already uses rather than calling
// a model API. Each CLI brings its own file editing, source-tree reading,
// permissions and auth; reimplementing that would be far more work and
// strictly less capable.
struct AgentProfile {
    QString name;
    QStringList command;  // {prompt}, {brief} and {root} are substituted
};

namespace AgentProfiles {

// ~/.config/galley/agents.toml, written with defaults on first run.
QString configPath();
QMap<QString, AgentProfile> load();

// The agent the user chose for the whole Omarchy desktop, if they chose one.
// Empty elsewhere, and empty when nothing is set.
QString omarchyDefault();

// Looks for agents installed since the file was written and appends profiles
// for them. Returns the names added. Never touches what is already there —
// the file belongs to whoever edited it last, which is usually not Galley.
QStringList rescan();

QStringList expand(const AgentProfile &p, const QString &prompt, const QString &brief,
                   const QString &root);

} // namespace AgentProfiles
