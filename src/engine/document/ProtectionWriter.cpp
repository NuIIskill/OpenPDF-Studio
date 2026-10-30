#include "engine/document/ProtectionWriter.hpp"

#include <QDebug>
#include <QFile>
#include <QUuid>

#ifdef HAVE_QPDF
#  include <qpdf/QPDF.hh>
#  include <qpdf/QPDFWriter.hh>
#endif

namespace ProtectionWriter {

bool available()
{
#ifdef HAVE_QPDF
    return true;
#else
    return false;
#endif
}

bool apply(const WriteGrant &fileGrant, const std::string &currentPassword,
           const PdfProtection &protection)
{
    const QString file = fileGrant.path();
#ifdef HAVE_QPDF
    const QString temp = file + QStringLiteral(".protect");
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(file.toLocal8Bit().constData(),
                        currentPassword.empty() ? nullptr : currentPassword.c_str());
        QPDFWriter writer(pdf, temp.toLocal8Bit().constData());
        writer.setCompressStreams(true);
        writer.setPreserveEncryption(false);
        if (protection.encrypt) {
            const DocumentInfo::Permissions &p = protection.permissions;
            const std::string user = protection.userPassword.toStdString();
            // Without its own owner password a restricted file could be unlocked
            // with the user password, so it gets one nobody knows.
            const std::string owner = !protection.ownerPassword.isEmpty()
                ? protection.ownerPassword.toStdString()
                : QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            const qpdf_r3_print_e print = !p.print       ? qpdf_r3p_none
                                        : p.printHighRes ? qpdf_r3p_full
                                                         : qpdf_r3p_low;
            switch (protection.cipher) {
            case PdfProtection::Cipher::Aes256:
                writer.setR6EncryptionParameters(user.c_str(), owner.c_str(), p.accessibility,
                                                 p.copy, p.assemble, p.annotate, p.fillForms,
                                                 p.modify, print, protection.encryptMetadata);
                break;
            case PdfProtection::Cipher::Aes128:
                writer.setR4EncryptionParametersInsecure(
                    user.c_str(), owner.c_str(), p.accessibility, p.copy, p.assemble,
                    p.annotate, p.fillForms, p.modify, print, protection.encryptMetadata, true);
                break;
            case PdfProtection::Cipher::Rc4:
                writer.setR3EncryptionParametersInsecure(
                    user.c_str(), owner.c_str(), p.accessibility, p.copy, p.assemble,
                    p.annotate, p.fillForms, p.modify, print);
                break;
            }
        }
        writer.write();
    } catch (const std::exception &ex) {
        qWarning() << "[ProtectionWriter] could not rewrite" << file << ex.what();
        QFile::remove(temp);
        return false;
    }
    if (!QFile::remove(file) || !QFile::rename(temp, file)) {
        qWarning() << "[ProtectionWriter] could not replace" << file;
        QFile::remove(temp);
        return false;
    }
    return true;
#else
    Q_UNUSED(file)
    Q_UNUSED(currentPassword)
    Q_UNUSED(protection)
    return false;
#endif
}

}
