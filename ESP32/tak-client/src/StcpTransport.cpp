#include "StcpTransport.h"

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 8000;
}

bool StcpTransport::begin() {
  return true;  // nothing to set up for plaintext
}

bool StcpTransport::connect(const char* host, uint16_t port) {
  client_.stop();
  client_.setTimeout(CONNECT_TIMEOUT_MS / 1000);
  if (!client_.connect(host, port, CONNECT_TIMEOUT_MS)) {
    return false;
  }
  // TAK reports are small and latency-sensitive; Nagle would coalesce them.
  client_.setNoDelay(true);
  return true;
}

bool StcpTransport::connected() {
  return client_.connected();
}

bool StcpTransport::send(const String& payload) {
  if (!client_.connected()) {
    return false;
  }
  const size_t written = client_.write((const uint8_t*)payload.c_str(), payload.length());
  return written == payload.length();
}

int StcpTransport::read(uint8_t* buf, size_t len) {
  const int available = client_.available();
  if (available <= 0) {
    return 0;
  }
  return client_.read(buf, min((size_t)available, len));
}

void StcpTransport::stop() {
  client_.stop();
}
