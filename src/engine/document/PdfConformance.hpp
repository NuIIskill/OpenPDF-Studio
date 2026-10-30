#pragma once

#include "engine/document/WriteGrant.hpp"

#include <QString>
#include <QStringList>

#include <string>

/// Whether a written PDF is sound: every object readable and a header version its features are allowed in.
namespace PdfConformance {

struct Version {
    int major { 1 };
    int minor { 0 };
    int extension { 0 };

    QString text() const;
    bool operator<(const Version &other) const;
};

bool available();

Version needed(const QString &path, const std::string &password, QStringList *why = nullptr);

bool raiseVersion(const WriteGrant &file, const std::string &password);

QStringList problems(const QString &path, const std::string &password);

}
