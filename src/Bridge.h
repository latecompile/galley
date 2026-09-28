#pragma once

#include "Document.h"
#include "Project.h"
#include "Round.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

class QWidget;
#include <QString>

class PdfExporter;
class ProcessRunner;
class ModelDiscovery;
class Theme;

// The entire C++ ↔ JS contract.
//
// Kept deliberately small, because this is the seam a hosted version would
// cut along: replace Bridge with an HTTP+WebSocket server exposing the same
// calls and the web/ directory moves across untouched. So: no DOM knowledge
// here, and no business logic on the other side beyond capturing an anchor.
class Bridge : public QObject
{
    Q_OBJECT

public:
    Bridge(Project *project, Theme *theme, QObject *parent = nullptr);

    // Parent for modal dialogs; without it a file chooser can open behind
    // the window it belongs to.
    void setDialogParent(QWidget *w) { m_dialogParent = w; }

public slots:
    QString projectJson();
    QString chapterJson(int index);
    QString roundJson();
    QString themeJson();
    QString historyJson();
    QString preflightJson();
    QString briefPreview(const QString &idsJson);
    // Adds profiles for agents installed since agents.toml was written.
    QString rescanAgents();
    // Model discovery is explicit and asynchronous. This call returns cached
    // catalogs; refreshModels later emits modelsRefreshed.
    QString modelPickerJson();
    void refreshModels(const QString &agent);
    QString modelProfileName(const QString &agent, const QString &model,
                             const QString &effort);
    QString addModelProfile(const QString &agent, const QString &model,
                            const QString &effort);
    // The book's own reference list, which every brief carries.
    void setProjectReferences(const QString &json);
    QString resultJson(int roundNumber);
    QString sourceForBlock(const QString &file, int block);

    // Whole-book search. Runs in C++ over every chapter, because the useful
    // version is the one that finds the thing you half-remember from a
    // chapter you are not currently reading.
    QString search(const QString &query);

    // Write mode. Galley's only path to writing the book, and the author's
    // own hand — the agent's edits still come from the agent.
    QString chapterSource(int index);
    QString saveChapter(int index, const QString &text, const QString &seenAt);

    // Native file chooser, so a reference can be picked rather than typed.
    QString pickReferences();
    // Which of these references actually resolve — a reference to a path that
    // is not there is worse than none, because the agent will not say so.
    QString checkReferences(const QString &json);

    QString addComment(const QString &json);
    void updateComment(const QString &id, const QString &json);
    void deleteComment(const QString &id);
    void carryForward(int fromRound, const QString &id);
    void setCommentStatus(const QString &id, const QString &status);

    void dispatch(const QString &agentName, const QString &idsJson);
    // The book as a PDF, from Galley's own renderer. Galley does not run a
    // book's own build script: that is the book's business, and a script
    // Galley shells out to blind can fail in ways it cannot explain.
    void proof();
    // Swap the window to another book, without going back to a terminal.
    void openBook();
    void quit();
    // Opens a link in the system browser. The app's own page must not
    // navigate anywhere.
    void openLink(const QString &url);
    void cancel();

signals:
    void roundChanged(const QString &json);
    void bookChanged(const QString &json);
    void themeChanged(const QString &json);
    void logLine(const QString &text);
    void runStarted(const QString &label);
    void runProgress(int seconds, const QString &pretty);
    void runFinished(int exitCode, const QString &diff);
    void modelsRefreshed(const QString &json);
    void status(const QString &level, const QString &message);

private:
    Document *document(const QString &relPath);
    void persist();
    // Re-checks every unsettled comment's quote against its chapter as it now
    // stands. Called after a round closes, because a comment left for the
    // next round may quote a paragraph the agent has just rewritten.
    void refreshStale(Round &round);
    void onAgentFinished(int exitCode, bool crashed);

    Project *m_project;
    Theme *m_theme;
    Round m_round;
    ProcessRunner *m_agent;
    ModelDiscovery *m_models;
    PdfExporter *m_pdf;
    QHash<QString, Document *> m_docs;
    QStringList m_log;
    int m_dispatchedRound = 0;
    int m_secsAtLastLine = 0;
    int m_lastResultRound = 0;
    QWidget *m_dialogParent = nullptr;
};
