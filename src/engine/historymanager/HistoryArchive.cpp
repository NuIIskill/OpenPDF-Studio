#include "engine/historymanager/HistoryArchive.hpp"

#include "app/SessionStore.hpp"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

namespace HistoryArchive {

namespace {

constexpr quint32 kMagic   = 0x4f504853;
constexpr quint16 kVersion = 1;

void writeBookmarks(QDataStream &out, const QList<PdfBookmark> &bookmarks)
{
    out << static_cast<qint32>(bookmarks.size());
    for (const PdfBookmark &b : bookmarks) {
        out << b.title << static_cast<qint32>(b.page) << b.expanded << b.supported;
        writeBookmarks(out, b.children);
    }
}

bool readBookmarks(QDataStream &in, QList<PdfBookmark> *bookmarks, int depth = 0)
{
    qint32 count = 0;
    in >> count;
    if (count < 0 || depth > 64) return false;
    for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        PdfBookmark b;
        qint32 page = -1;
        in >> b.title >> page >> b.expanded >> b.supported;
        b.page = page;
        if (!readBookmarks(in, &b.children, depth + 1)) return false;
        bookmarks->append(std::move(b));
    }
    return in.status() == QDataStream::Ok;
}

void writeEntry(QDataStream &out, const DocumentHistory::Entry &e)
{
    out << e.time << static_cast<qint32>(e.kind) << static_cast<qint32>(e.page)
        << static_cast<qint32>(e.count) << static_cast<qint32>(e.value) << e.text
        << static_cast<qint32>(e.undoIndex) << e.snapshot << static_cast<qint32>(e.base);

    out << static_cast<qint32>(e.state.images.size());
    for (const DocumentHistory::ImageState &image : e.state.images)
        out << static_cast<qint32>(image.page) << image.pdfBounds << image.image;
    writeBookmarks(out, e.state.bookmarks);
    out << e.state.overlays;
}

bool readEntry(QDataStream &in, DocumentHistory::Entry *e)
{
    qint32 kind = 0, page = 0, count = 0, value = 0, undoIndex = 0, base = 0;
    in >> e->time >> kind >> page >> count >> value >> e->text
       >> undoIndex >> e->snapshot >> base;
    e->kind      = static_cast<DocumentHistory::Kind>(kind);
    e->page      = page;
    e->count     = count;
    e->value     = value;
    e->undoIndex = undoIndex;
    e->base      = base;

    qint32 images = 0;
    in >> images;
    if (images < 0) return false;
    for (qint32 i = 0; i < images && in.status() == QDataStream::Ok; ++i) {
        DocumentHistory::ImageState image;
        qint32 imagePage = -1;
        in >> imagePage >> image.pdfBounds >> image.image;
        image.page = imagePage;
        e->state.images.append(std::move(image));
    }
    if (!readBookmarks(in, &e->state.bookmarks)) return false;
    in >> e->state.overlays;
    return in.status() == QDataStream::Ok;
}

}

bool write(const QList<DocumentHistory::Entry> &entries, int current, const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << kMagic << kVersion << static_cast<qint32>(current)
        << static_cast<qint32>(entries.size());
    for (const DocumentHistory::Entry &e : entries) writeEntry(out, e);

    if (out.status() != QDataStream::Ok) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool read(const QString &path, Timeline *timeline)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) return false;

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    qint32 current = -1, count = 0;
    in >> magic >> version >> current >> count;
    if (magic != kMagic || version != kVersion || count < 0) return false;

    Timeline read;
    read.current = current;
    for (qint32 i = 0; i < count; ++i) {
        DocumentHistory::Entry e;
        if (!readEntry(in, &e)) return false;
        read.entries.append(std::move(e));
    }
    if (read.entries.isEmpty()) return false;
    *timeline = std::move(read);
    return true;
}

void discard(const QString &path)
{
    Timeline timeline;
    if (read(path, &timeline))
        for (const DocumentHistory::Entry &e : std::as_const(timeline.entries))
            SessionStore::discardSnapshot(e.snapshot);
    SessionStore::discardSnapshot(path);
}

}
