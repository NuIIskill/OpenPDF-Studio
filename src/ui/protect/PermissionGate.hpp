#pragma once

#include <QCoreApplication>

#include <optional>

class DocumentView;

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/// Checks actions against the permissions of a protected document and lifts them with its permissions password.
class PermissionGate
{
    Q_DECLARE_TR_FUNCTIONS(PermissionGate)

public:
    enum class Action {
        Print, PrintHighQuality, Copy, Modify, Annotate, FillForms, Assemble, ChangeProtection
    };

    static bool allowed(const DocumentView *view, Action action);
    static bool request(DocumentView *view, Action action, QWidget *parent);
    static std::optional<Action> forTool(const QString &toolId);
};
