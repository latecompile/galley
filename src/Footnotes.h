#pragma once

#include "BlockScanner.h"

#include <QByteArray>
#include <QList>
#include <QString>

// One pandoc footnote definition, lifted out of the body.
struct Footnote {
    QString label;
    QByteArray markdown;  // the definition's content, dedented
    int number = 0;       // assigned on first reference; 0 means unreferenced
    SourceBlock block;    // where the definition lives in the source
};

// Pandoc footnotes, which md4c knows nothing about.
//
// Definitions are removed from the text md4c parses and re-emitted as a
// section at the end of the chapter; references become superscript links.
// Doing this in C++ rather than JS keeps the rendered HTML self-contained,
// which is what will let the same markup be printed to PDF later.
namespace Footnotes {

// Splits scanned blocks into body blocks and footnote definitions.
QList<Footnote> extract(const QByteArray &source, const QList<SourceBlock> &all,
                        QList<SourceBlock> *body);

// Rebuilds the source with the footnote definitions gone, for md4c to parse.
// Takes every scanned block rather than the body ones, because a link
// reference definition is not a body block but the text still needs it.
QByteArray bodySource(const QByteArray &source, const QList<SourceBlock> &all);

// Pandoc concatenates every chapter into one document, so footnote anchors
// have to be unique across the whole book, not just within a file. A short
// digest of the chapter's path namespaces them.
QString idScope(const QString &relPath);

} // namespace Footnotes
