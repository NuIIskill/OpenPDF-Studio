#pragma once

#include <QString>

#include <string>

/// Takes a signature off a PDF by going back to the revision written before it, with everything added after it.
namespace SignatureRemover {

struct Removal {
    bool possible     { false };
    int  signatures   { 0 };
    bool laterChanges { false };
};

Removal plan(const QString &source, const QString &field, const std::string &password = {});
bool    remove(const QString &source, const QString &output, const QString &field,
               const std::string &password = {});

}
