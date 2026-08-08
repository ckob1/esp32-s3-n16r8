/*
 * Application-level replacement for arduino-esp32's esp_crt_bundle callback.
 *
 * The stock callback only searches the bundle for the TOP presented
 * certificate's *issuer*. Chains that end in a cross-signed root (e.g. a
 * Sectigo R46 root re-issued by USERTrust, itself re-issued by the retired
 * AAA Certificate Services) therefore fail even though the root is present
 * in the Mozilla bundle. Here, when no issuer match exists, we additionally
 * accept the top certificate as a trust anchor if its own subject name AND
 * public key exactly match a bundle entry - the same rule OpenSSL applies.
 *
 * Defining arduino_esp_crt_bundle_set/attach/detach here means the linker
 * never pulls the library's esp_crt_bundle.o, so no symbol clash occurs.
 */

#include <string.h>

#include <esp_err.h>
#include <esp32-hal-log.h>
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>

#include "cert_bundle.h"
#include "esp_crt_bundle.h"

#define BUNDLE_HEADER_OFFSET 2
#define CRT_HEADER_OFFSET 4
#define MAX_BUNDLE_CERTS 200  // matches CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS

static const uint8_t *s_x509_bundle = nullptr;
static uint16_t s_num_certs = 0;

/* Dummy CA so the SSL layer does not reject the handshake for a NULL CA. */
static mbedtls_x509_crt s_dummy_crt;

static const uint8_t *bundle_entry_at(uint16_t idx) {
    const uint8_t *cur = s_x509_bundle + BUNDLE_HEADER_OFFSET;
    for (uint16_t i = 0; i < idx; ++i) {
        size_t name_len = ((size_t)cur[0] << 8) | cur[1];
        size_t key_len = ((size_t)cur[2] << 8) | cur[3];
        cur += CRT_HEADER_OFFSET + name_len + key_len;
    }
    return cur;
}

static esp_err_t bundle_init(const uint8_t *x509_bundle) {
    const uint16_t num = (uint16_t)((x509_bundle[0] << 8) | x509_bundle[1]);
    if (num == 0 || num > MAX_BUNDLE_CERTS) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate that every entry lies inside the embedded bundle blob. */
    const uint8_t *end = x509_bundle + X509_CRT_BUNDLE_LEN;
    const uint8_t *cur = x509_bundle + BUNDLE_HEADER_OFFSET;
    for (uint16_t i = 0; i < num; ++i) {
        if (cur + CRT_HEADER_OFFSET > end) {
            return ESP_ERR_INVALID_ARG;
        }
        size_t name_len = ((size_t)cur[0] << 8) | cur[1];
        size_t key_len = ((size_t)cur[2] << 8) | cur[3];
        cur += CRT_HEADER_OFFSET + name_len + key_len;
        if (cur > end) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (cur != end) {
        return ESP_ERR_INVALID_ARG;
    }

    s_x509_bundle = x509_bundle;
    s_num_certs = num;
    return ESP_OK;
}

/* Lexicographic byte compare (bundle is sorted by name bytes). */
static int name_cmp(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
    size_t n = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, n);
    if (cmp != 0) return cmp;
    if (a_len == b_len) return 0;
    return a_len < b_len ? -1 : 1;
}

static const uint8_t *bundle_find_name(const uint8_t *name, size_t name_len) {
    if (name == nullptr || s_num_certs == 0) {
        return nullptr;
    }

    int start = 0;
    int end = s_num_certs - 1;
    while (start <= end) {
        int middle = start + (end - start) / 2;
        const uint8_t *entry = bundle_entry_at((uint16_t)middle);
        size_t entry_name_len = ((size_t)entry[0] << 8) | entry[1];
        const uint8_t *entry_name = entry + CRT_HEADER_OFFSET;
        int cmp = name_cmp(entry_name, entry_name_len, name, name_len);
        if (cmp == 0) return entry;
        if (cmp < 0) start = middle + 1;
        else end = middle - 1;
    }
    return nullptr;
}

static int check_signature(mbedtls_x509_crt *child, const uint8_t *pub_key_buf,
                           size_t pub_key_len) {
    int ret;
    mbedtls_x509_crt parent;
    const mbedtls_md_info_t *md_info;
    unsigned char hash[MBEDTLS_MD_MAX_SIZE];

    mbedtls_x509_crt_init(&parent);

    if ((ret = mbedtls_pk_parse_public_key(&parent.pk, pub_key_buf, pub_key_len)) != 0) {
        goto cleanup;
    }

    if (!mbedtls_pk_can_do(&parent.pk, child->sig_pk)) {
        ret = MBEDTLS_ERR_PK_BAD_INPUT_DATA;
        goto cleanup;
    }

    md_info = mbedtls_md_info_from_type(child->sig_md);
    if (md_info == nullptr ||
        (ret = mbedtls_md(md_info, child->tbs.p, child->tbs.len, hash)) != 0) {
        goto cleanup;
    }

    ret = mbedtls_pk_verify_ext(child->sig_pk, child->sig_opts, &parent.pk,
                                child->sig_md, hash,
                                mbedtls_md_get_size(md_info), child->sig.p,
                                child->sig.len);

cleanup:
    mbedtls_x509_crt_free(&parent);
    return ret;
}

static bool pubkey_equals(const mbedtls_pk_context *a, const mbedtls_pk_context *b) {
    if (a == nullptr || b == nullptr) return false;
    if (mbedtls_pk_get_type(a) != mbedtls_pk_get_type(b)) return false;

    mbedtls_pk_type_t type = mbedtls_pk_get_type(a);
    if (type == MBEDTLS_PK_RSA) {
        const mbedtls_rsa_context *ra = mbedtls_pk_rsa(*a);
        const mbedtls_rsa_context *rb = mbedtls_pk_rsa(*b);
        if (ra == nullptr || rb == nullptr) return false;
        return mbedtls_mpi_cmp_mpi(&ra->N, &rb->N) == 0 &&
               mbedtls_mpi_cmp_mpi(&ra->E, &rb->E) == 0;
    }

    if (type == MBEDTLS_PK_ECKEY || type == MBEDTLS_PK_ECKEY_DH) {
        const mbedtls_ecp_keypair *ea = mbedtls_pk_ec(*a);
        const mbedtls_ecp_keypair *eb = mbedtls_pk_ec(*b);
        if (ea == nullptr || eb == nullptr) return false;
        if (ea->grp.id != eb->grp.id) return false;
        return mbedtls_ecp_point_cmp(&ea->Q, &eb->Q) == 0;
    }

    return false;
}

/* Accept `child` only when its subject name + public key match a bundle root. */
static int try_anchor(mbedtls_x509_crt *child) {
    const uint8_t *entry =
        bundle_find_name(child->subject_raw.p, child->subject_raw.len);
    if (entry == nullptr) {
        return MBEDTLS_ERR_X509_FATAL_ERROR;
    }

    size_t name_len = ((size_t)entry[0] << 8) | entry[1];
    size_t key_len = ((size_t)entry[2] << 8) | entry[3];
    const uint8_t *key = entry + CRT_HEADER_OFFSET + name_len;

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, key, key_len);
    if (ret == 0 && !pubkey_equals(&pk, &child->pk)) {
        ret = MBEDTLS_ERR_X509_FATAL_ERROR;
    }
    mbedtls_pk_free(&pk);
    return ret;
}

static int verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)buf;
    (void)depth;

    mbedtls_x509_crt *child = crt;

    /* Trusted roots may legitimately use a weak signature hash. */
    uint32_t flags_filtered = *flags & ~(uint32_t)MBEDTLS_X509_BADCERT_BAD_MD;
    if (flags_filtered != MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
        return 0;
    }

    if (s_x509_bundle == nullptr || s_num_certs == 0) {
        return MBEDTLS_ERR_X509_FATAL_ERROR;
    }

    /* Classic path: child was signed by a root stored in the bundle. */
    const uint8_t *entry =
        bundle_find_name(child->issuer_raw.p, child->issuer_raw.len);
    if (entry != nullptr) {
        size_t name_len = ((size_t)entry[0] << 8) | entry[1];
        size_t key_len = ((size_t)entry[2] << 8) | entry[3];
        const uint8_t *key = entry + CRT_HEADER_OFFSET + name_len;
        if (check_signature(child, key, key_len) == 0) {
            *flags = 0;
            return 0;
        }
    }

    /* Cross-signed-chain path: anchor directly on the presented root. */
    if (try_anchor(child) == 0) {
        *flags = 0;
        return 0;
    }

    return MBEDTLS_ERR_X509_FATAL_ERROR;
}

extern "C" esp_err_t arduino_esp_crt_bundle_attach(void *conf) {
    if (s_x509_bundle == nullptr || s_num_certs == 0) {
        log_e("No certificate bundle set");
        return ESP_ERR_INVALID_STATE;
    }

    if (conf != nullptr) {
        mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)conf;
        mbedtls_x509_crt_init(&s_dummy_crt);
        mbedtls_ssl_conf_ca_chain(ssl_conf, &s_dummy_crt, nullptr);
        mbedtls_ssl_conf_verify(ssl_conf, verify_callback, nullptr);
    }
    return ESP_OK;
}

extern "C" void arduino_esp_crt_bundle_detach(mbedtls_ssl_config *conf) {
    if (conf != nullptr) {
        mbedtls_ssl_conf_verify(conf, nullptr, nullptr);
    }
}

extern "C" void arduino_esp_crt_bundle_set(const uint8_t *x509_bundle) {
    s_x509_bundle = nullptr;
    s_num_certs = 0;
    if (x509_bundle != nullptr && bundle_init(x509_bundle) != ESP_OK) {
        log_e("Invalid certificate bundle rejected");
    }
}
