#include "ui/protect/PermissionGate.hpp"

#include "app/AppSettings.hpp"
#include "ui/DocumentView.hpp"
#include "ui/panels/LeftSidebar.hpp"

#include <QInputDialog>
#include <QLineEdit>

namespace {

QString refusal(PermissionGate::Action action)
{
    using A = PermissionGate::Action;
    switch (action) {
    case A::Print:
    case A::PrintHighQuality:
        return PermissionGate::tr("This document does not allow printing.");
    case A::Copy:      return PermissionGate::tr("This document does not allow copying text.");
    case A::Modify:    return PermissionGate::tr("This document does not allow changing its content.");
    case A::Annotate:  return PermissionGate::tr("This document does not allow adding comments.");
    case A::FillForms: return PermissionGate::tr("This document does not allow filling in forms.");
    case A::Assemble:
        return PermissionGate::tr("This document does not allow inserting, deleting and rotating pages.");
    case A::ChangeProtection:
        return PermissionGate::tr("The restrictions of this document can only be changed by its owner.");
    }
    return {};
}

}

bool PermissionGate::allowed(const DocumentView *view, Action action)
{
    if (!view || !AppSettings().respectPermissions()) return true;
    const DocumentInfo::Permissions p = view->permissions();
    switch (action) {
    case Action::Print:     return p.print;
    case Action::PrintHighQuality: return p.printHighRes;
    case Action::Copy:      return p.copy;
    case Action::Modify:    return p.modify;
    case Action::Annotate:  return p.annotate;
    case Action::FillForms: return p.fillForms;
    case Action::Assemble:  return p.assemble;
    case Action::ChangeProtection:
        return p.print && p.printHighRes && p.modify && p.copy && p.annotate && p.fillForms
            && p.assemble && p.accessibility;
    }
    return true;
}

bool PermissionGate::request(DocumentView *view, Action action, QWidget *parent)
{
    if (allowed(view, action)) return true;
    QString message = refusal(action) + QLatin1Char('\n')
                    + tr("Enter the permissions password to lift its restrictions:");
    for (;;) {
        bool ok = false;
        const QString password = QInputDialog::getText(parent, tr("Restricted document"), message,
                                                       QLineEdit::Password, {}, &ok);
        if (!ok) return false;
        if (view->unlockPermissions(password)) return true;
        message = tr("Wrong permissions password. Please try again:");
    }
}

std::optional<PermissionGate::Action> PermissionGate::forTool(const QString &toolId)
{
    for (const ToolDef &tool : LeftSidebar::toolCatalog()) {
        if (tool.id != toolId) continue;
        if (!tool.needsEditMode) return std::nullopt;
        if (toolId == QLatin1String("text") || toolId == QLatin1String("image"))
            return Action::Modify;
        return Action::Annotate;
    }
    return std::nullopt;
}
