#include "engine/sign/CertificateProvider.hpp"

#if defined(HAVE_DIGITAL_SIGNATURE) && defined(_WIN32)

#include <QCryptographicHash>
#include <QTimeZone>
#include <QUuid>

#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#include <vector>

namespace {

constexpr DWORD kEncoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
constexpr char kSigningCertificateV2[] = "1.2.840.113549.1.9.16.2.47";

QString certName(PCCERT_CONTEXT cert, DWORD flags)
{
    wchar_t buf[512];
    const DWORD len = CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, flags,
                                         nullptr, buf, DWORD(std::size(buf)));
    return len > 1 ? QString::fromWCharArray(buf, int(len - 1)) : QString();
}

QString distinguishedName(PCCERT_CONTEXT cert)
{
    wchar_t buf[1024];
    const DWORD len = CertNameToStrW(X509_ASN_ENCODING, &cert->pCertInfo->Subject,
                                     CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
                                     buf, DWORD(std::size(buf)));
    return len > 1 ? QString::fromWCharArray(buf, int(len - 1)) : QString();
}

QDateTime fromFileTime(const FILETIME &ft)
{
    const quint64 ticks = (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return QDateTime::fromMSecsSinceEpoch(qint64(ticks / 10000) - 11644473600000LL,
                                          QTimeZone::UTC);
}

QByteArray thumbprint(PCCERT_CONTEXT cert)
{
    BYTE hash[20];
    DWORD size = sizeof hash;
    if (!CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &size))
        return {};
    return QByteArray(reinterpret_cast<const char *>(hash), int(size));
}

bool hasPrivateKey(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    return CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size);
}

// SHA-256 needs the AES provider; keys imported from a .pfx usually sit in
// the older RSA_FULL one, which cannot hash with it.
HCRYPTPROV aesProvider(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size))
        return 0;
    std::vector<BYTE> buf(size);
    if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, buf.data(), &size))
        return 0;
    const auto *info = reinterpret_cast<const CRYPT_KEY_PROV_INFO *>(buf.data());
    if (info->dwProvType != PROV_RSA_FULL) return 0;
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContextW(&prov, info->pwszContainerName, nullptr, PROV_RSA_AES,
                              info->dwFlags & CRYPT_MACHINE_KEYSET))
        return 0;
    return prov;
}

// Wine reports keys imported from a .pfx with an invalid key spec.
DWORD capiKeySpec(HCRYPTPROV prov, DWORD reported)
{
    if (reported == AT_KEYEXCHANGE || reported == AT_SIGNATURE) return reported;
    HCRYPTKEY key = 0;
    if (CryptGetUserKey(prov, AT_KEYEXCHANGE, &key)) {
        CryptDestroyKey(key);
        return AT_KEYEXCHANGE;
    }
    return AT_SIGNATURE;
}

// Wine verifies only the signed attributes, not that their digest matches the content.
bool digestMatches(HCRYPTMSG msg, const QByteArray &content)
{
    DWORD size = 0;
    if (!CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size)) return false;
    std::vector<BYTE> buf(size);
    if (!CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &size)) return false;
    const auto *info = reinterpret_cast<const CMSG_SIGNER_INFO *>(buf.data());
    if (info->AuthAttrs.cAttr == 0) return true;

    const QByteArray oid(info->HashAlgorithm.pszObjId);
    QCryptographicHash::Algorithm algorithm;
    if      (oid == szOID_NIST_sha256)  algorithm = QCryptographicHash::Sha256;
    else if (oid == szOID_NIST_sha384)  algorithm = QCryptographicHash::Sha384;
    else if (oid == szOID_NIST_sha512)  algorithm = QCryptographicHash::Sha512;
    else if (oid == szOID_OIWSEC_sha1)  algorithm = QCryptographicHash::Sha1;
    else return false;

    for (DWORD i = 0; i < info->AuthAttrs.cAttr; ++i) {
        const CRYPT_ATTRIBUTE &attr = info->AuthAttrs.rgAttr[i];
        if (qstrcmp(attr.pszObjId, szOID_PKCS_9_MESSAGE_DIGEST) != 0 || attr.cValue != 1) continue;
        CRYPT_DATA_BLOB *digest = nullptr;
        DWORD len = 0;
        if (!CryptDecodeObjectEx(kEncoding, X509_OCTET_STRING, attr.rgValue[0].pbData,
                                 attr.rgValue[0].cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr,
                                 &digest, &len))
            return false;
        const bool match = QByteArray(reinterpret_cast<const char *>(digest->pbData),
                                      int(digest->cbData))
                           == QCryptographicHash::hash(content, algorithm);
        LocalFree(digest);
        return match;
    }
    return false;
}

class StoreHandle
{
public:
    StoreHandle()
        : m_store(CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
                                L"MY")) {}
    ~StoreHandle() { if (m_store) CertCloseStore(m_store, 0); }
    HCERTSTORE get() const { return m_store; }

private:
    HCERTSTORE m_store;
};

// Chain certificates except the root, so a verifier finds the intermediates.
std::vector<PCCERT_CONTEXT> chainOf(PCCERT_CONTEXT cert, PCCERT_CHAIN_CONTEXT *chainOut)
{
    std::vector<PCCERT_CONTEXT> out { cert };
    CERT_CHAIN_PARA para {};
    para.cbSize = sizeof para;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (!CertGetCertificateChain(nullptr, cert, nullptr, nullptr, &para, 0, nullptr, &chain))
        return out;
    *chainOut = chain;
    if (chain->cChain > 0) {
        const PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
        for (DWORD i = 1; i + 1 < simple->cElement; ++i)
            out.push_back(simple->rgpElement[i]->pCertContext);
    }
    return out;
}

class WindowsCertificateProvider : public CertificateProvider
{
public:
    QList<Certificate> certificates() override;
    QByteArray sign(const QString &certificateId, const QList<QByteArrayView> &data,
                    SignError *error) override;
    CmsCheck check(const QByteArray &cms, const QList<QByteArrayView> &data) override;
    QString create(const NewCertificate &request, SignError *error) override;
    bool remove(const QString &certificateId) override;
};

const QString kContainerPrefix = QStringLiteral("OpenPDF Studio ");

// Only certificates this program created carry its container name, and only those may go.
QString ownContainer(PCCERT_CONTEXT cert, CRYPT_KEY_PROV_INFO *infoOut, std::vector<BYTE> *buffer)
{
    DWORD size = 0;
    if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size))
        return {};
    buffer->resize(size);
    if (!CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, buffer->data(), &size))
        return {};
    const auto *info = reinterpret_cast<const CRYPT_KEY_PROV_INFO *>(buffer->data());
    const QString container = info->pwszContainerName
        ? QString::fromWCharArray(info->pwszContainerName) : QString();
    if (!container.startsWith(kContainerPrefix)) return {};
    if (infoOut) *infoOut = *info;
    return container;
}

QList<Certificate> WindowsCertificateProvider::certificates()
{
    QList<Certificate> out;
    StoreHandle store;
    if (!store.get()) return out;

    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store.get(), cert))) {
        if (!hasPrivateKey(cert)) continue;
        Certificate entry;
        entry.id      = QString::fromLatin1(thumbprint(cert).toHex());
        entry.subject = certName(cert, 0);
        entry.distinguishedName = distinguishedName(cert);
        entry.issuer  = certName(cert, CERT_NAME_ISSUER_FLAG);
        entry.validTo = fromFileTime(cert->pCertInfo->NotAfter);
        std::vector<BYTE> buffer;
        entry.removable = !ownContainer(cert, nullptr, &buffer).isEmpty();
        if (!entry.id.isEmpty()) out << entry;
    }
    return out;
}

QByteArray WindowsCertificateProvider::sign(const QString &certificateId,
                                            const QList<QByteArrayView> &data, SignError *error)
{
    auto fail = [error](SignError e) { if (error) *error = e; return QByteArray(); };

    StoreHandle store;
    const QByteArray hash = QByteArray::fromHex(certificateId.toLatin1());
    CRYPT_HASH_BLOB blob { DWORD(hash.size()),
                           reinterpret_cast<BYTE *>(const_cast<char *>(hash.constData())) };
    PCCERT_CONTEXT cert = store.get()
        ? CertFindCertificateInStore(store.get(), kEncoding, 0, CERT_FIND_SHA1_HASH, &blob, nullptr)
        : nullptr;
    if (!cert) return fail(SignError::CertificateNotFound);

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0;
    DWORD keySpec = 0;
    BOOL freeKey = FALSE;
    if (!CryptAcquireCertificatePrivateKey(cert, CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG, nullptr,
                                           &key, &keySpec, &freeKey)) {
        CertFreeCertificateContext(cert);
        return fail(SignError::CertificateNotFound);
    }
    if (keySpec != CERT_NCRYPT_KEY_SPEC) {
        if (const HCRYPTPROV aes = aesProvider(cert)) {
            if (freeKey) CryptReleaseContext(key, 0);
            key = aes;
            freeKey = TRUE;
        }
        keySpec = capiKeySpec(key, keySpec);
    }

    const QByteArray ess = Cms::signingCertificateV2(
        QByteArray(reinterpret_cast<const char *>(cert->pbCertEncoded), int(cert->cbCertEncoded)));
    CRYPT_ATTR_BLOB essValue { DWORD(ess.size()),
                               reinterpret_cast<BYTE *>(const_cast<char *>(ess.constData())) };
    CRYPT_ATTRIBUTE essAttr { const_cast<LPSTR>(kSigningCertificateV2), 1, &essValue };

    CMSG_SIGNER_ENCODE_INFO signer {};
    signer.cbSize = sizeof signer;
    signer.pCertInfo = cert->pCertInfo;
    if (keySpec == CERT_NCRYPT_KEY_SPEC) signer.hNCryptKey = key;
    else                                 signer.hCryptProv = key;
    signer.dwKeySpec = keySpec;
    signer.HashAlgorithm.pszObjId = const_cast<LPSTR>(szOID_NIST_sha256);
    signer.cAuthAttr = 1;
    signer.rgAuthAttr = &essAttr;

    PCCERT_CHAIN_CONTEXT chain = nullptr;
    const std::vector<PCCERT_CONTEXT> certs = chainOf(cert, &chain);
    std::vector<CERT_BLOB> certBlobs;
    for (PCCERT_CONTEXT c : certs)
        certBlobs.push_back({ c->cbCertEncoded, c->pbCertEncoded });

    CMSG_SIGNED_ENCODE_INFO info {};
    info.cbSize = sizeof info;
    info.cSigners = 1;
    info.rgSigners = &signer;
    info.cCertEncoded = DWORD(certBlobs.size());
    info.rgCertEncoded = certBlobs.data();

    QByteArray cms;
    SignError result = SignError::SigningFailed;
    HCRYPTMSG msg = CryptMsgOpenToEncode(kEncoding, CMSG_DETACHED_FLAG, CMSG_SIGNED,
                                         &info, nullptr, nullptr);
    if (msg) {
        bool ok = true;
        for (int i = 0; i < data.size() && ok; ++i)
            ok = CryptMsgUpdate(msg, reinterpret_cast<const BYTE *>(data[i].data()),
                                DWORD(data[i].size()), i + 1 == data.size());
        DWORD size = 0;
        if (ok && CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, nullptr, &size)) {
            cms.resize(int(size));
            if (CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, cms.data(), &size)) {
                cms.resize(int(size));
                result = SignError::None;
            } else {
                cms.clear();
            }
        }
        if (result != SignError::None) {
            const DWORD err = GetLastError();
            if (err == DWORD(SCARD_W_WRONG_CHV))
                result = SignError::WrongPin;
            else if (err == DWORD(SCARD_W_CHV_BLOCKED))
                result = SignError::PinLocked;
            else if (err == DWORD(SCARD_W_CANCELLED_BY_USER) || err == DWORD(ERROR_CANCELLED))
                result = SignError::PinRequired;
        }
        CryptMsgClose(msg);
    }

    if (chain) CertFreeCertificateChain(chain);
    if (freeKey) {
        if (keySpec == CERT_NCRYPT_KEY_SPEC) NCryptFreeObject(key);
        else                                 CryptReleaseContext(key, 0);
    }
    CertFreeCertificateContext(cert);
    if (error) *error = result;
    return cms;
}

CmsCheck WindowsCertificateProvider::check(const QByteArray &cms, const QList<QByteArrayView> &data)
{
    CmsCheck result;
    const QByteArray der = Cms::trimmed(cms);

    HCRYPTMSG msg = CryptMsgOpenToDecode(kEncoding, CMSG_DETACHED_FLAG, 0, 0, nullptr, nullptr);
    if (!msg) return result;
    bool ok = CryptMsgUpdate(msg, reinterpret_cast<const BYTE *>(der.constData()),
                             DWORD(der.size()), TRUE);
    // One final update: Wine rejects detached content split over several.
    QByteArray content;
    for (QByteArrayView part : data) content.append(part);
    ok = ok && CryptMsgUpdate(msg, reinterpret_cast<const BYTE *>(content.constData()),
                              DWORD(content.size()), TRUE);

    DWORD size = 0;
    std::vector<BYTE> signerInfo;
    if (ok && CryptMsgGetParam(msg, CMSG_SIGNER_CERT_INFO_PARAM, 0, nullptr, &size)) {
        signerInfo.resize(size);
        ok = CryptMsgGetParam(msg, CMSG_SIGNER_CERT_INFO_PARAM, 0, signerInfo.data(), &size);
    } else {
        ok = false;
    }

    HCERTSTORE msgStore = ok ? CertOpenStore(CERT_STORE_PROV_MSG, kEncoding, 0, 0, msg) : nullptr;
    PCCERT_CONTEXT signer = msgStore
        ? CertGetSubjectCertificateFromStore(msgStore, kEncoding,
                                             reinterpret_cast<PCERT_INFO>(signerInfo.data()))
        : nullptr;
    if (signer && CryptMsgControl(msg, 0, CMSG_CTRL_VERIFY_SIGNATURE, signer->pCertInfo)
            && digestMatches(msg, content)) {
        result.intact = true;
        result.signer = certName(signer, 0);

        CERT_CHAIN_PARA chainPara {};
        chainPara.cbSize = sizeof chainPara;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        if (CertGetCertificateChain(nullptr, signer, nullptr, msgStore, &chainPara, 0, nullptr, &chain)) {
            CERT_CHAIN_POLICY_PARA policyPara {};
            policyPara.cbSize = sizeof policyPara;
            CERT_CHAIN_POLICY_STATUS status {};
            status.cbSize = sizeof status;
            result.trusted = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain,
                                                              &policyPara, &status)
                             && status.dwError == 0;
            CertFreeCertificateChain(chain);
        }
    }
    if (signer)   CertFreeCertificateContext(signer);
    if (msgStore) CertCloseStore(msgStore, 0);
    CryptMsgClose(msg);
    return result;
}


// Encodes the subject from its parts; a name string would have to be parsed
// and escaped, and Wine rejects quoted values in it.
QByteArray encodedName(const NewCertificate &request)
{
    // UTF8String values are handed over as UTF-16; the encoder converts them.
    auto wide = [](const QString &text) {
        return QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
    };
    struct Part { const char *oid; DWORD type; QByteArray value; };
    QList<Part> parts { { szOID_COMMON_NAME, CERT_RDN_UTF8_STRING, wide(request.name) } };
    if (!request.organization.isEmpty())
        parts.append({ szOID_ORGANIZATION_NAME, CERT_RDN_UTF8_STRING, wide(request.organization) });
    if (!request.email.isEmpty())
        parts.append({ szOID_RSA_emailAddr, CERT_RDN_IA5_STRING, request.email.toLatin1() });

    std::vector<CERT_RDN_ATTR> attrs(size_t(parts.size()));
    std::vector<CERT_RDN> rdns(size_t(parts.size()));
    for (int i = 0; i < parts.size(); ++i) {
        attrs[i] = { const_cast<LPSTR>(parts[i].oid), parts[i].type,
                     { DWORD(parts[i].value.size()),
                       reinterpret_cast<BYTE *>(parts[i].value.data()) } };
        rdns[i] = { 1, &attrs[i] };
    }
    CERT_NAME_INFO info { DWORD(rdns.size()), rdns.data() };
    DWORD size = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_NAME, &info, 0, nullptr, nullptr, &size))
        return {};
    QByteArray der(int(size), '\0');
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_NAME, &info, 0, nullptr, der.data(), &size))
        return {};
    der.resize(int(size));
    return der;
}

// The key goes into the CryptoAPI AES provider, which Windows and Wine both
// support, and the certificate into the user's personal store.
QString WindowsCertificateProvider::create(const NewCertificate &request, SignError *error)
{
    auto fail = [error](SignError e) { if (error) *error = e; return QString(); };

    const std::wstring container =
        (kContainerPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString();
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContextW(&prov, container.c_str(), MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES,
                              CRYPT_NEWKEYSET))
        return fail(SignError::SigningFailed);
    HCRYPTKEY key = 0;
    if (!CryptGenKey(prov, AT_SIGNATURE, 3072u << 16, &key)) {
        CryptReleaseContext(prov, 0);
        return fail(SignError::SigningFailed);
    }
    CryptDestroyKey(key);

    QByteArray nameDer = encodedName(request);
    CERT_NAME_BLOB name { DWORD(nameDer.size()), reinterpret_cast<BYTE *>(nameDer.data()) };
    const bool haveName = !nameDer.isEmpty();

    BYTE usageBits = CERT_DIGITAL_SIGNATURE_KEY_USAGE | CERT_NON_REPUDIATION_KEY_USAGE;
    CRYPT_BIT_BLOB usage { 1, &usageBits, 0 };
    BYTE *usageDer = nullptr;
    DWORD usageSize = 0;
    CryptEncodeObjectEx(X509_ASN_ENCODING, X509_KEY_USAGE, &usage, CRYPT_ENCODE_ALLOC_FLAG,
                        nullptr, &usageDer, &usageSize);
    CERT_EXTENSION ext { const_cast<LPSTR>(szOID_KEY_USAGE), TRUE, { usageSize, usageDer } };
    CERT_EXTENSIONS exts { usageDer ? 1u : 0u, &ext };

    CRYPT_KEY_PROV_INFO provInfo {};
    provInfo.pwszContainerName = const_cast<LPWSTR>(container.c_str());
    provInfo.pwszProvName = const_cast<LPWSTR>(MS_ENH_RSA_AES_PROV_W);
    provInfo.dwProvType = PROV_RSA_AES;
    provInfo.dwKeySpec = AT_SIGNATURE;
    CRYPT_ALGORITHM_IDENTIFIER algorithm {};
    algorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);

    SYSTEMTIME start, end;
    GetSystemTime(&start);
    end = start;
    end.wYear = WORD(end.wYear + qMax(1, request.years));
    if (end.wMonth == 2 && end.wDay == 29) end.wDay = 28;

    PCCERT_CONTEXT cert = haveName
        ? CertCreateSelfSignCertificate(prov, &name, 0, &provInfo, &algorithm, &start, &end, &exts)
        : nullptr;
    if (usageDer) LocalFree(usageDer);

    QString id;
    if (cert) {
        HCERTSTORE my = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                      CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
        PCCERT_CONTEXT added = nullptr;
        if (my && CertAddCertificateContextToStore(my, cert, CERT_STORE_ADD_NEW, &added)) {
            id = QString::fromLatin1(thumbprint(added).toHex());
            CertFreeCertificateContext(added);
        }
        if (my) CertCloseStore(my, 0);
        CertFreeCertificateContext(cert);
    }
    CryptReleaseContext(prov, 0);
    if (error) *error = id.isEmpty() ? SignError::WriteFailed : SignError::None;
    return id;
}

bool WindowsCertificateProvider::remove(const QString &certificateId)
{
    HCERTSTORE my = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                  CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
    if (!my) return false;
    const QByteArray hash = QByteArray::fromHex(certificateId.toLatin1());
    CRYPT_HASH_BLOB blob { DWORD(hash.size()),
                           reinterpret_cast<BYTE *>(const_cast<char *>(hash.constData())) };
    PCCERT_CONTEXT cert = CertFindCertificateInStore(my, kEncoding, 0, CERT_FIND_SHA1_HASH,
                                                     &blob, nullptr);
    bool removed = false;
    if (cert) {
        CRYPT_KEY_PROV_INFO info {};
        std::vector<BYTE> buffer;
        if (!ownContainer(cert, &info, &buffer).isEmpty()) {
            HCRYPTPROV prov = 0;
            CryptAcquireContextW(&prov, info.pwszContainerName, info.pwszProvName,
                                 info.dwProvType, CRYPT_DELETEKEYSET);
            removed = CertDeleteCertificateFromStore(cert);
        } else {
            CertFreeCertificateContext(cert);
        }
    }
    CertCloseStore(my, 0);
    return removed;
}
}

std::unique_ptr<CertificateProvider> createPlatformProvider()
{
    return std::make_unique<WindowsCertificateProvider>();
}

#endif
