#pragma once

#include <QByteArray>
#include <QList>

// Byte range of one top-level block in a Markdown source file.
struct SourceBlock {
    int start = 0;  // inclusive
    int end = 0;    // exclusive

    // A pandoc footnote definition — `[^label]: …` plus its indented
    // continuation. These are lifted out of the body before rendering and
    // re-emitted as a footnotes section, so they are not body blocks and must
    // not be counted as such.
    bool footnoteDef = false;

    // A run of link reference definitions — `[ref]: https://…`. They resolve
    // links elsewhere in the file and render nothing themselves, so md4c
    // emits no block for them. They must stay in the text md4c parses, or the
    // links they define break, but they are not blocks the reader can see.
    bool refDef = false;
};

// Finds the byte ranges of top-level (depth-0) blocks in Markdown source.
//
// This exists only to map a rendered block back to the source text behind it.
// It is a second, independent reading of the file, so it can disagree with
// md4c; Document reconciles the two by count and discards these ranges when
// they disagree. Nothing downstream is allowed to depend on it succeeding.
namespace BlockScanner {
QList<SourceBlock> scan(const QByteArray &source);
}
