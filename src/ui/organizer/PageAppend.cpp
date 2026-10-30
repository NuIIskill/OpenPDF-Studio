#include "ui/organizer/PageAppend.hpp"

#include "app/PdfPwStore.hpp"
#include "app/SessionStore.hpp"
#include "engine/document/OrganizerDoc.hpp"
#include "engine/document/OrganizerWriter.hpp"
#include "ui/DocumentView.hpp"
#include "ui/widgets/PasswordDialog.hpp"

#include <QFileInfo>
#include <QMap>

#include <memory>

namespace {

OrganizerDoc *load(const QString &path, QWidget *parent)
{
    bool needsPassword = false;
    OrganizerDoc *doc = OrganizerDoc::load(path, PdfPwStore::get(path), &needsPassword);
    for (int attempt = 0; !doc && needsPassword; ++attempt) {
        PasswordDialog prompt(QFileInfo(path).fileName(), attempt > 0, parent);
        if (prompt.exec() != QDialog::Accepted) return nullptr;
        doc = OrganizerDoc::load(path, prompt.password(), &needsPassword);
        if (doc) PdfPwStore::set(path, prompt.password());
    }
    return doc;
}

}

bool PageAppend::append(DocumentView *view, const QString &otherPdf)
{
    if (!view || view->pageCount() <= 0) return false;
    const QString content = view->contentFile();

    std::unique_ptr<OrganizerDoc> current(load(content, view));
    std::unique_ptr<OrganizerDoc> other(load(otherPdf, view));
    if (!current || !other) return false;

    QList<OrganizerPage> pages;
    for (int i = 0; i < current->pageCount(); ++i) pages.append({ content, i });
    for (int i = 0; i < other->pageCount(); ++i)   pages.append({ otherPdf, i });
    const QMap<QString, OrganizerDoc *> docs {
        { content, current.get() }, { otherPdf, other.get() } };

    const WriteGrant grant = view->newWorkingCopy();
    const QString work = grant.path();
    if (work.isEmpty() || !OrganizerWriter(pages, docs).write(grant).ok) {
        SessionStore::discard(work);
        return false;
    }

    const int first = current->pageCount();
    current.reset();
    other.reset();
    DocumentHistory::Change change { DocumentHistory::Kind::PageAdded, first };
    change.count = pages.size() - first;
    if (view->openWorkingCopy(work, view->currentFile(), change)) return true;
    SessionStore::discard(work);
    return false;
}
