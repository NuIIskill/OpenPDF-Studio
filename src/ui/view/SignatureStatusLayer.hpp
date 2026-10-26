#pragma once

#include "engine/sign/SignatureVerifier.hpp"
#include "ui/view/PageOverlay.hpp"

#include <QList>
#include <QObject>
#include <QPointer>

class DocumentInfoLoader;
class PageCanvas;
class SignatureSpot;
class SignatureStatusCard;

/// A badge on each signed field that opens its verification result.
class SignatureStatusLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    SignatureStatusLayer(PageCanvas *canvas, DocumentInfoLoader *info, QObject *parent = nullptr);
    ~SignatureStatusLayer() override;

    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override;
    void relayout() override;

private:
    void rebuild();
    void clear();
    void toggleCard(SignatureSpot *spot);
    void openCard(SignatureSpot *spot);
    void placeCard();

    PageCanvas                    *m_canvas { nullptr };
    DocumentInfoLoader            *m_info { nullptr };
    QList<SignatureSpot *>         m_spots;
    QPointer<SignatureStatusCard>  m_card;
    bool                           m_interactive { true };
};
