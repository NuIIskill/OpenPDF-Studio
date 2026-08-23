#pragma once

#include "engine/document/PdfBackend.hpp"
#include "engine/render/PdfRenderer.hpp"

#include <QImage>
#include <QSizeF>
#include <QString>

#include <memory>

// Ein zweites, unabhängig geöffnetes Dokument. Der Organizer stellt Seiten aus
// mehreren PDFs zusammen, liest also aus anderen Dateien als die Ansicht und
// braucht ein eigenes Backend statt des einen, das DocumentSource hält. Mehr
// als Seitenzahl, Seitengröße und einen Rasterisierer braucht er nicht.
class OrganizerDoc
{
public:
    // needsPassword unterscheidet "verschlüsselt" von "kaputt", damit der
    // Aufrufer im ersten Fall fragen kann, ohne im zweiten zu nerven.
    static OrganizerDoc *load(const QString &path, const QString &password = {},
                              bool *needsPassword = nullptr)
    {
        if (needsPassword) *needsPassword = false;
        auto backend = PdfBackend::create();
        if (!backend) return nullptr;

        // Nach einem Passwort gefragt wird nur, wenn die Datei verschlüsselt
        // ist — dass der Rückruf lief, ist also die Antwort auf needsPassword.
        // Angeboten wird das übergebene Passwort genau einmal; danach gibt der
        // Rückruf auf, statt in einer Schleife dasselbe zu wiederholen.
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
        // Der Renderer leiht sich das Backend nur — er geht zuerst.
        m_renderer.reset();
        m_backend.reset();
    }

    int    pageCount() const { return m_backend->pageCount(); }
    QSizeF pageSizePts(int page) const { return m_renderer->pageSizePts(page); }
    // scale = Ausgabepixel pro PDF-Punkt.
    QImage render(int page, qreal scale) const { return m_renderer->renderPage(page, scale); }

private:
    explicit OrganizerDoc(std::unique_ptr<PdfBackend> backend)
        : m_backend(std::move(backend))
        , m_renderer(std::make_unique<PdfRenderer>(m_backend.get())) {}

    std::unique_ptr<PdfBackend>  m_backend;
    std::unique_ptr<PdfRenderer> m_renderer;
};
