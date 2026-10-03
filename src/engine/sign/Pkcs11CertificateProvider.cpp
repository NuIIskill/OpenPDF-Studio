#include "engine/sign/CertificateProvider.hpp"
#include "engine/sign/FileCertificates.hpp"

#if defined(HAVE_DIGITAL_SIGNATURE) && !defined(_WIN32)

#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>
#include <gnutls/pkcs11.h>
#include <gnutls/pkcs7.h>
#include <gnutls/x509.h>
#include <p11-kit/p11-kit.h>
#include <p11-kit/uri.h>

#include <QTimeZone>

#include <cstring>
#include <vector>

namespace {

constexpr const char *kSigningCertificateV2 = "1.2.840.113549.1.9.16.2.47";

QString distinguishedName(gnutls_x509_crt_t crt)
{
    gnutls_datum_t dn {};
    if (gnutls_x509_crt_get_dn3(crt, &dn, 0) < 0) return {};
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(dn.data), dn.size);
    gnutls_free(dn.data);
    return text;
}

QString commonName(gnutls_x509_crt_t crt, bool issuer)
{
    char buf[512];
    size_t size = sizeof buf;
    const int rc = issuer
        ? gnutls_x509_crt_get_issuer_dn_by_oid(crt, GNUTLS_OID_X520_COMMON_NAME, 0, 0, buf, &size)
        : gnutls_x509_crt_get_dn_by_oid(crt, GNUTLS_OID_X520_COMMON_NAME, 0, 0, buf, &size);
    if (rc == 0) return QString::fromUtf8(buf, qsizetype(size));

    gnutls_datum_t dn {};
    if ((issuer ? gnutls_x509_crt_get_issuer_dn3(crt, &dn, 0)
                : gnutls_x509_crt_get_dn3(crt, &dn, 0)) < 0)
        return {};
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(dn.data), dn.size);
    gnutls_free(dn.data);
    return text;
}

QByteArray joined(const QList<QByteArrayView> &data)
{
    QByteArray out;
    qsizetype total = 0;
    for (QByteArrayView part : data) total += part.size();
    out.reserve(total);
    for (QByteArrayView part : data) out.append(part);
    return out;
}

gnutls_datum_t datum(const QByteArray &bytes)
{
    return { reinterpret_cast<unsigned char *>(const_cast<char *>(bytes.constData())),
             unsigned(bytes.size()) };
}

QByteArray digestInfoPrefix(gnutls_digest_algorithm_t digest)
{
    switch (digest) {
    case GNUTLS_DIG_SHA1:   return QByteArray::fromHex("3021300906052b0e03021a05000414");
    case GNUTLS_DIG_SHA256: return QByteArray::fromHex("3031300d060960864801650304020105000420");
    case GNUTLS_DIG_SHA384: return QByteArray::fromHex("3041300d060960864801650304020205000430");
    case GNUTLS_DIG_SHA512: return QByteArray::fromHex("3051300d060960864801650304020305000440");
    default:                return {};
    }
}

// The certificate's private key, used through p11-kit without logging in to
// the token. GnuTLS always logs in, and the app asks for no PIN.
class DirectKey
{
public:
    explicit DirectKey(const QString &certificateUrl);
    ~DirectKey();

    bool      tokenFound() const { return m_tokenFound; }
    bool      isOpen() const     { return m_handle != CK_INVALID_HANDLE; }
    bool      needsLogin() const { return m_needsLogin; }
    int       importInto(gnutls_privkey_t key);

private:
    bool open(CK_FUNCTION_LIST *module, CK_SLOT_ID slot, const QByteArray &id);
    int  sign(gnutls_sign_algorithm_t algo, const gnutls_datum_t *hash, gnutls_datum_t *signature);
    int  info(unsigned int flags) const;

    CK_FUNCTION_LIST **m_modules    { nullptr };
    CK_FUNCTION_LIST  *m_module     { nullptr };
    CK_SESSION_HANDLE  m_session    { CK_INVALID_HANDLE };
    CK_OBJECT_HANDLE   m_handle     { CK_INVALID_HANDLE };
    CK_KEY_TYPE        m_type       { CKK_RSA };
    int                m_bits       { 0 };
    bool               m_tokenFound { false };
    bool               m_needsLogin { false };
};

DirectKey::DirectKey(const QString &certificateUrl)
{
    P11KitUri *uri = p11_kit_uri_new();
    if (p11_kit_uri_parse(certificateUrl.toUtf8().constData(), P11_KIT_URI_FOR_ANY, uri) != P11_KIT_URI_OK) {
        p11_kit_uri_free(uri);
        return;
    }
    const CK_ATTRIBUTE *idAttr = p11_kit_uri_get_attribute(uri, CKA_ID);
    const QByteArray id = idAttr ? QByteArray(static_cast<const char *>(idAttr->pValue),
                                              qsizetype(idAttr->ulValueLen))
                                 : QByteArray();

    m_modules = p11_kit_modules_load_and_initialize(0);
    for (CK_FUNCTION_LIST **module = m_modules; module && *module && !isOpen(); ++module) {
        CK_ULONG count = 0;
        if ((*module)->C_GetSlotList(CK_TRUE, nullptr, &count) != CKR_OK || count == 0) continue;
        std::vector<CK_SLOT_ID> slots(count);
        if ((*module)->C_GetSlotList(CK_TRUE, slots.data(), &count) != CKR_OK) continue;
        for (CK_ULONG i = 0; i < count && !isOpen(); ++i) {
            CK_TOKEN_INFO token {};
            if ((*module)->C_GetTokenInfo(slots[i], &token) != CKR_OK
                    || !p11_kit_uri_match_token_info(uri, &token))
                continue;
            m_tokenFound = true;
            if (open(*module, slots[i], id)) m_module = *module;
            else m_needsLogin = m_needsLogin || (token.flags & CKF_LOGIN_REQUIRED);
        }
    }
    p11_kit_uri_free(uri);
}

DirectKey::~DirectKey()
{
    if (m_module && m_session != CK_INVALID_HANDLE) m_module->C_CloseSession(m_session);
    if (m_modules) p11_kit_modules_finalize_and_release(m_modules);
}

bool DirectKey::open(CK_FUNCTION_LIST *module, CK_SLOT_ID slot, const QByteArray &id)
{
    CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
    if (id.isEmpty()
            || module->C_OpenSession(slot, CKF_SERIAL_SESSION, nullptr, nullptr, &session) != CKR_OK)
        return false;

    CK_OBJECT_CLASS keyClass = CKO_PRIVATE_KEY;
    CK_ATTRIBUTE search[] = {
        { CKA_CLASS, &keyClass, sizeof keyClass },
        { CKA_ID, const_cast<char *>(id.constData()), CK_ULONG(id.size()) },
    };
    CK_OBJECT_HANDLE handle = CK_INVALID_HANDLE;
    CK_ULONG found = 0;
    if (module->C_FindObjectsInit(session, search, 2) == CKR_OK) {
        module->C_FindObjects(session, &handle, 1, &found);
        module->C_FindObjectsFinal(session);
    }
    if (found != 1) {
        module->C_CloseSession(session);
        return false;
    }

    CK_ATTRIBUTE type { CKA_KEY_TYPE, &m_type, sizeof m_type };
    module->C_GetAttributeValue(session, handle, &type, 1);
    CK_ATTRIBUTE modulus { CKA_MODULUS, nullptr, 0 };
    if (m_type == CKK_RSA && module->C_GetAttributeValue(session, handle, &modulus, 1) == CKR_OK)
        m_bits = int(modulus.ulValueLen * 8);
    m_session = session;
    m_handle = handle;
    return true;
}

int DirectKey::importInto(gnutls_privkey_t key)
{
    auto signHash = [](gnutls_privkey_t, gnutls_sign_algorithm_t algo, void *self, unsigned int,
                       const gnutls_datum_t *hash, gnutls_datum_t *signature) {
        return static_cast<DirectKey *>(self)->sign(algo, hash, signature);
    };
    auto keyInfo = [](gnutls_privkey_t, unsigned int flags, void *self) {
        return static_cast<DirectKey *>(self)->info(flags);
    };
    return gnutls_privkey_import_ext4(key, this, nullptr, signHash, nullptr, nullptr, keyInfo, 0);
}

int DirectKey::info(unsigned int flags) const
{
    const gnutls_pk_algorithm_t pk = m_type == CKK_EC ? GNUTLS_PK_ECDSA : GNUTLS_PK_RSA;
    if (flags & GNUTLS_PRIVKEY_INFO_PK_ALGO) return pk;
    if (flags & GNUTLS_PRIVKEY_INFO_PK_ALGO_BITS) return m_bits;
    if (flags & GNUTLS_PRIVKEY_INFO_HAVE_SIGN_ALGO) {
        const auto algo = gnutls_sign_algorithm_t(GNUTLS_FLAGS_TO_SIGN_ALGO(flags));
        return gnutls_sign_get_pk_algorithm(algo) == pk ? 1 : 0;
    }
    if (flags & GNUTLS_PRIVKEY_INFO_SIGN_ALGO)
        return pk == GNUTLS_PK_ECDSA ? GNUTLS_SIGN_ECDSA_SHA256 : GNUTLS_SIGN_RSA_SHA256;
    return -1;
}

int DirectKey::sign(gnutls_sign_algorithm_t algo, const gnutls_datum_t *hash, gnutls_datum_t *signature)
{
    QByteArray input(reinterpret_cast<const char *>(hash->data), qsizetype(hash->size));
    CK_MECHANISM mechanism { m_type == CKK_EC ? CKM_ECDSA : CKM_RSA_PKCS, nullptr, 0 };
    if (m_type != CKK_EC) {
        const gnutls_digest_algorithm_t digest = gnutls_sign_get_hash_algorithm(algo);
        if (input.size() == qsizetype(gnutls_hash_get_len(digest)))
            input.prepend(digestInfoPrefix(digest));
    }

    CK_ULONG length = 0;
    auto *in = reinterpret_cast<CK_BYTE *>(input.data());
    CK_RV rv = m_module->C_SignInit(m_session, &mechanism, m_handle);
    if (rv == CKR_OK) rv = m_module->C_Sign(m_session, in, CK_ULONG(input.size()), nullptr, &length);
    QByteArray raw(qsizetype(length), '\0');
    if (rv == CKR_OK)
        rv = m_module->C_Sign(m_session, in, CK_ULONG(input.size()),
                              reinterpret_cast<CK_BYTE *>(raw.data()), &length);
    if (rv != CKR_OK) {
        if (rv == CKR_USER_NOT_LOGGED_IN) m_needsLogin = true;
        return GNUTLS_E_PK_SIGN_FAILED;
    }
    raw.resize(qsizetype(length));

    if (m_type == CKK_EC) {
        const unsigned half = unsigned(raw.size() / 2);
        auto *bytes = reinterpret_cast<unsigned char *>(raw.data());
        const gnutls_datum_t r { bytes, half }, s { bytes + half, half };
        return gnutls_encode_rs_value(signature, &r, &s);
    }
    signature->data = static_cast<unsigned char *>(gnutls_malloc(size_t(raw.size())));
    if (!signature->data) return GNUTLS_E_MEMORY_ERROR;
    std::memcpy(signature->data, raw.constData(), size_t(raw.size()));
    signature->size = unsigned(raw.size());
    return 0;
}

class Pkcs11CertificateProvider : public CertificateProvider
{
public:
    Pkcs11CertificateProvider()  { gnutls_global_init(); }
    ~Pkcs11CertificateProvider() override { gnutls_global_deinit(); }

    QList<Certificate> certificates() override;
    QByteArray sign(const QString &certificateId, const QList<QByteArrayView> &data,
                    SignError *error) override;
    CmsCheck check(const QByteArray &cms, const QList<QByteArrayView> &data) override;
    QString create(const NewCertificate &request, SignError *error) override;
    bool remove(const QString &certificateId) override;
};

Certificate describe(gnutls_x509_crt_t crt, const QString &id)
{
    Certificate cert;
    cert.id                = id;
    cert.subject           = commonName(crt, false);
    cert.distinguishedName = distinguishedName(crt);
    cert.issuer            = commonName(crt, true);
    cert.validTo = QDateTime::fromSecsSinceEpoch(
        qint64(gnutls_x509_crt_get_expiration_time(crt)), QTimeZone::UTC);
    return cert;
}

// Builds the detached CMS over the signed ranges, the same for token and file keys.
QByteArray signCms(gnutls_x509_crt_t crt, gnutls_privkey_t key, const QList<QByteArrayView> &data,
                   int *rcOut)
{
    gnutls_pkcs7_t       p7    = nullptr;
    gnutls_pkcs7_attrs_t attrs = nullptr;
    gnutls_datum_t       der   {};
    gnutls_datum_t       out   {};
    QByteArray           cms;

    const QByteArray content = joined(data);
    int rc = gnutls_x509_crt_export2(crt, GNUTLS_X509_FMT_DER, &der);
    if (rc == 0) {
        const QByteArray ess = Cms::signingCertificateV2(
            QByteArray(reinterpret_cast<const char *>(der.data), der.size));
        gnutls_datum_t essDatum = datum(ess);
        rc = gnutls_pkcs7_add_attr(&attrs, kSigningCertificateV2, &essDatum, 0);
    }
    if (rc == 0) rc = gnutls_pkcs7_init(&p7);
    if (rc == 0) {
        gnutls_datum_t contentDatum = datum(content);
        rc = gnutls_pkcs7_sign(p7, crt, key, &contentDatum, attrs, nullptr,
                               GNUTLS_DIG_SHA256, GNUTLS_PKCS7_INCLUDE_CERT);
    }
    if (rc == 0) rc = gnutls_pkcs7_export2(p7, GNUTLS_X509_FMT_DER, &out);
    if (rc == 0) cms = QByteArray(reinterpret_cast<const char *>(out.data), out.size);

    gnutls_free(out.data);
    gnutls_free(der.data);
    if (attrs) gnutls_pkcs7_attrs_deinit(attrs);
    if (p7)    gnutls_pkcs7_deinit(p7);
    if (rcOut) *rcOut = rc;
    return cms;
}

QList<Certificate> Pkcs11CertificateProvider::certificates()
{
    QList<Certificate> out;
    gnutls_pkcs11_obj_t *objects = nullptr;
    unsigned int count = 0;
    if (gnutls_pkcs11_obj_list_import_url4(&objects, &count, "pkcs11:",
                                           GNUTLS_PKCS11_OBJ_FLAG_CRT) < 0)
        return out;

    for (unsigned int i = 0; i < count; ++i) {
        gnutls_x509_crt_t crt = nullptr;
        char *url = nullptr;
        unsigned int flags = 0;
        // Keys usually stay hidden until login, so the list cannot ask for
        // them. The trust anchors p11-kit adds are marked trusted; a
        // certificate on a token is not.
        gnutls_pkcs11_obj_get_flags(objects[i], &flags);
        if (!(flags & (GNUTLS_PKCS11_OBJ_FLAG_MARK_TRUSTED | GNUTLS_PKCS11_OBJ_FLAG_MARK_DISTRUSTED))
                && gnutls_x509_crt_init(&crt) == 0
                && gnutls_x509_crt_import_pkcs11(crt, objects[i]) == 0
                && gnutls_pkcs11_obj_export_url(objects[i], GNUTLS_PKCS11_URL_GENERIC, &url) == 0) {
            out << describe(crt, QString::fromUtf8(url));
        }
        gnutls_free(url);
        if (crt) gnutls_x509_crt_deinit(crt);
        gnutls_pkcs11_obj_deinit(objects[i]);
    }
    gnutls_free(objects);

    for (const QString &id : FileCertificates::ids()) {
        const QByteArray pem = FileCertificates::certificatePem(id);
        gnutls_datum_t pemDatum = datum(pem);
        gnutls_x509_crt_t crt = nullptr;
        if (gnutls_x509_crt_init(&crt) == 0
                && gnutls_x509_crt_import(crt, &pemDatum, GNUTLS_X509_FMT_PEM) == 0) {
            Certificate cert = describe(crt, id);
            cert.removable = true;
            out << cert;
        }
        if (crt) gnutls_x509_crt_deinit(crt);
    }
    return out;
}

QByteArray Pkcs11CertificateProvider::sign(const QString &certificateId,
                                           const QList<QByteArrayView> &data, SignError *error)
{
    gnutls_x509_crt_t crt = nullptr;
    gnutls_privkey_t  key = nullptr;
    QByteArray        cms;
    SignError         result = SignError::SigningFailed;

    if (FileCertificates::isFileId(certificateId)) {
        const QByteArray certPem = FileCertificates::certificatePem(certificateId);
        const QByteArray keyPem  = FileCertificates::keyPem(certificateId);
        gnutls_datum_t certDatum = datum(certPem), keyDatum = datum(keyPem);
        int rc = gnutls_x509_crt_init(&crt);
        if (rc == 0) rc = gnutls_x509_crt_import(crt, &certDatum, GNUTLS_X509_FMT_PEM);
        if (rc == 0) rc = gnutls_privkey_init(&key);
        if (rc == 0) rc = gnutls_privkey_import_x509_raw(key, &keyDatum, GNUTLS_X509_FMT_PEM, nullptr, 0);
        if (rc < 0) {
            result = SignError::CertificateNotFound;
        } else {
            cms = signCms(crt, key, data, &rc);
            if (rc == 0) result = SignError::None;
        }
    } else {
        DirectKey direct(certificateId);
        int rc = gnutls_x509_crt_init(&crt);
        if (rc == 0) rc = gnutls_x509_crt_import_url(crt, certificateId.toUtf8().constData(), 0);
        if (rc < 0 || !direct.tokenFound()) {
            result = SignError::CertificateNotFound;
        } else if (!direct.isOpen()) {
            result = direct.needsLogin() ? SignError::PinRequired : SignError::CertificateNotFound;
        } else {
            rc = gnutls_privkey_init(&key);
            if (rc == 0) rc = direct.importInto(key);
            if (rc == 0) cms = signCms(crt, key, data, &rc);
            if (rc == 0) result = SignError::None;
            else result = direct.needsLogin() ? SignError::PinRequired : SignError::SigningFailed;
        }
        // The external key points into the session DirectKey owns.
        if (key) gnutls_privkey_deinit(key);
        key = nullptr;
    }

    if (key) gnutls_privkey_deinit(key);
    if (crt) gnutls_x509_crt_deinit(crt);
    if (error) *error = result;
    return cms;
}

QString Pkcs11CertificateProvider::create(const NewCertificate &request, SignError *error)
{
    gnutls_x509_privkey_t key = nullptr;
    gnutls_x509_crt_t     crt = nullptr;
    gnutls_datum_t        keyPem {}, certPem {};
    QString               id;

    const QByteArray name = request.name.toUtf8();
    const QByteArray org  = request.organization.toUtf8();
    const QByteArray mail = request.email.toUtf8();
    unsigned char serial[16];
    gnutls_rnd(GNUTLS_RND_NONCE, serial, sizeof serial);
    serial[0] &= 0x7F;
    const time_t now = time(nullptr);

    int rc = gnutls_x509_privkey_init(&key);
    if (rc == 0) rc = gnutls_x509_privkey_generate(key, GNUTLS_PK_RSA, 3072, 0);
    if (rc == 0) rc = gnutls_x509_crt_init(&crt);
    if (rc == 0) rc = gnutls_x509_crt_set_version(crt, 3);
    if (rc == 0) rc = gnutls_x509_crt_set_serial(crt, serial, sizeof serial);
    if (rc == 0) rc = gnutls_x509_crt_set_activation_time(crt, now - 3600);
    if (rc == 0) rc = gnutls_x509_crt_set_expiration_time(
                     crt, now + time_t(qMax(1, request.years)) * 365 * 24 * 3600);
    if (rc == 0) rc = gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_COMMON_NAME, 0,
                                                    name.constData(), unsigned(name.size()));
    if (rc == 0 && !org.isEmpty())
        rc = gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_X520_ORGANIZATION_NAME, 0,
                                           org.constData(), unsigned(org.size()));
    if (rc == 0 && !mail.isEmpty())
        rc = gnutls_x509_crt_set_dn_by_oid(crt, GNUTLS_OID_PKCS9_EMAIL, 0,
                                           mail.constData(), unsigned(mail.size()));
    if (rc == 0) rc = gnutls_x509_crt_set_key(crt, key);
    if (rc == 0) rc = gnutls_x509_crt_set_basic_constraints(crt, 0, -1);
    if (rc == 0) rc = gnutls_x509_crt_set_key_usage(
                     crt, GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_NON_REPUDIATION);
    if (rc == 0) {
        unsigned char keyId[64];
        size_t keyIdSize = sizeof keyId;
        if (gnutls_x509_crt_get_key_id(crt, GNUTLS_KEYID_USE_SHA1, keyId, &keyIdSize) == 0)
            gnutls_x509_crt_set_subject_key_id(crt, keyId, keyIdSize);
        rc = gnutls_x509_crt_sign2(crt, crt, key, GNUTLS_DIG_SHA256, 0);
    }
    if (rc == 0) rc = gnutls_x509_privkey_export2(key, GNUTLS_X509_FMT_PEM, &keyPem);
    if (rc == 0) rc = gnutls_x509_crt_export2(crt, GNUTLS_X509_FMT_PEM, &certPem);
    if (rc == 0)
        id = FileCertificates::store(
            QByteArray(reinterpret_cast<const char *>(certPem.data), certPem.size),
            QByteArray(reinterpret_cast<const char *>(keyPem.data), keyPem.size));

    gnutls_free(certPem.data);
    if (keyPem.data) {
        std::memset(keyPem.data, 0, keyPem.size);
        gnutls_free(keyPem.data);
    }
    if (crt) gnutls_x509_crt_deinit(crt);
    if (key) gnutls_x509_privkey_deinit(key);
    if (error) *error = rc < 0 ? SignError::SigningFailed : id.isEmpty() ? SignError::WriteFailed
                                                                         : SignError::None;
    return id;
}

bool Pkcs11CertificateProvider::remove(const QString &certificateId)
{
    return FileCertificates::isFileId(certificateId) && FileCertificates::remove(certificateId);
}

CmsCheck Pkcs11CertificateProvider::check(const QByteArray &cms, const QList<QByteArrayView> &data)
{
    CmsCheck result;
    const QByteArray der = Cms::trimmed(cms);
    const QByteArray content = joined(data);
    gnutls_datum_t derDatum = datum(der);
    gnutls_datum_t contentDatum = datum(content);

    gnutls_pkcs7_t p7 = nullptr;
    if (gnutls_pkcs7_init(&p7) < 0) return result;
    if (gnutls_pkcs7_import(p7, &derDatum, GNUTLS_X509_FMT_DER) < 0) {
        gnutls_pkcs7_deinit(p7);
        return result;
    }

    const int certs = gnutls_pkcs7_get_crt_count(p7);
    for (int i = 0; i < certs && !result.intact; ++i) {
        gnutls_datum_t raw {};
        gnutls_x509_crt_t crt = nullptr;
        if (gnutls_pkcs7_get_crt_raw2(p7, unsigned(i), &raw) == 0
                && gnutls_x509_crt_init(&crt) == 0
                && gnutls_x509_crt_import(crt, &raw, GNUTLS_X509_FMT_DER) == 0
                && gnutls_pkcs7_verify_direct(p7, crt, 0, &contentDatum, 0) == 0) {
            result.intact = true;
            result.signer = commonName(crt, false);
        }
        if (crt) gnutls_x509_crt_deinit(crt);
        gnutls_free(raw.data);
    }

    gnutls_x509_trust_list_t trust = nullptr;
    if (result.intact && gnutls_x509_trust_list_init(&trust, 0) == 0) {
        gnutls_x509_trust_list_add_system_trust(trust, 0, 0);
        result.trusted = gnutls_pkcs7_verify(p7, trust, nullptr, 0, 0, &contentDatum, 0) == 0;
        gnutls_x509_trust_list_deinit(trust, 1);
    }
    gnutls_pkcs7_deinit(p7);
    return result;
}

}

std::unique_ptr<CertificateProvider> createPlatformProvider()
{
    return std::make_unique<Pkcs11CertificateProvider>();
}

#endif
