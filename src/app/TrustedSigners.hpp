#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

/// Signers the user trusts on this computer, by the SHA-256 fingerprint of their certificate, stored as JSON beside config.ini.
class TrustedSigners : public QObject
{
    Q_OBJECT

public:
    struct Entry {
        QByteArray fingerprint;
        QString    name;
        QString    issuer;
        QDateTime  validTo;
        QDateTime  added;
    };

    static TrustedSigners *instance();

    static QList<Entry> list();
    static bool         contains(const QByteArray &fingerprint);
    static bool         add(Entry entry);
    static bool         remove(const QByteArray &fingerprint);

Q_SIGNALS:
    void changed();

private:
    using QObject::QObject;
};
