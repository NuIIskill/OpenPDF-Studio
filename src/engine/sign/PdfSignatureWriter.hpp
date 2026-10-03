#pragma once

#include "engine/sign/CertificateProvider.hpp"

#include <QImage>
#include <QRectF>

/// Appends a signature field to a PDF as an incremental update and fills in the signature once it exists.
class PdfSignatureWriter
{
public:
    struct Field {
        int       page { 0 };
        QRectF    bounds;
        QImage    appearance;
        QString   name;
        QString   reason;
        QString   location;
        QDateTime time;
    };

    SignError prepare(const QString &source, const Field &field, int contentsBytes);
    QList<QByteArrayView> signedRanges() const;
    bool embed(const QByteArray &cms);
    const QByteArray &data() const { return m_data; }

private:
    QByteArray m_data;
    qsizetype  m_contentsAt  { -1 };
    qsizetype  m_contentsLen { 0 };
};
