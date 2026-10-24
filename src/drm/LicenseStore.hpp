#pragma once

#include "drm/LicenseKey.hpp"

#include <QDate>
#include <QString>

namespace License {

inline constexpr int kEvaluationDays = 30;

QString usage();
bool    isBusinessInstall();

void setUsage(const QString &usage);

QDate evaluationStart();
int   evaluationDaysLeft();
bool  isEvaluationOver();

bool    hasKey();
QString key();
KeyInfo keyInfo();
bool    keyIsMachineWide();
void    setKey(const QString &key);
void    clearKey();

}
