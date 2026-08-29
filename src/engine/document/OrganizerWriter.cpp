#include "engine/document/OrganizerWriter.hpp"

#include "app/PdfPwStore.hpp"
#include "app/SafeWrite.hpp"

#include <QDebug>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFAcroFormDocumentHelper.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <map>
#  include <memory>
#endif

bool OrganizerWriter::writeVector(const QString &outPath)
{
#ifndef HAVE_QPDF
    Q_UNUSED(outPath)
    return false;
#else
    try {
        QPDF out;
        out.emptyPDF();
        QPDFPageDocumentHelper outPages(out);
        QPDFAcroFormDocumentHelper outForms(out);

        /// One QPDF per source file, opened lazily and shared by all pages that come from it.
        struct Source {
            std::unique_ptr<QPDF>                       pdf;
            std::unique_ptr<QPDFAcroFormDocumentHelper> forms;
            std::vector<QPDFPageObjectHelper>           pages;
        };
        std::map<QString, Source> sources;

        auto sourceFor = [&](const QString &path) -> Source * {
            auto it = sources.find(path);
            if (it != sources.end()) return &it->second;

            Source s;
            s.pdf = std::make_unique<QPDF>();
            const std::string pw = PdfPwStore::forQpdf(path);
            s.pdf->processFile(path.toLocal8Bit().constData(),
                               pw.empty() ? nullptr : pw.c_str());
            s.forms = std::make_unique<QPDFAcroFormDocumentHelper>(*s.pdf);
            s.pages = QPDFPageDocumentHelper(*s.pdf).getAllPages();
            return &sources.emplace(path, std::move(s)).first->second;
        };

        auto blankSizePt = [&](int at) -> QSizeF {
            for (int i = at - 1; i >= 0; --i)
                if (!m_pages[i].isBlank && m_docs.contains(m_pages[i].pdfPath))
                    return m_docs[m_pages[i].pdfPath]->pageSizePts(m_pages[i].pageIndex);
            for (int i = at + 1; i < m_pages.size(); ++i)
                if (!m_pages[i].isBlank && m_docs.contains(m_pages[i].pdfPath))
                    return m_docs[m_pages[i].pdfPath]->pageSizePts(m_pages[i].pageIndex);
            return QSizeF(595.0, 842.0);
        };

        for (int i = 0; i < m_pages.size(); ++i) {
            const OrganizerPage &e = m_pages[i];

            if (e.isBlank) {
                const QSizeF sz = blankSizePt(i);
                QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
                page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
                page.replaceKey("/MediaBox", QPDFObjectHandle::newFromRectangle(
                    QPDFObjectHandle::Rectangle(0, 0, sz.width(), sz.height())));
                page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
                page.replaceKey("/Contents", QPDFObjectHandle::newStream(&out, ""));
                outPages.addPage(QPDFPageObjectHelper(out.makeIndirectObject(page)), false);
            } else {
                Source *src = sourceFor(e.pdfPath);
                const auto idx = static_cast<std::size_t>(e.pageIndex);
                if (e.pageIndex < 0 || idx >= src->pages.size()) continue;
                outPages.addPage(src->pages[idx], false);
            }

            auto added = outPages.getAllPages();
            if (added.empty()) continue;
            QPDFPageObjectHelper newPage = added.back();

            if (!e.isBlank) {
                Source *src = sourceFor(e.pdfPath);
                const auto idx = static_cast<std::size_t>(e.pageIndex);
                if (idx < src->pages.size()) {

                    outForms.fixCopiedAnnotations(
                        newPage.getObjectHandle(),
                        src->pages[idx].getObjectHandle(),
                        *src->forms);
                }
            }

            if (e.rotation != 0)
                newPage.rotatePage(e.rotation, true);
        }

        const QString staging = SafeWrite::stagingPath(outPath);
        if (staging.isEmpty()) return false;
        {
            QPDFWriter writer(out, staging.toLocal8Bit().constData());

            QString protectWith;
            for (const OrganizerPage &e : m_pages) {
                if (e.isBlank) continue;
                const QString pw = PdfPwStore::get(e.pdfPath);
                if (!pw.isEmpty()) { protectWith = pw; break; }
            }
            if (!protectWith.isEmpty()) {
                const std::string pass = protectWith.toStdString();

                writer.setR6EncryptionParameters(
                    pass.c_str(), pass.c_str(),
                      true,   true,   true,
                      true,   true,
                      true, qpdf_r3p_full,   true);
                PdfPwStore::set(outPath, protectWith);
            }
            writer.write();
        }
        return SafeWrite::commit(staging, outPath);

    } catch (const std::exception &ex) {
        qWarning() << "[QPDF] organizer vector save failed:" << ex.what();
        return false;
    }
#endif
}

bool OrganizerWriter::verifyWritten(const QString &path) const
{
#ifdef HAVE_PDF_RENDERING
    if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
        qWarning() << "[Organizer] nothing was written to" << path;
        return false;
    }

    bool needsPassword = false;
    std::unique_ptr<OrganizerDoc> check(
        OrganizerDoc::load(path, PdfPwStore::get(path), &needsPassword));
    for (const OrganizerPage &e : m_pages) {
        if (check || !needsPassword) break;
        const QString pw = PdfPwStore::get(e.pdfPath);
        if (pw.isEmpty()) continue;
        check.reset(OrganizerDoc::load(path, pw, &needsPassword));
        if (check) PdfPwStore::set(path, pw);
    }
    if (!check) {
        qWarning() << "[Organizer] the written file cannot be opened:" << path;
        return false;
    }
    if (check->pageCount() != m_pages.size()) {
        qWarning() << "[Organizer] wrote" << check->pageCount()
                   << "pages, expected" << m_pages.size();
        return false;
    }
    return true;
#else
    Q_UNUSED(path)
    return true;
#endif
}

OrganizerWriter::Result OrganizerWriter::write(const QString &outPath)
{
#ifdef HAVE_QPDF
    if (writeVector(outPath) && verifyWritten(outPath)) {
        qInfo() << "[Organizer] saved" << m_pages.size() << "pages (vector) to"
                << outPath;
        return { true, Error::None, 0, int(m_pages.size()) };
    }
    qWarning() << "[Organizer] falling back to raster save for" << outPath;
#endif
#ifdef HAVE_PDF_RENDERING
    constexpr int   SAVE_DPI = 150;
    constexpr qreal scale    = SAVE_DPI / 72.0;

    auto outputPageSizePt = [&](const OrganizerPage &e) -> QSizeF {
        if (e.isBlank || !m_docs.contains(e.pdfPath))
            return QSizeF(595.0, 842.0);
        QSizeF pt = m_docs[e.pdfPath]->pageSizePts(e.pageIndex);
        if (e.rotation == 90 || e.rotation == 270)
            pt.transpose();
        return pt;
    };

    const QString staging = SafeWrite::stagingPath(outPath);
    if (staging.isEmpty())
        return { false, Error::WriteFailed, 0, int(m_pages.size()) };

    QPdfWriter writer(staging);
    writer.setCreator(QStringLiteral("OpenPDF Studio"));
    writer.setResolution(SAVE_DPI);
    if (!m_pages.isEmpty())
        writer.setPageSize(QPageSize(outputPageSizePt(m_pages[0]),
                                     QPageSize::Point, {}, QPageSize::ExactMatch));

    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter(&writer);
    if (!painter.isActive()) {
        SafeWrite::discard(staging);
        return { false, Error::WriteFailed, 0, int(m_pages.size()) };
    }

    int lostPages = 0;

    for (int i = 0; i < m_pages.size(); ++i) {
        const OrganizerPage &e = m_pages[i];

        if (i > 0) {

            writer.setPageSize(QPageSize(outputPageSizePt(e),
                                         QPageSize::Point, {}, QPageSize::ExactMatch));
            writer.newPage();
        }

        if (e.isBlank)
            continue;

        if (!m_docs.contains(e.pdfPath)) {
            qWarning() << "[Organizer] page" << (i + 1) << "has no open source:"
                       << e.pdfPath;
            ++lostPages;
            continue;
        }

        QImage img = m_docs[e.pdfPath]->render(e.pageIndex, scale);
        if (img.isNull()) {
            qWarning() << "[Organizer] page" << (i + 1) << "of" << e.pdfPath
                       << "(index" << e.pageIndex << ") did not render";
            ++lostPages;
            continue;
        }

        if (e.rotation != 0) {
            QTransform t;
            t.rotate(e.rotation);
            img = img.transformed(t, Qt::SmoothTransformation);
        }

        painter.drawImage(QRect(0, 0, painter.device()->width(),
                                painter.device()->height()), img);
    }
    painter.end();

    if (lostPages > 0) {
        SafeWrite::discard(staging);
        return { false, Error::RenderFailures, lostPages, int(m_pages.size()) };
    }
    if (!SafeWrite::commit(staging, outPath) || !verifyWritten(outPath)) {
        return { false, Error::WriteFailed, 0, int(m_pages.size()) };
    }
    qInfo() << "[Organizer] saved" << m_pages.size() << "pages (raster) to" << outPath;
    return { true, Error::None, 0, int(m_pages.size()) };
#else
    Q_UNUSED(outPath)
    return { false, Error::NoBackend, 0, 0 };
#endif
}
