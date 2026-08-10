#pragma once

#include <WiFiClient.h>

#include "TakTransport.h"

// Plaintext streaming TCP against a TAK Server <input protocol="stcp"> port.
class StcpTransport : public TakTransport {
 public:
  bool begin() override;
  bool connect(const char* host, uint16_t port) override;
  bool connected() override;
  bool send(const String& payload) override;
  int read(uint8_t* buf, size_t len) override;
  void stop() override;
  const char* name() const override { return "stcp"; }

 private:
  WiFiClient client_;
};
