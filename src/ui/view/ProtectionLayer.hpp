#pragma once

#include "engine/document/ProtectionWriter.hpp"
#include "ui/view/PageOverlay.hpp"

#include <QHash>
#include <QObject>

#include <optional>

/// A protection change that waits for the next save; its passwords stay in memory only.
class ProtectionLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    explicit ProtectionLayer(QObject *parent = nullptr);

    void set(const PdfProtection &protection);
    const std::optional<PdfProtection> &pending() const { return m_pending; }

    void unlock(const QString &ownerPassword) { m_ownerPassword = ownerPassword; }
    const QString &ownerPassword() const { return m_ownerPassword; }

    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override { Q_UNUSED(toolId) }
    void relayout() override {}
    QString stateKey() const override;
    QByteArray state() const override;
    void restoreState(const QByteArray &state) override;

private:
    struct Secret {
        QString user;
        QString owner;
    };

    std::optional<PdfProtection> m_pending;
    QString                      m_token;
    QHash<QString, Secret>       m_secrets;
    QString                      m_ownerPassword;
};
