#pragma once

#include <QMainWindow>

class Bridge;
class Project;
class QPlainTextEdit;
class QSplitter;
class QWebEngineView;
class Theme;

// The shell: a web view holding the reading and review surfaces, and a log
// pane that appears while an agent or a build is running.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Project *project, QWidget *parent = nullptr);

private:
    void appendLog(const QString &line);
    void setLogVisible(bool visible);

    Project *m_project;
    Theme *m_theme;
    Bridge *m_bridge;
    QWebEngineView *m_view;
    QPlainTextEdit *m_log;
    QWidget *m_logPane;
    QSplitter *m_splitter;
};
