#include "PdfExporter.h"

#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QTimer>
#include <QWebEnginePage>

PdfExporter::PdfExporter(QObject *parent)
    : QObject(parent)
    , m_page(new QWebEnginePage(this))
{
    connect(m_page, &QWebEnginePage::pdfPrintingFinished, this,
            [this](const QString &path, bool ok) {
                m_busy = false;
                emit finished(ok, path);
            });
}

void PdfExporter::write(const QString &html, const QString &outPath)
{
    if (m_busy)
        return;
    m_busy = true;
    m_path = outPath;

    // setHtml finishing means the DOM is up; laying out and loading fonts
    // takes a moment longer, and printing before that yields a page of
    // fallback type.
    connect(
        m_page, &QWebEnginePage::loadFinished, this,
        [this](bool ok) {
            if (!ok) {
                m_busy = false;
                emit finished(false, m_path);
                return;
            }
            QTimer::singleShot(250, this, [this] {
                const QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                                         QMarginsF(18, 18, 18, 20), QPageLayout::Millimeter);
                m_page->printToPdf(m_path, layout);
            });
        },
        Qt::SingleShotConnection);

    m_page->setHtml(html);
}
