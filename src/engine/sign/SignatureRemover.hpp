#pragma once

#include "engine/document/WriteGrant.hpp"
#include <QString>

#include <string>

/// Takes a signature off a PDF by going back to the revision written before it, with everything added after it, or clears it when there is none.
namespace SignatureRemover {

struct Removal {
    bool possible     { false };
    int  signatures   { 0 };
    bool laterChanges { false };
    bool inPlace      { false };
};

Removal plan(const QString &source, const QString &field, const std::string &password = {});
bool    remove(const QString &source, const WriteGrant &output, const QString &field,
               const std::string &password = {});

}
