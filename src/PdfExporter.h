#pragma once

#include <QObject>
#include <QString>

class QWebEnginePage;

// Renders the whole book to a PDF using Galley's own renderer.
//
// Not a replacement for a book's pandoc build, which has real typesetting
// behind it. This is the proof copy: it needs nothing installed, it is what
// the reader just read — Galley's footnotes, Galley's code blocks — and it
// means a book with no build script of its own is not left without a PDF.
class PdfExporter : public QObject
{
    Q_OBJECT

public:
    explicit PdfExporter(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void write(const QString &html, const QString &outPath);

signals:
    void finished(bool ok, const QString &path);

private:
    QWebEnginePage *m_page;
    QString m_path;
    bool m_busy = false;
};
