#include "AgentProfiles.h"
#include "ModelDiscovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <cstdlib>
#include <sys/resource.h>

namespace {

void require(bool condition, const QString &message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), QStringLiteral("read %1").arg(path));
    return file.readAll();
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), QStringLiteral("open %1").arg(path));
    require(file.write(bytes) == bytes.size() && file.flush(), QStringLiteral("write %1").arg(path));
}

QJsonObject refresh(const QString &agent)
{
    ModelDiscovery discovery;
    QEventLoop loop;
    QJsonObject result;
    QObject::connect(&discovery, &ModelDiscovery::finished, [&](const QString &json) {
        result = QJsonDocument::fromJson(json.toUtf8()).object();
        loop.quit();
    });
    discovery.refresh(agent);
    if (result.isEmpty()) {
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    require(!result.isEmpty(), QStringLiteral("discovery completed"));
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    require(argc == 2, QStringLiteral("expected repository root"));
    const QDir root(QString::fromLocal8Bit(argv[1]));
    QTemporaryDir tmp;
    require(tmp.isValid(), QStringLiteral("temporary XDG roots"));
    qputenv("XDG_CONFIG_HOME", tmp.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", tmp.filePath(QStringLiteral("cache")).toUtf8());
    QDir().mkpath(tmp.filePath(QStringLiteral("config/galley")));
    const QString path = AgentProfiles::configPath();
    QTextStream out(stdout);

    const QByteArray original =
        "# Keep these exact bytes.\r\n"
        "codex-gpt-test-high = \"reserved\"\r\n"
        "[codex-gpt-test-high-2]\r\ncommand = []\r\n"
        "[manual]\r\ncommand = [\"true\", \"{prompt}\"]";
    write(path, original);
    QString error;
    const QString preview = AgentProfiles::modelProfileName("codex", "gpt-test", "high", nullptr, &error);
    require(preview == "codex-gpt-test-high-3", QStringLiteral("all TOML keys reserve names"));
    const QString added = AgentProfiles::addModel("codex", "gpt-test", "high", nullptr, &error);
    require(added == preview, error);
    const QByteArray appended = read(path);
    require(appended.startsWith(original), QStringLiteral("original CRLF and no final newline preserved"));
    const auto profiles = AgentProfiles::load();
    require(profiles.contains("manual") && profiles.contains(added), QStringLiteral("profiles still load"));
    bool existed = false;
    require(AgentProfiles::addModel("codex", "gpt-test", "high", &existed, &error) == added
            && existed && read(path) == appended, QStringLiteral("duplicate reused without writes"));
    out << "ok reserved names, exact original bytes, duplicate reuse\n";

    const QByteArray invalid = "# Do not append to a broken file.\n[broken\n";
    write(path, invalid);
    error.clear();
    require(AgentProfiles::modelProfileName("codex", "gpt-test", "high", nullptr, &error).isEmpty()
            && error.contains("invalid TOML"), QStringLiteral("preview reports invalid config"));
    require(AgentProfiles::addModel("codex", "gpt-test", "high", nullptr, &error).isEmpty()
            && error.contains("invalid TOML") && read(path) == invalid,
            QStringLiteral("invalid config stays intact"));
    out << "ok invalid TOML rejected before preview or append\n";

    // Force a short append followed by a flush failure, without depending on
    // permissions (which behave differently when tests are run as root).
    write(path, original);
    struct rlimit before;
    require(getrlimit(RLIMIT_FSIZE, &before) == 0, QStringLiteral("read file size limit"));
    struct rlimit limited = before;
    limited.rlim_cur = original.size() + 40;
    const auto previousSignal = std::signal(SIGXFSZ, SIG_IGN);
    require(setrlimit(RLIMIT_FSIZE, &limited) == 0, QStringLiteral("set file size limit"));
    error.clear();
    const QString failed = AgentProfiles::addModel("codex", "gpt-test", "high", nullptr, &error);
    require(setrlimit(RLIMIT_FSIZE, &before) == 0, QStringLiteral("restore file size limit"));
    std::signal(SIGXFSZ, previousSignal);
    require(failed.isEmpty() && error.contains("could not append"), QStringLiteral("failed write reported"));
    require(read(path) == original, QStringLiteral("short append rolled back to original bytes"));
    out << "ok partial append rejected and rolled back\n";

    for (const QString &agent : {QStringLiteral("codex"), QStringLiteral("grok"),
                                 QStringLiteral("claude"), QStringLiteral("opencode"),
                                 QStringLiteral("gemini")}) {
        const QString extension = agent == "codex" ? ".json" : ".txt";
        const auto catalog = ModelDiscovery::parse(agent, read(root.filePath("test/models/" + agent + extension)));
        require(!catalog.contains("error"), QStringLiteral("parse %1 fixture").arg(agent));
        out << ModelDiscovery::format(catalog);
    }
    const auto codex = ModelDiscovery::parse("codex", read(root.filePath("test/models/codex.json")));
    require(codex["models"].toArray().size() == 2, QStringLiteral("only listed Codex models visible"));
    const auto claude = ModelDiscovery::parse("claude", read(root.filePath("test/models/claude.txt")));
    require(claude["efforts"].toArray().size() == 4
            && claude["models"].toArray().first().toObject()["efforts"].toArray().isEmpty(),
            QStringLiteral("Claude efforts apply to full model names too"));
    for (const QString &agent : {QStringLiteral("codex"), QStringLiteral("grok"),
                                 QStringLiteral("claude"), QStringLiteral("opencode"),
                                 QStringLiteral("gemini")})
        require(ModelDiscovery::parse(agent, "unrecognized output").contains("error"),
                QStringLiteral("bad %1 output reports an error").arg(agent));
    out << "ok CLI parsers reject malformed discovery output\n";

    const auto success = refresh("codex");
    require(!success.contains("error") && !success.contains("cacheError"), QStringLiteral("cache saved"));
    const QByteArray goodCache = read(ModelDiscovery::cachePath());
    const QString previousPath = QString::fromLocal8Bit(qgetenv("PATH"));
    qputenv("PATH", tmp.filePath("missing-bin").toUtf8());
    require(refresh("codex").contains("error"), QStringLiteral("discovery failure reported"));
    require(read(ModelDiscovery::cachePath()) == goodCache, QStringLiteral("failed refresh retains cache"));
    qputenv("PATH", previousPath.toLocal8Bit());
    // A regular file blocks directory creation, deterministically.
    const QString blocker = tmp.filePath("blocked-cache");
    write(blocker, "not a directory");
    qputenv("XDG_CACHE_HOME", blocker.toUtf8());
    const auto unsaved = refresh("codex");
    require(!unsaved.contains("error") && unsaved.contains("cacheError")
            && !unsaved["models"].toArray().isEmpty(), QStringLiteral("fresh models survive cache save error"));
    out << "ok discovery failure retains cache; cache write failure is reported\n";
    QProcess terminal;
    terminal.start(root.filePath("galley"), {QStringLiteral("--models"), QStringLiteral("codex")});
    require(terminal.waitForFinished(20000) && terminal.exitCode() == 1
            && terminal.readAllStandardOutput().contains("could not save model cache"),
            QStringLiteral("--models returns failure and explains cache save error"));
    out << "ok --models returns failure when the cache cannot be saved\n";

    // Exercise the actual picker in Qt's JS engine with a small DOM adapter.
    // QtQml is already a Galley dependency, so make check needs no browser or Node.
    QJSEngine engine;
    const QString source = QString::fromUtf8(read(root.filePath("web/app.js")));
    const qsizetype start = source.indexOf("function modelPicker(");
    const qsizetype end = source.indexOf("function showDispatch(", start);
    require(start >= 0 && end > start, QStringLiteral("picker function found"));
    const QString script = QString::fromUtf8(read(root.filePath("test/model_picker.js")))
        + QLatin1Char('\n') + source.mid(start, end - start) + QStringLiteral("\ntestModelPicker();");
    const QJSValue result = engine.evaluate(script);
    require(!result.isError(), QStringLiteral("picker test: %1 (line %2)")
                .arg(result.toString()).arg(result.property("lineNumber").toInt()));
    out << "ok picker preserves model and effort through typing and refresh\n"
        << "ok picker ignores stale previews after edits, redraws and close\n"
        << "ok picker uses per-model and CLI-wide efforts; preview errors disable Add\n";
    return 0;
}
