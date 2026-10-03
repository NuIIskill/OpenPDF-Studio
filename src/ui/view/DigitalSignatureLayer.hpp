#pragma once

#include "engine/sign/SignatureManager.hpp"
#include "ui/sign/SignatureAppearance.hpp"
#include "ui/view/PageOverlay.hpp"

#include <QObject>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class PageCanvas;

/// Digital signatures placed but not yet written; they are signed when the document is saved.
class DigitalSignatureLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    explicit DigitalSignatureLayer(PageCanvas *canvas, QObject *parent = nullptr);
    ~DigitalSignatureLayer() override;

    void add(const SignRequest &request, const SignatureAppearance &appearance);
    bool hasPending() const { return !m_pending.isEmpty(); }
    SignError signInto(const QString &stagingPath);
    SignError lastError() const { return m_lastError; }
    void clearError() { m_lastError = SignError::None; }

    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override;
    void relayout() override;
    QString stateKey() const override;
    QByteArray state() const override;
    void restoreState(const QByteArray &state) override;

private:
    struct Pending {
        SignRequest         request;
        SignatureAppearance appearance;
        QLabel             *preview { nullptr };
    };

    void addPreview(Pending &pending);
    void clear();

    PageCanvas        *m_canvas { nullptr };
    QList<Pending>     m_pending;
    SignError          m_lastError { SignError::None };
};
