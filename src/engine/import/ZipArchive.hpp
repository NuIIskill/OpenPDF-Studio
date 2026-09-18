#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

/// Read-only ZIP container, enough to get at the parts of a docx or odt file.
class ZipArchive
{
public:
    static bool available();

    bool open(const QString &path, QString *error = nullptr);
    bool open(const QByteArray &data, QString *error = nullptr);

    bool contains(const QString &name) const { return m_entries.contains(name); }
    QStringList names() const { return m_entries.keys(); }
    bool isEmpty() const { return m_entries.isEmpty(); }

    QByteArray read(const QString &name, QString *error = nullptr) const;

private:
    struct Entry {
        qint64  offset       { 0 };
        qint64  compressed   { 0 };
        qint64  uncompressed { 0 };
        quint16 method       { 0 };
        quint16 flags        { 0 };
    };

    bool readCentralDirectory(QString *error);

    QByteArray            m_data;
    QHash<QString, Entry> m_entries;
};
