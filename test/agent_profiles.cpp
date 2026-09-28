#include "AgentProfiles.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

void printExpansion(QTextStream &out, const QString &label, const AgentProfile &profile)
{
    out << label << ": "
        << AgentProfiles::expand(profile, QStringLiteral("PROMPT"), QStringLiteral("BRIEF"),
                                 QStringLiteral("ROOT"))
               .join(QStringLiteral(" | "))
        << "\n";
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    AgentProfile profile;
    profile.name = QStringLiteral("codex-gpt-test-high");
    profile.command = {QStringLiteral("codex"), QStringLiteral("exec"),
                       QStringLiteral("--sandbox"), QStringLiteral("workspace-write"),
                       QStringLiteral("-m"), QStringLiteral("{model}"),
                       QStringLiteral("-c"),
                       QStringLiteral("model_reasoning_effort=\"{effort}\""),
                       QStringLiteral("{prompt}"), QStringLiteral("{brief}"),
                       QStringLiteral("{root}")};

    profile.model = QStringLiteral("gpt-test");
    profile.effort = QStringLiteral("high");
    out << "label: " << AgentProfiles::label(profile) << "\n";
    printExpansion(out, QStringLiteral("model and effort"), profile);

    profile.effort.clear();
    printExpansion(out, QStringLiteral("model only"), profile);

    profile.model.clear();
    profile.effort = QStringLiteral("high");
    printExpansion(out, QStringLiteral("effort only"), profile);

    profile.effort.clear();
    printExpansion(out, QStringLiteral("neither"), profile);

    profile.model = QStringLiteral("gpt-{effort}");
    profile.effort = QStringLiteral("high-{model}");
    out << "literal placeholders: "
        << AgentProfiles::expand(profile, QStringLiteral("Keep {model} and {effort}."),
                                 QStringLiteral("BRIEF/{model}"), QStringLiteral("ROOT/{effort}"))
               .join(QStringLiteral(" | ")) << "\n";
    return 0;
}
