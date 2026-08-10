/*
 * Transport abstraction for the TAK connection.
 *
 * Two implementations:
 *   StcpTransport — plaintext streaming TCP. No certificates, so it works
 *                   before the server's CA exists. Unauthenticated: restrict
 *                   the server port to your own source IP.
 *   TlsTransport  — mutual X.509 TLS on 8089. What a real ATAK device does,
 *                   and the only one safe to expose on a public host.
 *
 * main.cpp picks one at compile time via TAK_USE_TLS, so switching is a
 * one-line change rather than a rewrite.
 */
#pragma once

#include <Arduino.h>

class TakTransport {
 public:
  virtual ~TakTransport() {}

  // One-time setup: load certificates, configure the socket. Call before connect().
  virtual bool begin() = 0;

  virtual bool connect(const char* host, uint16_t port) = 0;
  virtual bool connected() = 0;

  // Returns false if the write did not fully complete, which means the peer
  // is gone and the caller should reconnect.
  virtual bool send(const String& payload) = 0;

  // Non-blocking. Returns bytes read, 0 if none waiting.
  virtual int read(uint8_t* buf, size_t len) = 0;

  virtual void stop() = 0;

  // For logging.
  virtual const char* name() const = 0;
};
