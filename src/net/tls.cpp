#include "net/tls.hpp"

#ifndef _WIN32
#include <openssl/ssl.h>

#include <boost/asio/ssl/host_name_verification.hpp>

#include "common/error.hpp"
#endif

#if defined(__ANDROID__)
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <filesystem>

#include <spdlog/spdlog.h>
#endif

namespace sbc::net {

#ifdef _WIN32

// Windows: Schannel via wintls. The system trust store provides the CA roots and
// performs chain + hostname validation during the handshake.
TlsContext::TlsContext() : ctx_(wintls::method::system_default) {
    ctx_.use_default_certificates(true);
    ctx_.verify_server_certificate(true);
}

void tls_set_client_hostname(TlsStream& stream, const std::string& host) {
    // Sets the SNI host name and the name checked against the certificate during
    // the handshake (chain validation against the Windows trust store).
    stream.set_server_hostname(host);
}

#else

namespace ssl = boost::asio::ssl;

#if defined(__ANDROID__)
namespace {

// Load Android's system CA roots into the context's X509 store by reading each
// certificate file directly, rather than via OpenSSL's hashed-directory lookup
// (set_default_verify_paths / add_verify_path). That lookup is unusable on
// Android: its cacerts files are named with the legacy subject hash
// (X509_NAME_hash_old), but OpenSSL 3.x computes the new-style hash when probing
// a verify directory, so it never finds them and every handshake fails with
// "certificate verify failed". Reading the files outright avoids the naming
// mismatch and works on every release.
//
// Directories, newest store first: the Conscrypt APEX holds the updatable roots
// on Android 14+ (API 34); the classic location exists on all releases. A cert
// already present (duplicate across the two dirs) is rejected by the store with a
// benign error we clear and ignore.
void load_android_system_cas(ssl::context& ctx) {
    X509_STORE* store = SSL_CTX_get_cert_store(ctx.native_handle());
    if (store == nullptr) {
        return;
    }

    int loaded = 0;
    for (const char* dir :
         {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec), end;
        if (ec) {
            continue;  // store absent on this release (e.g. no APEX pre-14)
        }
        for (; it != end; it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) {
                continue;
            }
            BIO* bio = BIO_new_file(it->path().c_str(), "r");
            if (bio == nullptr) {
                continue;
            }
            // Each file may hold a PEM block followed by human-readable text;
            // PEM_read_bio_X509 scans for the BEGIN CERTIFICATE marker.
            X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (cert == nullptr) {
                continue;
            }
            if (X509_STORE_add_cert(store, cert) == 1) {
                ++loaded;
            }
            X509_free(cert);
        }
    }
    ERR_clear_error();  // drop benign duplicate-cert errors

    if (loaded == 0) {
        spdlog::warn(
            "tls: no Android system CA certificates were loaded; "
            "upstream TLS verification will fail");
    } else {
        spdlog::info("tls: loaded {} Android system CA certificate(s)", loaded);
    }
}

}  // namespace
#endif

TlsContext::TlsContext() : ctx_(ssl::context::tls_client) {
    ctx_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                     ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
    // Honours SSL_CERT_FILE / SSL_CERT_DIR and OpenSSL's compiled-in OPENSSLDIR.
    ctx_.set_default_verify_paths();

#if defined(__ANDROID__)
    load_android_system_cas(ctx_);
#endif

    ctx_.set_verify_mode(ssl::verify_peer);
}

void tls_set_client_hostname(TlsStream& stream, const std::string& host) {
    if (SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()) != 1) {
        throw Error("tls: failed to set SNI host name");
    }
    stream.set_verify_callback(ssl::host_name_verification(host));
    static const unsigned char kAlpn[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    SSL_set_alpn_protos(stream.native_handle(), kAlpn, sizeof(kAlpn));
}

#endif

}  // namespace sbc::net