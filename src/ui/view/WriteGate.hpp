#pragma once

#include "engine/document/WriteGrant.hpp"
#include "engine/historymanager/DocumentHistory.hpp"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <string>

/// Stops a write the history manager does not account for, or one that would leave an unsound file.
class WriteGate
{
    Q_DECLARE_TR_FUNCTIONS(WriteGate)

public:
    bool recorded(const DocumentHistory *history, const DocumentHistory::DocumentState &now);
    bool conform(const WriteGrant &staging, const std::string &password);
    bool sound(const QString &staging, const std::string &password, const QString &source,
               const std::string &sourcePassword);

    QString refusal() const { return m_refusal; }
    void    clear() { m_refusal.clear(); }

private:
    void refuse(const QString &reason, const QString &detail);

    QString m_refusal;
    QString m_sourceKey;
    QStringList m_sourceProblems;
};
