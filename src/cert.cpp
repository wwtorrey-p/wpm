#include "cert.h"

#include <cstdio>
#include <vector>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/asn1.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"

static const char* TAG = "cert";

static bool readStr(nvs_handle_t h, const char* key, std::string& out) {
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len < 2) return false;
    std::vector<char> buf(len);
    if (nvs_get_str(h, key, buf.data(), &len) != ESP_OK) return false;
    out.assign(buf.data());
    return true;
}

static bool generate(std::string& certPem, std::string& keyPem, const unsigned char* ip) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);

    bool ok = false;
    std::vector<unsigned char> certBuf(2048), keyBuf(1024);
    unsigned char serial[8];
    const char* name = "CN=water-pump-monitor,O=pump";
    // Clients check the address they browse to against the SAN, not the CN.
    mbedtls_x509_san_list san{};
    san.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
    san.node.san.unstructured_name.p = const_cast<unsigned char*>(ip);
    san.node.san.unstructured_name.len = 4;
    mbedtls_asn1_sequence serverAuth{};
    serverAuth.buf.tag = MBEDTLS_ASN1_OID;
    serverAuth.buf.p = (unsigned char*)MBEDTLS_OID_SERVER_AUTH;
    serverAuth.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);

    do {
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)"pump", 4)) break;
        if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) break;
        if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &drbg)) break;

        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        if (mbedtls_x509write_crt_set_subject_name(&crt, name)) break;
        if (mbedtls_x509write_crt_set_issuer_name(&crt, name)) break;
        esp_fill_random(serial, sizeof serial);
        serial[0] &= 0x7f;
        if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof serial)) break;
        // Fixed window: the device clock is not trustworthy at first boot.
        if (mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20740101000000")) break;
        if (mbedtls_x509write_crt_set_basic_constraints(&crt, 1, 0)) break;  // Android only installs CA:TRUE certs without a private key
        if (ip && mbedtls_x509write_crt_set_subject_alternative_name(&crt, &san)) break;
        if (mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_CERT_SIGN)) break;
        if (mbedtls_x509write_crt_set_ext_key_usage(&crt, &serverAuth)) break;

        if (mbedtls_x509write_crt_pem(&crt, certBuf.data(), certBuf.size(), mbedtls_ctr_drbg_random, &drbg)) break;
        if (mbedtls_pk_write_key_pem(&key, keyBuf.data(), keyBuf.size())) break;
        certPem.assign((char*)certBuf.data());
        keyPem.assign((char*)keyBuf.data());
        ok = true;
    } while (false);

    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}

static bool parseIp(const std::string& s, unsigned char out[4]) {
    unsigned a, b, c, d;
    char tail;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    out[0] = a, out[1] = b, out[2] = c, out[3] = d;
    return true;
}

static bool store(nvs_handle_t h, const std::string& cert, const std::string& key, const std::string& ip) {
    return nvs_set_str(h, "cert", cert.c_str()) == ESP_OK && nvs_set_str(h, "key", key.c_str()) == ESP_OK &&
           nvs_set_str(h, "certip", ip.c_str()) == ESP_OK && nvs_commit(h) == ESP_OK;
}

bool ensureCert(std::string& certPem, std::string& keyPem) {
    nvs_handle_t h;
    if (nvs_open("pump", NVS_READWRITE, &h) != ESP_OK) return false;
    if (readStr(h, "cert", certPem) && readStr(h, "key", keyPem)) {
        nvs_close(h);
        return true;
    }
    // No address is known at first boot, so this cert has no SAN until regenerateCert() is used.
    ESP_LOGI(TAG, "generating self-signed certificate");
    bool ok = generate(certPem, keyPem, nullptr) && store(h, certPem, keyPem, "");
    nvs_close(h);
    return ok;
}

bool regenerateCert(const std::string& ip) {
    unsigned char addr[4];
    if (!parseIp(ip, addr)) return false;
    std::string cert, key;
    if (!generate(cert, key, addr)) return false;
    nvs_handle_t h;
    if (nvs_open("pump", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = store(h, cert, key, ip);
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "new certificate for %s stored", ip.c_str());
    return ok;
}

std::string certIp() {
    nvs_handle_t h;
    std::string ip;
    if (nvs_open("pump", NVS_READONLY, &h) != ESP_OK) return ip;
    readStr(h, "certip", ip);
    nvs_close(h);
    return ip;
}
