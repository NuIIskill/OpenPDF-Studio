#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QString>

/// Saved looks for digital signatures, as JSON files beside config.ini with the logo as PNG.
namespace SignatureProfileStore {

inline const QString kStandardId = QStringLiteral("standard");

struct Profile {
    QString   id;
    QString   name;
    QString   signer;
    QString   reason;
    QString   location;
    QImage    logo;
    QString   logoName;
    bool      showSigner   { true };
    bool      showDate     { true };
    bool      showReason   { true };
    bool      showLocation { true };
    bool      showLogo     { false };
    QDateTime created;
};

QList<Profile> list();
QString        save(Profile profile);
bool           remove(const QString &id);
QString        selected();
void           setSelected(const QString &id);

}
