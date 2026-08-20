#pragma once

#include <QCoreApplication>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

// Die beiden Momente, in denen die Lizenz von sich aus sichtbar wird. Beide
// sind wegklickbar und ändern nichts an dem, was das Programm kann.
// Eine Klasse statt eines Namensraums, weil Q_DECLARE_TR_FUNCTIONS nur dort
// funktioniert — die Texte brauchen einen eigenen Übersetzungskontext.
class LicenseNotice
{
    Q_DECLARE_TR_FUNCTIONS(LicenseNotice)

public:
    // Einmal beim ersten Start, wenn weder Installer noch Benutzer die Frage
    // schon beantwortet haben. Ohne Antwort wird nichts gespeichert — dann
    // kommt sie beim nächsten Mal wieder.
    static void askUsageIfUnknown(QWidget *parent);

    // Höchstens einmal pro Tag, wenn die 30 Tage einer geschäftlichen
    // Installation ohne Schlüssel um sind. onEnterKey ruft die Lizenzseite auf.
    static void showExpiryReminderIfDue(QWidget *parent,
                                        std::function<void()> onEnterKey);
};
