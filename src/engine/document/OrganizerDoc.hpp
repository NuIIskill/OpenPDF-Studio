#pragma once

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
        return new OrganizerDoc(std::move(backend));
    }

    ~OrganizerDoc()
    {

        m_renderer.reset();
        m_backend.reset();
    }

    int    pageCount() const { return m_backend->pageCount(); }
    QSizeF pageSizePts(int page) const { return m_renderer->pageSizePts(page); }

    QImage render(int page, qreal scale) const { return m_renderer->renderPage(page, scale); }

private:
    explicit OrganizerDoc(std::unique_ptr<PdfBackend> backend)
        : m_backend(std::move(backend))
        , m_renderer(std::make_unique<PdfRenderer>(m_backend.get())) {}

    std::unique_ptr<PdfBackend>  m_backend;
    std::unique_ptr<PdfRenderer> m_renderer;
};
