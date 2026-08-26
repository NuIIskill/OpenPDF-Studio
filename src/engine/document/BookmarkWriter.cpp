#include "engine/document/BookmarkWriter.hpp"

#include "app/SafeWrite.hpp"

#include <QDebug>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFObjectHandle.hh>
#  include <qpdf/QPDFPageDocumentHelper.hh>
#  include <qpdf/QPDFPageObjectHelper.hh>
#  include <qpdf/QPDFWriter.hh>
#endif

bool BookmarkWriter::available()
{
#ifdef HAVE_QPDF
    return true;
#else
    return false;
#endif
}

#ifdef HAVE_QPDF
namespace {

int descendantCount(const PdfBookmark &bookmark)
{
    int count = static_cast<int>(bookmark.children.size());
    for (const PdfBookmark &child : bookmark.children)
        count += descendantCount(child);
    return count;
}

QList<QPDFObjectHandle> makeLevel(QPDF &pdf,
                                  const QList<PdfBookmark> &bookmarks,
                                  const QPDFObjectHandle &parent,
                                  const std::vector<QPDFPageObjectHelper> &pages)
{
    QList<QPDFObjectHandle> items;
    items.reserve(bookmarks.size());

    for (const PdfBookmark &bookmark : bookmarks) {
        QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
        dict.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(
            bookmark.title.toUtf8().toStdString()));
        dict.replaceKey("/Parent", parent);

        if (bookmark.page >= 0
                && bookmark.page < static_cast<int>(pages.size())) {
            QPDFObjectHandle dest = QPDFObjectHandle::newArray();
            dest.appendItem(pages[static_cast<std::size_t>(bookmark.page)]
                                .getObjectHandle());
            dest.appendItem(QPDFObjectHandle::newName("/Fit"));
            dict.replaceKey("/Dest", dest);
        }

        items.append(pdf.makeIndirectObject(dict));
    }

    for (int i = 0; i < items.size(); ++i) {
        QPDFObjectHandle item = items[i];
        if (i > 0) item.replaceKey("/Prev", items[i - 1]);
        if (i + 1 < items.size()) item.replaceKey("/Next", items[i + 1]);

        const QList<QPDFObjectHandle> children =
            makeLevel(pdf, bookmarks[i].children, item, pages);
        if (children.isEmpty()) continue;

        item.replaceKey("/First", children.first());
        item.replaceKey("/Last", children.last());
        const int count = descendantCount(bookmarks[i]);
        item.replaceKey("/Count", QPDFObjectHandle::newInteger(
            bookmarks[i].expanded ? count : -count));
    }

    return items;
}

int totalCount(const QList<PdfBookmark> &bookmarks)
{
    int count = static_cast<int>(bookmarks.size());
    for (const PdfBookmark &bookmark : bookmarks)
        count += descendantCount(bookmark);
    return count;
}

} // namespace
#endif

bool BookmarkWriter::write(const QString &path,
                           const QList<PdfBookmark> &bookmarks,
                           const QString &password)
{
#ifndef HAVE_QPDF
    Q_UNUSED(path)
    Q_UNUSED(bookmarks)
    Q_UNUSED(password)
    return false;
#else
    if (path.isEmpty()) return false;
    const QString staging = SafeWrite::stagingPath(path);
    if (staging.isEmpty()) return false;

    try {
        QPDF pdf;
        const QByteArray fileName = path.toLocal8Bit();
        const QByteArray passwordBytes = password.toUtf8();
        pdf.processFile(fileName.constData(),
                        passwordBytes.isEmpty() ? nullptr
                                                : passwordBytes.constData());

        QPDFObjectHandle catalog = pdf.getRoot();
        catalog.removeKey("/Outlines");
        catalog.removeKey("/PageMode");

        if (!bookmarks.isEmpty()) {
            QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
            root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
            root = pdf.makeIndirectObject(root);

            const std::vector<QPDFPageObjectHelper> pages =
                QPDFPageDocumentHelper(pdf).getAllPages();
            const QList<QPDFObjectHandle> top =
                makeLevel(pdf, bookmarks, root, pages);
            if (!top.isEmpty()) {
                root.replaceKey("/First", top.first());
                root.replaceKey("/Last", top.last());
                root.replaceKey("/Count",
                                QPDFObjectHandle::newInteger(totalCount(bookmarks)));
                catalog.replaceKey("/Outlines", root);
                catalog.replaceKey("/PageMode",
                                   QPDFObjectHandle::newName("/UseOutlines"));
            }
        }

        QPDFWriter writer(pdf, staging.toLocal8Bit().constData());
        writer.setPreserveEncryption(true);
        writer.setCompressStreams(true);
        writer.write();
    } catch (const std::exception &ex) {
        qWarning() << "BookmarkWriter:" << ex.what();
        SafeWrite::discard(staging);
        return false;
    }

    return SafeWrite::commit(staging, path);
#endif
}
