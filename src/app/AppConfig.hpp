#pragma once

#include <QString>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

// Die eine Konfigurationsdatei. Überall INI, auch auf Windows — die Registry
// lässt sich nicht mitkopieren, nicht sichern und nicht von Hand lesen.
//
// Gesucht wird in dieser Reihenfolge:
//   1. $OPENPDF_CONFIG                     (voller Pfad, für Tests und Sonderfälle)
//   2. <Ordner der Anwendung>/config.ini   (portabel — nur wenn vorhanden und
//                                           beschreibbar; in Program Files ist
//                                           sie es nicht und wird übergangen)
//   3. QStandardPaths::AppConfigLocation/config.ini
//
// Der portable Modus ist damit eine Datei, keine Einstellung: config.ini neben
// die .exe legen, fertig — genau das liegt im ZIP schon bei.
namespace AppConfig {

QSettings &store();

// Der tatsächlich benutzte Pfad — für Dokumentation und Fehlersuche.
QString path();

// True, wenn die Datei neben der Anwendung liegt.
bool isPortable();

} // namespace AppConfig
