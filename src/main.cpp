#include "AgentProfiles.h"
#include "Bridge.h"
#include "Brief.h"
#include "Document.h"
#include "MainWindow.h"
#include "Project.h"
#include "Round.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

namespace {

// Launched from a menu there is no argument and no useful working directory,
// so Galley has to ask. Tried in order: what was asked for, the directory the
// terminal was in, the book opened last, and finally a chooser.
bool openForWindow(Project *project, const QString &asked, QString *chosen)
{
    QSettings settings;
    QString error;

    // Naming a directory is an instruction: import it, Markdown and all.
    if (!asked.isEmpty() && project->open(asked, &error)) {
        *chosen = asked;
        settings.setValue(QStringLiteral("lastBook"), project->root());
        return true;
    }

    // Opening one nobody named is a guess, and a guess may only land on a
    // book that already exists. Galley will happily import any directory with
    // Markdown in it — a source tree, a notes folder, a home directory — and
    // writing .galley into one of those because a launcher had no argument is
    // not a thing it should do.
    if (asked.isEmpty()) {
        for (const QString &candidate : {QDir::currentPath(),
                                         settings.value(QStringLiteral("lastBook")).toString()}) {
            if (candidate.isEmpty() || !Project::isBook(candidate))
                continue;
            if (project->open(candidate, &error)) {
                *chosen = candidate;
                settings.setValue(QStringLiteral("lastBook"), project->root());
                return true;
            }
        }
    }

    // Asked for something specific and it was not a book: say so rather than
    // quietly offering a chooser, which would look like the argument worked.
    if (!asked.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("Galley"),
                             QStringLiteral("%1\n\nPick a folder of Markdown files instead.")
                                 .arg(error));
    }

    forever {
        // Start where the user already is, so a deliberate cd into a new book
        // is one keypress away from being accepted.
        const QString here = QDir::currentPath();
        const QString startAt = (here != QDir::homePath() && here != QStringLiteral("/"))
            ? here
            : settings.value(QStringLiteral("lastBook"), QDir::homePath()).toString();

        const QString picked = QFileDialog::getExistingDirectory(
            nullptr, QStringLiteral("Open a book — a folder of Markdown files"), startAt);
        if (picked.isEmpty())
            return false;
        if (project->open(picked, &error)) {
            *chosen = picked;
            settings.setValue(QStringLiteral("lastBook"), project->root());
            return true;
        }
        QMessageBox::warning(nullptr, QStringLiteral("Galley"), error);
    }
}

// The terminal modes. Every one of them reports to stdout and opens no
// window, but they are still reached through a QApplication, which aborts —
// silently, with nothing on stdout or stderr — when there is no display to
// connect to. Scanned before Qt is touched, because by the time the parser
// could tell us the process has already died.
const char *const kTerminalModes[] = {
    "check", "brief", "blocks", "html", "dispatch", "proof", "agents",
    "version", "help", "V", "h",
};

bool wantsTerminalMode(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        QByteArray arg(argv[i]);
        if (!arg.startsWith('-'))
            continue;
        arg = arg.mid(arg.startsWith("--") ? 2 : 1);
        const int eq = arg.indexOf('=');
        if (eq >= 0)
            arg.truncate(eq);
        for (const char *mode : kTerminalModes)
            if (arg == mode)
                return true;
    }
    return false;
}

// A terminal mode runs offscreen, always. It has no window to put anywhere,
// and the alternative is connecting to whatever display happens to be around
// — which means `galley --check` aborts over ssh and in a container while
// working on a desktop, and that is the wrong way round for the command that
// is also the regression test.
//
// Unconditional on purpose. Reading DISPLAY is not enough: a desktop session
// exports QT_QPA_PLATFORM (Hyprland sets `wayland;xcb`), and inheriting that
// into a headless CLI run is not somebody being deliberate. The GTK platform
// theme goes too — it opens its own display connection and aborts even under
// the offscreen platform.
void runOffscreen()
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_PLATFORMTHEME", "");
}

} // namespace

int main(int argc, char **argv)
{
    if (wantsTerminalMode(argc, argv))
        runOffscreen();

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("galley"));
#ifdef GALLEY_VERSION
    QApplication::setApplicationVersion(QStringLiteral(GALLEY_VERSION));
#endif
    QApplication::setOrganizationName(QStringLiteral("galley"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Galley — a review loop for books written in Markdown."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("book"),
                                 QStringLiteral("Directory of Markdown files "
                                                "(default: the current directory)"));
    QCommandLineOption checkOption(
        QStringLiteral("check"),
        QStringLiteral("Render every chapter and report block mapping, then exit."));
    parser.addOption(checkOption);
    QCommandLineOption briefOption(
        QStringLiteral("brief"),
        QStringLiteral("Print the open round's brief to stdout and exit."));
    parser.addOption(briefOption);
    QCommandLineOption blocksOption(
        QStringLiteral("blocks"),
        QStringLiteral("List the top-level blocks of one chapter and exit."),
        QStringLiteral("file"));
    parser.addOption(blocksOption);
    QCommandLineOption htmlOption(
        QStringLiteral("html"),
        QStringLiteral("Print one chapter's rendered HTML and exit."),
        QStringLiteral("file"));
    parser.addOption(htmlOption);
    QCommandLineOption agentsOption(
        QStringLiteral("agents"),
        QStringLiteral("List agent profiles, adding any newly installed, then exit."));
    parser.addOption(agentsOption);
    QCommandLineOption proofOption(
        QStringLiteral("proof"),
        QStringLiteral("Render the book to .galley/proof.pdf and exit."));
    parser.addOption(proofOption);
    QCommandLineOption dispatchOption(
        QStringLiteral("dispatch"),
        QStringLiteral("Send the open round to an agent from the terminal and exit."));
    parser.addOption(dispatchOption);
    QCommandLineOption onlyOption(
        QStringLiteral("only"),
        QStringLiteral("Dispatch only these comment ids (comma separated)."),
        QStringLiteral("ids"));
    parser.addOption(onlyOption);
    QCommandLineOption agentOption(
        QStringLiteral("agent"),
        QStringLiteral("Agent profile to dispatch to (default: the project's)."),
        QStringLiteral("name"));
    parser.addOption(agentOption);
    parser.process(app);

    // Every flag below is a terminal mode: it reports to stdout and has no
    // window to put a dialog in, so those keep the strict behaviour of taking
    // the current directory and failing loudly.
    const bool headless = parser.isSet(checkOption) || parser.isSet(briefOption)
        || parser.isSet(blocksOption) || parser.isSet(htmlOption)
        || parser.isSet(dispatchOption) || parser.isSet(proofOption);

    // Independent of any book, so it runs before one is opened.
    if (parser.isSet(agentsOption)) {
        QTextStream out(stdout);
        const QStringList added = AgentProfiles::rescan();
        for (const QString &name : added)
            out << "added   " << name << "\n";

        const auto profiles = AgentProfiles::load();
        const QString desktop = AgentProfiles::omarchyDefault();
        for (auto it = profiles.begin(); it != profiles.end(); ++it)
            out << "        " << it.key() << "\n";
        out << "\n" << AgentProfiles::configPath() << "\n";
        if (!desktop.isEmpty() && !profiles.contains(desktop))
            out << "\nYour desktop agent is " << desktop
                << ", which has no profile here.\n";
        return 0;
    }

    const QStringList args = parser.positionalArguments();
    QString root = args.isEmpty() ? QDir::currentPath() : args.first();

    auto *project = new Project;
    QString error;

    if (headless) {
        if (!project->open(root, &error)) {
            QTextStream(stderr) << "galley: " << error << "\n";
            return 1;
        }
    } else if (!openForWindow(project, args.isEmpty() ? QString() : root, &root)) {
        return 0;  // the chooser was dismissed; nothing to say about it
    }

    // First open on a book writes the inferred project file, so the spine is
    // visible and editable rather than re-guessed on every launch.
    if (project->wasImported()) {
        QString saveError;
        if (!project->save(&saveError))
            QTextStream(stderr) << "galley: " << saveError << "\n";
        else
            QTextStream(stdout)
                << "galley: wrote " << project->galleyDir() << "/project.toml\n";
    }

    // A book is the only fixture corpus that matters, so make it cheap to run
    // the parser over the whole thing and see where the two readings disagree.
    if (parser.isSet(checkOption)) {
        QTextStream out(stdout);
        int unmapped = 0, failed = 0;
        for (const Chapter &ch : project->chapters()) {
            Document d;
            if (!d.load(project->absolutePath(ch.relPath), ch.relPath)) {
                out << "  FAIL  " << ch.relPath << " — " << d.error() << "\n";
                ++failed;
                continue;
            }
            if (!d.blocksMapped())
                ++unmapped;
            out << (d.blocksMapped() ? "  ok    " : "  UNMAP ")
                << QString(ch.relPath).leftJustified(52) << d.blockCount() << " blocks\n";
        }
        out << "\n" << project->chapters().size() << " chapters, " << unmapped
            << " unmapped, " << failed << " failed\n";
        return failed ? 1 : 0;
    }

    if (parser.isSet(briefOption)) {
        const Round round = Round::loadOrCreateOpen(project->galleyDir());
        QTextStream(stdout) << Brief::generate(*project, round);
        return 0;
    }

    // Block indices are what a comment anchors to, so being able to see them
    // is the difference between debugging this and guessing at it.
    if (parser.isSet(blocksOption)) {
        const QString rel = parser.value(blocksOption);
        Document d;
        if (!d.load(project->absolutePath(rel), rel)) {
            QTextStream(stderr) << "galley: " << d.error() << "\n";
            return 1;
        }
        QTextStream out(stdout);
        out << rel << " — " << d.blockCount() << " blocks";
        if (d.anchorCount() > d.blockCount())
            out << " + " << (d.anchorCount() - d.blockCount()) << " footnotes";
        out << ", " << (d.blocksMapped() ? "mapped" : "UNMAPPED") << "\n\n";
        for (int i = 0; i < d.anchorCount(); ++i) {
            QString text = QString::fromUtf8(d.sourceForBlock(i)).simplified();
            if (text.size() > 84)
                text = text.left(81) + QStringLiteral("...");
            out << QStringLiteral("%1%2  %3\n")
                       .arg(i, 4)
                       .arg(i >= d.blockCount() ? QStringLiteral(" fn") : QStringLiteral("   "))
                       .arg(text);
        }
        return 0;
    }

    if (parser.isSet(htmlOption)) {
        const QString rel = parser.value(htmlOption);
        Document d;
        if (!d.load(project->absolutePath(rel), rel)) {
            QTextStream(stderr) << "galley: " << d.error() << "\n";
            return 1;
        }
        QTextStream out(stdout);
        for (const QString &n : d.notices())
            out << "<!-- notice: " << n << " -->\n";
        out << d.html();
        return 0;
    }

    if (parser.isSet(proofOption)) {
        Theme theme;
        Bridge bridge(project, &theme);
        QTextStream out(stdout);
        int result = 1;
        QObject::connect(&bridge, &Bridge::status, [&](const QString &lvl, const QString &m) {
            out << "galley: " << m << "\n";
            out.flush();
            result = (lvl == QStringLiteral("ok")) ? 0 : 1;
            QCoreApplication::quit();
        });
        bridge.proof();
        QTimer::singleShot(120000, &app, [&] { QCoreApplication::quit(); });
        app.exec();
        return result;
    }

    // The same dispatch the GUI performs, for people who would rather stay in
    // a terminal. Exits with the agent's exit code.
    if (parser.isSet(dispatchOption)) {
        Theme theme;
        Bridge bridge(project, &theme);
        QTextStream out(stdout);

        bool started = false;
        int result = 0;
        QObject::connect(&bridge, &Bridge::runStarted, [&](const QString &) { started = true; });
        QObject::connect(&bridge, &Bridge::logLine,
                         [&](const QString &l) { out << l << "\n"; out.flush(); });
        QObject::connect(&bridge, &Bridge::status, [&](const QString &, const QString &m) {
            out << "galley: " << m << "\n";
            out.flush();
        });
        QObject::connect(&bridge, &Bridge::runFinished, [&](int code, const QString &) {
            result = code;
            const QJsonObject res =
                QJsonDocument::fromJson(bridge.resultJson(0).toUtf8()).object();
            const QJsonObject sum = res[QStringLiteral("summary")].toObject();
            out << "\n";
            if (!res[QStringLiteral("reported")].toBool())
                out << "galley: the agent reported nothing per comment\n";
            for (const char *k : {"applied", "partial", "declined", "unreported", "deferred"}) {
                const int n = sum[QLatin1String(k)].toInt();
                if (n)
                    out << QStringLiteral("  %1  %2\n").arg(n, 3).arg(QLatin1String(k));
            }
            out.flush();
            QCoreApplication::quit();
        });

        QString onlyJson;
        if (parser.isSet(onlyOption)) {
            QJsonArray ids;
            const auto parts = parser.value(onlyOption).split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : parts)
                ids << p.trimmed();
            onlyJson = QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact));
        }
        bridge.dispatch(parser.value(agentOption), onlyJson);

        // dispatch() refuses synchronously on an empty round or an unknown
        // agent, and then there is no run to wait for.
        QTimer::singleShot(250, &app, [&] {
            if (!started) {
                result = 1;
                QCoreApplication::quit();
            }
        });

        app.exec();
        return result;
    }

    MainWindow window(project);
    window.show();
    return app.exec();
}
