#pragma once

#include <QString>
#include <QStringList>

// Records what the book looked like when a round opened, so the round's
// result can be shown as a diff.
//
// Galley reads git and never writes to it: no init, no add, no commit. The
// agent's edits land in the working tree, which is where the author wants
// them.
namespace Snapshot {

bool isGitRepo(const QString &root);

// Records the working tree as it stands. A manifest of file hashes is always
// written, because that — not HEAD — is what answers "did this run change
// anything": mid-review the tree normally differs from the last commit
// already, and a diff against HEAD would credit the agent with the author's
// own uncommitted edits. The returned string notes the git commit too, for
// the record.
QString capture(const QString &root, const QString &galleyDir, int roundNumber);

// Files whose contents differ from the manifest taken at dispatch. This is
// the run's own footprint, and nothing else.
QStringList changedFiles(const QString &root, const QString &galleyDir, int roundNumber);

// Uncommitted changes under the book root. A dirty tree at dispatch means the
// result diff will mix the author's edits with the agent's.
bool isDirty(const QString &root);

// Unified diff of the book root against the snapshot.
QString diff(const QString &root, const QString &galleyDir, const QString &snapshot,
             int roundNumber);

} // namespace Snapshot
