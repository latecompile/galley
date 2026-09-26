#pragma once

#include <QString>
#include <QStringList>

class Project;
class Round;

// Turns a round's comments into the Markdown document the agent is given.
//
// Plain Markdown on purpose: the consumer is a language model, and the author
// should be able to read exactly what was sent before sending it.
namespace Brief {

// `only` restricts the brief to those comment ids. Empty means every comment
// in the round that has not already been dealt with.
QString generate(const Project &project, const Round &round,
                 const QStringList &only = {});
bool write(const Project &project, const Round &round, const QStringList &only,
           QString *error);

}
