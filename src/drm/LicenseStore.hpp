#pragma once

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
bool    keyIsMachineWide();
void    setKey(const QString &key);
void    clearKey();

}
