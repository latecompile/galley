#include "ModelDiscovery.h"

#include "AgentProfiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {

constexpr int kTimeoutMs = 15000;

QString compactJson(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString withoutAnsi(QString text)
{
    static const QRegularExpression ansi(
        QStringLiteral("\\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(ansi);
    return text;
}

QJsonObject baseCatalog(const QString &agent)
{
    QString effortMode = QStringLiteral("none");
    if (agent == QStringLiteral("grok") || agent == QStringLiteral("opencode"))
        effortMode = QStringLiteral("free");
    else if (agent == QStringLiteral("codex") || agent == QStringLiteral("claude"))
        effortMode = QStringLiteral("list");
    return QJsonObject{{QStringLiteral("agent"), agent},
                       {QStringLiteral("models"), QJsonArray()},
                       {QStringLiteral("effortMode"), effortMode},
                       {QStringLiteral("cached"), false}};
}

QJsonArray strings(const QStringList &values)
{
    return QJsonArray::fromStringList(values);
}

QJsonObject modelObject(const QString &id, const QString &display, bool isDefault,
                        const QStringList &efforts = {}, const QString &defaultEffort = {})
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("displayName"), display.isEmpty() ? id : display},
                       {QStringLiteral("default"), isDefault},
                       {QStringLiteral("efforts"), strings(efforts)},
                       {QStringLiteral("defaultEffort"), defaultEffort}};
}

QJsonObject parseCodex(const QByteArray &bytes, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("codex returned invalid model JSON: %1")
                     .arg(parseError.errorString());
        return {};
    }
    if (!doc.object()[QStringLiteral("models")].isArray()) {
        *error = QStringLiteral("codex returned JSON without a models array");
        return {};
    }

    QJsonObject result = baseCatalog(QStringLiteral("codex"));
    QJsonArray models;
    const QJsonArray raw = doc.object()[QStringLiteral("models")].toArray();
    for (const QJsonValue &value : raw) {
        const QJsonObject item = value.toObject();
        if (item[QStringLiteral("visibility")].toString() != QStringLiteral("list"))
            continue;
        const QString id = item[QStringLiteral("slug")].toString();
        if (id.isEmpty())
            continue;
        QStringList efforts;
        for (const QJsonValue &level : item[QStringLiteral("supported_reasoning_levels")].toArray()) {
            const QString effort = level.toObject()[QStringLiteral("effort")].toString();
            if (!effort.isEmpty())
                efforts << effort;
        }
        models << modelObject(id, item[QStringLiteral("display_name")].toString(), false,
                              efforts, item[QStringLiteral("default_reasoning_level")].toString());
    }
    result[QStringLiteral("models")] = models;
    if (models.isEmpty())
        result[QStringLiteral("message")] = QStringLiteral("Codex returned no visible models.");
    return result;
}

QJsonObject parseGrok(const QString &raw, QString *error)
{
    const QString text = withoutAnsi(raw);
    const QRegularExpression defaultPattern(QStringLiteral("^Default model:\\s*(\\S+)"),
                                            QRegularExpression::MultilineOption);
    const QString defaultModel = defaultPattern.match(text).captured(1);
    const int heading = text.indexOf(QStringLiteral("Available models:"));
    if (heading < 0) {
        *error = QStringLiteral("grok output did not contain an Available models section");
        return {};
    }

    QJsonArray models;
    const QStringList lines = text.mid(heading).split(QLatin1Char('\n'));
    const QRegularExpression linePattern(QStringLiteral("^\\s*[*-]\\s+(\\S+?)(?:\\s+\\(default\\))?\\s*$"));
    for (const QString &line : lines) {
        const auto match = linePattern.match(line);
        if (!match.hasMatch())
            continue;
        const QString id = match.captured(1);
        models << modelObject(id, id, line.contains(QStringLiteral("(default)"))
                                         || id == defaultModel);
    }
    if (models.isEmpty()) {
        *error = QStringLiteral("grok returned an empty model list");
        return {};
    }
    QJsonObject result = baseCatalog(QStringLiteral("grok"));
    result[QStringLiteral("models")] = models;
    return result;
}

QJsonObject parseClaude(const QString &raw, QString *error)
{
    const QString text = withoutAnsi(raw);
    const int modelAt = text.indexOf(QStringLiteral("--model <model>"));
    if (modelAt < 0) {
        *error = QStringLiteral("claude --help did not describe --model");
        return {};
    }

    int aliasesEnd = text.indexOf(QStringLiteral("or a\n"), modelAt);
    if (aliasesEnd < 0)
        aliasesEnd = text.indexOf(QStringLiteral("model's full name"), modelAt);
    if (aliasesEnd < 0)
        aliasesEnd = qMin(text.size(), modelAt + 600);
    const QString aliasesText = text.mid(modelAt, aliasesEnd - modelAt);
    const QRegularExpression quoted(QStringLiteral("'([^']+)'"));
    QStringList aliases;
    auto matches = quoted.globalMatch(aliasesText);
    while (matches.hasNext()) {
        const QString alias = matches.next().captured(1);
        if (!aliases.contains(alias))
            aliases << alias;
    }

    QStringList efforts;
    const int effortAt = text.indexOf(QStringLiteral("--effort <level>"));
    if (effortAt >= 0) {
        const QString effortText = text.mid(effortAt, 300);
        const auto values = QRegularExpression(QStringLiteral("\\((low[^)]*)\\)"))
                                .match(effortText);
        if (values.hasMatch()) {
            for (const QString &value : values.captured(1).split(QLatin1Char(',')))
                efforts << value.trimmed();
        }
    }

    QJsonObject result = baseCatalog(QStringLiteral("claude"));
    result[QStringLiteral("efforts")] = strings(efforts);
    QJsonArray models;
    for (const QString &alias : aliases)
        models << modelObject(alias, alias, false);
    result[QStringLiteral("models")] = models;
    if (models.isEmpty())
        result[QStringLiteral("message")] =
            QStringLiteral("Claude lists no aliases here; enter a full model name.");
    return result;
}

QJsonObject parseOpenCode(const QString &raw, QString *error)
{
    const QString text = withoutAnsi(raw);
    const QRegularExpression modelLine(QStringLiteral("^[A-Za-z0-9_.-]+/\\S+$"));
    QJsonArray models;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString id = line.trimmed();
        if (modelLine.match(id).hasMatch())
            models << modelObject(id, id, false);
    }
    if (models.isEmpty() && !text.trimmed().isEmpty()) {
        *error = QStringLiteral("opencode returned output Galley could not read as provider/model names");
        return {};
    }
    QJsonObject result = baseCatalog(QStringLiteral("opencode"));
    result[QStringLiteral("models")] = models;
    if (models.isEmpty())
        result[QStringLiteral("message")] = QStringLiteral("OpenCode returned no models; enter one as provider/model.");
    return result;
}

QJsonObject parseGemini(const QString &raw, QString *error)
{
    if (!withoutAnsi(raw).contains(QStringLiteral("--model"))) {
        *error = QStringLiteral("gemini --help did not describe --model");
        return {};
    }
    QJsonObject result = baseCatalog(QStringLiteral("gemini"));
    result[QStringLiteral("message")] =
        QStringLiteral("Gemini does not expose a model list; enter a model name.");
    return result;
}

QJsonObject parseCatalog(const QString &agent, const QByteArray &stdoutBytes, QString *error)
{
    if (agent == QStringLiteral("codex"))
        return parseCodex(stdoutBytes, error);
    const QString output = QString::fromUtf8(stdoutBytes);
    if (agent == QStringLiteral("grok"))
        return parseGrok(output, error);
    if (agent == QStringLiteral("claude"))
        return parseClaude(output, error);
    if (agent == QStringLiteral("opencode"))
        return parseOpenCode(output, error);
    if (agent == QStringLiteral("gemini"))
        return parseGemini(output, error);
    *error = QStringLiteral("unknown model-capable agent '%1'").arg(agent);
    return {};
}

bool saveCatalog(const QJsonObject &catalog, QString *error)
{
    const QString path = ModelDiscovery::cachePath();
    QJsonObject root;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly))
        root = QJsonDocument::fromJson(in.readAll()).object();
    root[QStringLiteral("version")] = 1;
    QJsonObject agents = root[QStringLiteral("agents")].toObject();
    QJsonObject stored = catalog;
    stored.remove(QStringLiteral("cached"));
    agents[catalog[QStringLiteral("agent")].toString()] = stored;
    root[QStringLiteral("agents")] = agents;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        *error = QStringLiteral("could not save model cache %1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

} // namespace

ModelDiscovery::ModelDiscovery(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_timer(new QTimer(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_timer->setSingleShot(true);

    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_done)
            return;
        m_timedOut = true;
        m_process->kill();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_done || error != QProcess::FailedToStart)
            return;
        QJsonObject result = baseCatalog(m_agent);
        result[QStringLiteral("error")] = QStringLiteral("could not start %1: %2")
                                               .arg(m_agent, m_process->errorString());
        complete(result, false);
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        if (m_done)
            return;
        m_timer->stop();
        if (m_timedOut) {
            QJsonObject result = baseCatalog(m_agent);
            result[QStringLiteral("error")] =
                QStringLiteral("%1 model discovery timed out after 15 seconds").arg(m_agent);
            complete(result, false);
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            QString detail = withoutAnsi(QString::fromUtf8(m_process->readAllStandardError())).trimmed();
            if (detail.isEmpty())
                detail = withoutAnsi(QString::fromUtf8(m_process->readAllStandardOutput())).trimmed();
            if (detail.size() > 500)
                detail = detail.left(497) + QStringLiteral("...");
            QJsonObject result = baseCatalog(m_agent);
            result[QStringLiteral("error")] = QStringLiteral("%1 model discovery exited %2%3")
                .arg(m_agent).arg(exitCode).arg(detail.isEmpty() ? QString() : QStringLiteral(": %1").arg(detail));
            complete(result, false);
            return;
        }

        QJsonObject result = parse(m_agent, m_process->readAllStandardOutput());
        if (result.contains(QStringLiteral("error"))) {
            complete(result, false);
            return;
        }
        result[QStringLiteral("refreshedAt")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        complete(result, true);
    });
}

QString ModelDiscovery::cachePath()
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation))
                            .filePath(QStringLiteral("galley"));
    return QDir(dir).filePath(QStringLiteral("models.json"));
}

QJsonObject ModelDiscovery::parse(const QString &agent, const QByteArray &output)
{
    QString error;
    QJsonObject result = parseCatalog(agent, output, &error);
    if (result.isEmpty()) {
        result = baseCatalog(agent);
        result[QStringLiteral("error")] = error;
    }
    return result;
}

QJsonObject ModelDiscovery::cached(const QString &agent)
{
    QJsonObject result = baseCatalog(agent);
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return result;
    const QJsonObject stored = QJsonDocument::fromJson(file.readAll())
                                   .object()[QStringLiteral("agents")]
                                   .toObject()[agent]
                                   .toObject();
    if (stored.isEmpty())
        return result;
    result = stored;
    result[QStringLiteral("agent")] = agent;
    result[QStringLiteral("cached")] = true;
    return result;
}

QString ModelDiscovery::format(const QJsonObject &catalog)
{
    QString out;
    QTextStream stream(&out);
    stream << catalog[QStringLiteral("agent")].toString() << " models";
    const QString refreshed = catalog[QStringLiteral("refreshedAt")].toString();
    if (!refreshed.isEmpty())
        stream << " — refreshed " << refreshed;
    stream << "\n";
    if (!catalog[QStringLiteral("error")].toString().isEmpty()) {
        stream << "  error: " << catalog[QStringLiteral("error")].toString() << "\n";
        return out;
    }
    for (const QJsonValue &value : catalog[QStringLiteral("models")].toArray()) {
        const QJsonObject model = value.toObject();
        const QString id = model[QStringLiteral("id")].toString();
        const QString display = model[QStringLiteral("displayName")].toString();
        stream << (model[QStringLiteral("default")].toBool() ? "* " : "  ");
        stream << (display == id ? id : QStringLiteral("%1 [%2]").arg(display, id)) << "\n";
        QStringList effortLabels;
        const QString defaultEffort = model[QStringLiteral("defaultEffort")].toString();
        for (const QJsonValue &effortValue : model[QStringLiteral("efforts")].toArray()) {
            QString effort = effortValue.toString();
            if (effort == defaultEffort)
                effort += QStringLiteral(" (default)");
            effortLabels << effort;
        }
        if (!effortLabels.isEmpty())
            stream << "    efforts: " << effortLabels.join(QStringLiteral(", ")) << "\n";
    }
    const QString message = catalog[QStringLiteral("message")].toString();
    const QJsonArray efforts = catalog[QStringLiteral("efforts")].toArray();
    QStringList effortLabels;
    for (const QJsonValue &effort : efforts)
        effortLabels << effort.toString();
    if (!effortLabels.isEmpty())
        stream << "  efforts: " << effortLabels.join(QStringLiteral(", ")) << "\n";
    if (!message.isEmpty())
        stream << "  " << message << "\n";
    if (catalog.contains(QStringLiteral("cacheError")))
        stream << "  warning: " << catalog[QStringLiteral("cacheError")].toString() << "\n";
    return out;
}

void ModelDiscovery::refresh(const QString &agent)
{
    if (!m_done) {
        QJsonObject result = baseCatalog(agent);
        result[QStringLiteral("error")] = QStringLiteral("another model refresh is already running");
        emit finished(compactJson(result));
        return;
    }
    if (!AgentProfiles::supportsModelAgent(agent)) {
        QJsonObject result = baseCatalog(agent);
        result[QStringLiteral("error")] = QStringLiteral("unknown model-capable agent '%1'").arg(agent);
        emit finished(compactJson(result));
        return;
    }
    const QString program = QStandardPaths::findExecutable(agent);
    if (program.isEmpty()) {
        QJsonObject result = baseCatalog(agent);
        result[QStringLiteral("error")] = QStringLiteral("%1 is not installed on PATH").arg(agent);
        emit finished(compactJson(result));
        return;
    }

    QStringList arguments;
    if (agent == QStringLiteral("codex"))
        arguments = {QStringLiteral("debug"), QStringLiteral("models")};
    else if (agent == QStringLiteral("grok"))
        arguments = {QStringLiteral("models")};
    else if (agent == QStringLiteral("claude") || agent == QStringLiteral("gemini"))
        arguments = {QStringLiteral("--help")};
    else if (agent == QStringLiteral("opencode"))
        arguments = {QStringLiteral("models"), QStringLiteral("--refresh")};

    m_agent = agent;
    m_done = false;
    m_timedOut = false;
    m_process->setProgram(program);
    m_process->setArguments(arguments);
    m_process->start();
    m_process->closeWriteChannel();
    m_timer->start(kTimeoutMs);
}

void ModelDiscovery::complete(const QJsonObject &catalog, bool save)
{
    if (m_done)
        return;
    m_done = true;
    m_timer->stop();
    QJsonObject result = catalog;
    QString error;
    if (save && !saveCatalog(catalog, &error))
        result[QStringLiteral("cacheError")] = error;
    emit finished(compactJson(result));
}
