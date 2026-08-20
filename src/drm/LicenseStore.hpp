#pragma once

#include <QDate>
#include <QString>

// Business-Lizenz-Platzhalter für die source-available Module
// (modules/rich-media/). Bewusst simpel: hier wird nichts geprüft und nichts
// gesperrt — die Stelle beantwortet nur drei Fragen.
//
//   1. Ist das eine geschäftliche Installation?  (Setup schreibt "Usage")
//   2. Wie viele der 30 Evaluierungstage sind noch übrig?
//   3. Liegt ein Schlüssel vor?
//
// Ein eingegebener Schlüssel wird ungeprüft übernommen. Die echte Prüfung
// (signierter Offline-Key) kommt zusammen mit dem Rich-Media-Modul; bis dahin
// ist das hier der Rahmen, nicht der Inhalt.
//
// Speicherorte (Maschine vor Benutzer, siehe modules/rich-media/README.md):
//   Maschine  Windows: HKLM\Software\OpenPDFStudio
//                      Werte "Usage" und "BusinessLicense\Key"
//             sonst:   /etc/openpdf-studio/license.conf, gleiche Schlüsselnamen
//   Benutzer  QSettings("OpenPDF", "OpenPDFStudio"), Gruppe "license"
//
// Woher die Nutzungsart kommt, in dieser Reihenfolge:
//   1. OPENPDF_USAGE=business|personal          (nur zum Testen)
//   2. die eigene Angabe des Benutzers          (Einstellungen → Erweitert)
//   3. was der Installer hinterlassen hat       (Windows-Setup)
//   4. nichts davon → die Anwendung fragt beim ersten Start einmal nach
namespace License {

inline constexpr int kEvaluationDays = 30;

// "personal" | "business" | "" (noch nie beantwortet)
QString usage();
bool    isBusinessInstall();

// Die eigene Angabe des Benutzers. Sie gilt vor der des Installers, sonst
// liesse sich eine einmal getroffene Wahl nicht mehr korrigieren.
void setUsage(const QString &usage);

// Erster Start dieser geschäftlichen Installation. Wird beim ersten Aufruf
// gesetzt; für private Installationen bleibt das Datum ungültig.
QDate evaluationStart();
int   evaluationDaysLeft();   // 0 … kEvaluationDays
bool  isEvaluationOver();     // geschäftlich, ohne Schlüssel, Frist vorbei

bool    hasKey();
QString key();                        // "" wenn keiner hinterlegt ist
bool    keyIsMachineWide();           // vom Setup gesetzt — nicht entfernbar
void    setKey(const QString &key);   // Platzhalter: wird ungeprüft gespeichert
void    clearKey();

} // namespace License
