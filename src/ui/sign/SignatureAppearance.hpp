#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QImage>
#include <QString>

QT_BEGIN_NAMESPACE
class QDataStream;
QT_END_NAMESPACE

/// What a visible digital signature shows, and the stamp drawn from it.
struct SignatureAppearance
{
    Q_DECLARE_TR_FUNCTIONS(SignatureAppearance)

public:
    QString name;
    QString reason;
    QString location;
    QImage  logo;
    QString logoName;
    bool    showName     { true };
    bool    showDate     { true };
    bool    showReason   { true };
    bool    showLocation { true };
    bool    showLogo     { false };

    QImage render(const QDateTime &time) const;
};

QDataStream &operator<<(QDataStream &out, const SignatureAppearance &appearance);
QDataStream &operator>>(QDataStream &in, SignatureAppearance &appearance);
