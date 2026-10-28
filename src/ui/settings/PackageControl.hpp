#pragma once

#include "app/SystemPackages.hpp"

#include <QObject>

#include <functional>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QWidget;
QT_END_NAMESPACE

/// The install or remove button for a set of system packages and the line saying what it is doing.
class PackageControl : public QObject
{
    Q_OBJECT

public:
    explicit PackageControl(QWidget *parent);

    QPushButton *button() const { return m_button; }
    QLabel *status() const { return m_status; }

    void setTarget(const SystemPackages::Names &names, std::function<bool()> installed);

    void refresh();

Q_SIGNALS:
    void changed();

private:
    void act();
    void finish(bool ok, const QString &error);

    QPushButton           *m_button   { nullptr };
    QLabel                *m_status   { nullptr };
    SystemPackages        *m_packages { nullptr };
    QStringList            m_names;
    std::function<bool()>  m_installed;
    bool                   m_present  { false };
};
