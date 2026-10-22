#pragma once

#include "engine/document/DocumentInfo.hpp"

#include <QColor>
#include <QString>

/// The short wording and colours the status bar and the info center share for a DocumentInfo.
namespace InfoText {

QString signatureSummary(const DocumentInfo &info);
QString signatureIcon(const DocumentInfo &info);
QColor  signatureColor(const DocumentInfo &info);
QString encryptionSummary(const DocumentInfo &info);
QColor  warningColor();
QColor  errorColor();

}
