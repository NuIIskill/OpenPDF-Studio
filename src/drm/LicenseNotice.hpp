#pragma once

#include <QCoreApplication>

#include <functional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class LicenseNotice
{
    Q_DECLARE_TR_FUNCTIONS(LicenseNotice)

public:

    static void askUsageIfUnknown(QWidget *parent);

    static void showExpiryReminderIfDue(QWidget *parent,
                                        std::function<void()> onEnterKey);
};
