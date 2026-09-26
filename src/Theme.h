#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QString>

// The current Omarchy theme, as CSS custom properties.
//
// Omarchy rewrites ~/.local/state/omarchy/current/theme/colors.toml when the
// theme changes, so watching that one file is enough to follow a theme switch
// live. Falls back to a neutral palette when the file is not there, which
// keeps Galley usable off Omarchy.
class Theme : public QObject
{
    Q_OBJECT

public:
    explicit Theme(QObject *parent = nullptr);

    QJsonObject json() const { return m_json; }
    static QString colorsPath();

signals:
    void changed(const QJsonObject &json);

private:
    void reload();

    QFileSystemWatcher m_watcher;
    QJsonObject m_json;
};
