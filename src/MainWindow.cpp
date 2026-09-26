#include "MainWindow.h"

#include "Bridge.h"
#include "Project.h"
#include "Theme.h"

#include <QAction>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSplitter>
#include <QStatusBar>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

MainWindow::MainWindow(Project *project, QWidget *parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_theme(new Theme(this))
    , m_view(new QWebEngineView(this))
    , m_log(new QPlainTextEdit(this))
    , m_splitter(new QSplitter(Qt::Vertical, this))
{
    setWindowTitle(QStringLiteral("Galley — %1").arg(project->name()));
    resize(1280, 860);

    m_bridge = new Bridge(project, m_theme, this);
    m_bridge->setDialogParent(this);

    auto *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("galley"), m_bridge);
    m_view->page()->setWebChannel(channel);

    // Local images referenced from the book need to resolve from qrc pages.
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_view->settings()->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);

    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFont(QStringLiteral("monospace"), 10));
    m_log->setFrameShape(QFrame::NoFrame);

    // The pane opens itself whenever something runs, so it has to say how to
    // get rid of it. Someone who pressed `p`, got a PDF and is now looking at
    // a wall of log has no other clue.
    m_logPane = new QWidget(this);
    auto *column = new QVBoxLayout(m_logPane);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_log);

    auto *footer = new QWidget(m_logPane);
    auto *row = new QHBoxLayout(footer);
    row->setContentsMargins(10, 2, 6, 3);
    row->addStretch();

    auto *hint = new QLabel(QStringLiteral("Ctrl+L to close"), footer);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    hint->setFont(small);
    hint->setEnabled(false);
    row->addWidget(hint);

    auto *shut = new QToolButton(footer);
    shut->setText(QStringLiteral("✕"));
    shut->setAutoRaise(true);
    shut->setToolTip(QStringLiteral("Close the log (Ctrl+L)"));
    connect(shut, &QToolButton::clicked, this, [this] { setLogVisible(false); });
    row->addWidget(shut);

    column->addWidget(footer);
    m_logPane->hide();

    m_splitter->addWidget(m_view);
    m_splitter->addWidget(m_logPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    setCentralWidget(m_splitter);

    connect(m_bridge, &Bridge::bookChanged, this, [this](const QString &json) {
        const auto name = QJsonDocument::fromJson(json.toUtf8())
                              .object()
                              .value(QStringLiteral("name"))
                              .toString();
        setWindowTitle(QStringLiteral("Galley — %1").arg(name));
    });
    connect(m_bridge, &Bridge::logLine, this, &MainWindow::appendLog);
    connect(m_bridge, &Bridge::runStarted, this, [this](const QString &) {
        m_log->clear();
        setLogVisible(true);
    });
    connect(m_bridge, &Bridge::status, this, [this](const QString &, const QString &msg) {
        statusBar()->showMessage(msg, 8000);
    });
    connect(m_bridge, &Bridge::runProgress, this, [this](int, const QString &pretty) {
        statusBar()->showMessage(QStringLiteral("running — %1  (Ctrl+L for the log)").arg(pretty));
    });

    // Ctrl+L is the only shortcut the shell owns; everything else belongs to
    // the page, which knows what is focused.
    auto *toggleLog = new QAction(this);
    toggleLog->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(toggleLog, &QAction::triggered, this,
            [this] { setLogVisible(m_logPane->isHidden()); });
    addAction(toggleLog);

    m_view->load(QUrl(QStringLiteral("qrc:/index.html")));
}

void MainWindow::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void MainWindow::setLogVisible(bool visible)
{
    m_logPane->setVisible(visible);
    if (visible)
        m_splitter->setSizes({height() * 2 / 3, height() / 3});
}
