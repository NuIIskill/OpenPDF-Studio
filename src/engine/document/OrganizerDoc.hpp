#pragma once

#include "engine/document/DocumentWorker.hpp"
#include "engine/document/PdfBackend.hpp"
#include "engine/render/PdfRenderer.hpp"

#include <QImage>
#include <QSizeF>
#include <QString>

#include <memory>

class OrganizerDoc
{
public:

    static OrganizerDoc *load(const QString &path, const QString &password = {},
                              bool *needsPassword = nullptr)
    {
        if (needsPassword) *needsPassword = false;
        auto backend = PdfBackend::create();
        if (!backend) return nullptr;

        bool asked = false;
        bool offered = false;
        auto ask = [&](const QString &, bool) -> std::optional<QString> {
            asked = true;
            if (offered || password.isEmpty()) return std::nullopt;
            offered = true;
            return password;
        };

        if (!backend->open(path, ask)) {
            if (needsPassword) *needsPassword = asked;
            return nullptr;
        }
        return new OrganizerDoc(std::move(backend), path);
    }

    ~OrganizerDoc()
    {

        m_worker.reset();
        m_renderer.reset();
        m_backend.reset();
    }

    int    pageCount() const { return m_backend->pageCount(); }
    QSizeF pageSizePts(int page) const { return m_renderer->pageSizePts(page); }

    QImage render(int page, qreal scale) const { return m_renderer->renderPage(page, scale); }

    DocumentWorker *worker()
    {
        if (!m_worker) {
            m_worker = std::make_unique<DocumentWorker>(m_backend.get());
            m_worker->setDocument(m_path);
        }
        return m_worker.get();
    }

private:
    OrganizerDoc(std::unique_ptr<PdfBackend> backend, const QString &path)
        : m_backend(std::move(backend))
        , m_renderer(std::make_unique<PdfRenderer>(m_backend.get()))
        , m_path(path) {}

    std::unique_ptr<PdfBackend>     m_backend;
    std::unique_ptr<PdfRenderer>    m_renderer;
    std::unique_ptr<DocumentWorker> m_worker;
    QString                         m_path;
};
