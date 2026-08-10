#include "config.h"

// The whole implementation compiles away unless TLS is selected, so a
// bring-up build over stcp does not need certs.h to exist.
#ifdef TAK_USE_TLS

#include "TlsTransport.h"

#if __has_include("certs.h")
  #include "certs.h"
#else
  #error "TAK_USE_TLS is set: copy include/certs.example.h to include/certs.h and paste in your PEMs."
#endif

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;  // TLS handshake needs headroom
}

bool TlsTransport::begin() {
  // Verify the server against our own CA. TAK's CA is self-signed and is the
  // trust anchor for the whole deployment, so this is the correct check —
  // never replace it with setInsecure().
  client_.setCACert(TAK_CA_PEM);

  // Our client certificate. This is how the server authenticates us; without
  // it the handshake is rejected and no contact is ever created.
  client_.setCertificate(TAK_CLIENT_CERT_PEM);
  client_.setPrivateKey(TAK_CLIENT_KEY_PEM);

  client_.setHandshakeTimeout(CONNECT_TIMEOUT_MS / 1000);
  return true;
}

bool TlsTransport::connect(const char* host, uint16_t port) {
  client_.stop();
  if (!client_.connect(host, port, CONNECT_TIMEOUT_MS)) {
    // Almost always one of: clock not yet NTP-synced so the cert reads as not
    // yet valid, TAK_HOST not matching the server certificate's CN, or the
    // client cert not registered on the server.
    return false;
  }
  return true;
}

bool TlsTransport::connected() {
  return client_.connected();
}

bool TlsTransport::send(const String& payload) {
  if (!client_.connected()) {
    return false;
  }
  const size_t written = client_.write((const uint8_t*)payload.c_str(), payload.length());
  return written == payload.length();
}

int TlsTransport::read(uint8_t* buf, size_t len) {
  const int available = client_.available();
  if (available <= 0) {
    return 0;
  }
  return client_.read(buf, min((size_t)available, len));
}

void TlsTransport::stop() {
  client_.stop();
}

#endif  // TAK_USE_TLS
