#pragma once

#include <WiFiClientSecure.h>

#include "TakTransport.h"

// Mutual X.509 TLS against the TAK Server <input protocol="tls"> port (8089).
// Requires include/certs.h — see certs.example.h for how to produce it from
// the CA and client certificate that makeCert.sh generates.
//
// Budget roughly 45 KB of heap for the mbedTLS session, and expect the first
// handshake to take a few seconds.
class TlsTransport : public TakTransport {
 public:
  bool begin() override;
  bool connect(const char* host, uint16_t port) override;
  bool connected() override;
  bool send(const String& payload) override;
  int read(uint8_t* buf, size_t len) override;
  void stop() override;
  const char* name() const override { return "tls"; }

 private:
  WiFiClientSecure client_;
};
