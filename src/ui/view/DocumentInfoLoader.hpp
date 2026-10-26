#pragma once

#include "engine/document/DocumentInfo.hpp"

#include <QObject>

#include <string>

/// Reads the DocumentInfo of a view's file on a background thread and keeps the latest result.
class DocumentInfoLoader : public QObject
{
    Q_OBJECT

public:
    explicit DocumentInfoLoader(QObject *parent = nullptr);

    void load(const QString &path, const std::string &password);
    void reverifySignatures();
    void clear();

    const DocumentInfo &info() const { return m_info; }
    bool                loaded() const { return m_loaded; }

Q_SIGNALS:
    void changed();

private:
    DocumentInfo m_info;
    QString      m_path;
    std::string  m_password;
    bool         m_loaded     { false };
    int          m_generation { 0 };
};
