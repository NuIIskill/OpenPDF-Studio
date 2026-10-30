#pragma once

#include "engine/document/WriteGrant.hpp"
#include "engine/document/DocumentInfo.hpp"

#include <QString>

#include <string>

/// The protection a document is saved with: passwords, cipher and what readers may do.
struct PdfProtection
{
    enum class Cipher { Aes256, Aes128, Rc4 };

    bool        encrypt { false };
    QString     userPassword;
    QString     ownerPassword;
    Cipher      cipher { Cipher::Aes256 };
    bool        encryptMetadata { true };
    DocumentInfo::Permissions permissions;
};

/// Rewrites a PDF with new encryption, or without any.
namespace ProtectionWriter {

bool available();
bool apply(const WriteGrant &file, const std::string &currentPassword,
           const PdfProtection &protection);

}
