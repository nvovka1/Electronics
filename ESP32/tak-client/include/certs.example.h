/*
 * Copy this file to include/certs.h and paste in your own PEMs.
 * certs.h is gitignored — the private key below must never be committed.
 *
 * Only needed when TAK_USE_TLS is enabled in config.h.
 *
 * ---------------------------------------------------------------------------
 * Producing the PEMs
 * ---------------------------------------------------------------------------
 * TAK's makeCert.sh emits PKCS#12 and JKS, neither of which the ESP32 reads.
 * Convert on the server, in utils/misc/certs/files:
 *
 *   # Trust anchor — the CA that signed the server certificate
 *   openssl x509 -in ca.pem -out ca-only.pem
 *
 *   # Client certificate and its unencrypted private key
 *   openssl pkcs12 -in esp32.p12 -clcerts -nokeys  -out esp32-cert.pem -passin pass:atakatak
 *   openssl pkcs12 -in esp32.p12 -nocerts -nodes   -out esp32-key.pem  -passin pass:atakatak
 *
 * Generate the client certificate first with:  ./makeCert.sh client esp32
 * then register it with the server:
 *   java -jar takserver-usermanager-<version>.jar certmod -A certs/files/esp32.pem
 *
 * Paste each file's contents below, keeping the BEGIN/END lines. Every line
 * needs its own "..\n" — R"EOF(...)EOF" raw strings work too and are easier to
 * paste; both are shown.
 */
#pragma once

// Trust anchor. The server's certificate must chain to this.
static const char TAK_CA_PEM[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
...paste ca-only.pem here...
-----END CERTIFICATE-----
)EOF";

// Our client certificate, presented to the server during the handshake.
static const char TAK_CLIENT_CERT_PEM[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
...paste esp32-cert.pem here...
-----END CERTIFICATE-----
)EOF";

// Matching private key. Keep this file out of version control.
static const char TAK_CLIENT_KEY_PEM[] PROGMEM = R"EOF(
-----BEGIN PRIVATE KEY-----
...paste esp32-key.pem here...
-----END PRIVATE KEY-----
)EOF";
