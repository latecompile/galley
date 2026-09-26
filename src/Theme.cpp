#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QTimer>

#include <toml++/toml.hpp>

namespace {

// Keys Galley reads from colors.toml. The ANSI set is used for code
// highlighting so the book matches the terminal it was written in.
const char *kKeys[] = {
    "mode",        "accent",       "selection",     "muted",
    "background",  "dark_background", "darker_background", "lighter_background",
    "foreground",  "dark_foreground", "light_foreground",  "bright_foreground",
    "red",         "yellow",       "orange",        "green",
    "cyan",        "blue",         "magenta",       "brown",
    "bright_red",  "bright_yellow", "bright_green", "bright_cyan",
    "bright_blue", "bright_magenta",
};

QJsonObject fallbackPalette()
{
    return QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("dark")},
        {QStringLiteral("accent"), QStringLiteral("#7aa2f7")},
        {QStringLiteral("selection"), QStringLiteral("#2d3450")},
        {QStringLiteral("muted"), QStringLiteral("#565f89")},
        {QStringLiteral("background"), QStringLiteral("#1a1b26")},
        {QStringLiteral("dark_background"), QStringLiteral("#16161e")},
        {QStringLiteral("darker_background"), QStringLiteral("#101014")},
        {QStringLiteral("lighter_background"), QStringLiteral("#24283b")},
        {QStringLiteral("foreground"), QStringLiteral("#c0caf5")},
        {QStringLiteral("dark_foreground"), QStringLiteral("#9aa5ce")},
        {QStringLiteral("light_foreground"), QStringLiteral("#cfd8ff")},
        {QStringLiteral("bright_foreground"), QStringLiteral("#ffffff")},
        {QStringLiteral("red"), QStringLiteral("#f7768e")},
        {QStringLiteral("yellow"), QStringLiteral("#e0af68")},
        {QStringLiteral("orange"), QStringLiteral("#ff9e64")},
        {QStringLiteral("green"), QStringLiteral("#9ece6a")},
        {QStringLiteral("cyan"), QStringLiteral("#7dcfff")},
        {QStringLiteral("blue"), QStringLiteral("#7aa2f7")},
        {QStringLiteral("magenta"), QStringLiteral("#bb9af7")},
        {QStringLiteral("brown"), QStringLiteral("#a57a4c")},
    };
}

} // namespace

QString Theme::colorsPath()
{
    return QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
}

Theme::Theme(QObject *parent) : QObject(parent)
{
    reload();

    const QString path = colorsPath();
    if (QFile::exists(path)) {
        m_watcher.addPath(path);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this, path] {
            // Theme switches replace the file rather than rewriting it, which
            // drops the watch; re-add after a beat and re-read.
            QTimer::singleShot(120, this, [this, path] {
                if (!m_watcher.files().contains(path) && QFile::exists(path))
                    m_watcher.addPath(path);
                reload();
                emit changed(m_json);
            });
        });
    }
}

void Theme::reload()
{
    m_json = fallbackPalette();

    const QString path = colorsPath();
    if (!QFile::exists(path))
        return;

    try {
        auto tbl = toml::parse_file(path.toStdString());
        for (const char *key : kKeys)
            if (auto v = tbl[key].value<std::string>())
                m_json[QString::fromLatin1(key)] = QString::fromStdString(*v);
    } catch (const toml::parse_error &) {
        // Keep the fallback rather than half a palette.
    }
}
