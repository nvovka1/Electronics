#include "CotEvent.h"

#include <sys/time.h>

#include "config.h"

namespace {

// Anything past 2023 means NTP has run. The ESP32 boots at epoch 0.
constexpr int64_t SANE_EPOCH_MS = 1700000000LL * 1000LL;

void appendDouble(String& out, double value, uint8_t decimals) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", (int)decimals, value);
  out += buf;
}

}  // namespace

const char* cot::deviceUid() {
  static char uid[24] = {0};
  if (uid[0] == '\0') {
    // Only stability and uniqueness matter here, not matching the byte order
    // printed on the board's label.
    uint64_t mac = ESP.getEfuseMac();
    snprintf(uid, sizeof(uid), "ESP32-%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
  }
  return uid;
}

int64_t cot::nowMillis() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

bool cot::clockIsSane() {
  return nowMillis() > SANE_EPOCH_MS;
}

String cot::isoTimestamp(int64_t epochMillis) {
  time_t seconds = (time_t)(epochMillis / 1000);
  int millisPart = (int)(epochMillis % 1000);

  struct tm utc;
  gmtime_r(&seconds, &utc);

  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
           utc.tm_hour, utc.tm_min, utc.tm_sec, millisPart);
  return String(buf);
}

String cot::buildPli(const GeoPosition& pos, uint32_t staleSeconds) {
  const int64_t now = nowMillis();
  const String timeStr  = isoTimestamp(now);
  const String staleStr = isoTimestamp(now + (int64_t)staleSeconds * 1000LL);

  String xml;
  xml.reserve(900);

  // ATAK emits the declaration ahead of every event on a stream, so the server
  // is happy to see it repeated. The stream is a sequence of documents rather
  // than one document.
  xml += F("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  xml += F("<event version=\"2.0\" uid=\"");
  xml += deviceUid();
  xml += F("\" type=\"" TAK_COT_TYPE "\" how=\"m-g\" time=\"");
  xml += timeStr;
  xml += F("\" start=\"");
  xml += timeStr;
  xml += F("\" stale=\"");
  xml += staleStr;
  xml += F("\">");

  xml += F("<point lat=\"");
  appendDouble(xml, pos.lat, 7);
  xml += F("\" lon=\"");
  appendDouble(xml, pos.lon, 7);
  xml += F("\" hae=\"");
  appendDouble(xml, pos.hae, 1);
  xml += F("\" ce=\"");
  appendDouble(xml, pos.ce, 1);
  xml += F("\" le=\"");
  appendDouble(xml, pos.le, 1);
  xml += F("\"/>");

  xml += F("<detail>");
  xml += F("<takv device=\"" TAK_DEVICE "\" platform=\"" TAK_PLATFORM
           "\" os=\"" TAK_OS "\" version=\"" TAK_VERSION "\"/>");

  // endpoint "*:-1:stcp" tells the server to answer over the connection we
  // already hold open rather than trying to dial back to us.
  xml += F("<contact callsign=\"" TAK_CALLSIGN "\" endpoint=\"*:-1:stcp\"/>");
  xml += F("<uid Droid=\"" TAK_CALLSIGN "\"/>");
  xml += F("<__group name=\"" TAK_TEAM "\" role=\"" TAK_ROLE "\"/>");

  // Battery is reported as a constant. The T3 v1.6 has a divider on GPIO 35,
  // but its ratio varies between board revisions, so reading it would report
  // confidently wrong numbers on the wrong revision.
  xml += F("<status battery=\"100\"/>");

  xml += F("<track speed=\"");
  appendDouble(xml, pos.speed, 2);
  xml += F("\" course=\"");
  appendDouble(xml, pos.course, 2);
  xml += F("\"/>");

  xml += F("<precisionlocation altsrc=\"GPS\" geopointsrc=\"GPS\"/>");
  xml += F("</detail>");
  xml += F("</event>");

  return xml;
}

String cot::buildPing() {
  const int64_t now = nowMillis();
  const String timeStr  = isoTimestamp(now);
  const String staleStr = isoTimestamp(now + 20000LL);

  String xml;
  xml.reserve(400);

  xml += F("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
  xml += F("<event version=\"2.0\" uid=\"");
  xml += deviceUid();
  xml += F("-ping\" type=\"t-x-c-t\" how=\"h-g-i-g-o\" time=\"");
  xml += timeStr;
  xml += F("\" start=\"");
  xml += timeStr;
  xml += F("\" stale=\"");
  xml += staleStr;
  xml += F("\">");
  xml += F("<point lat=\"0.0\" lon=\"0.0\" hae=\"0.0\" ce=\"9999999.0\" le=\"9999999.0\"/>");
  xml += F("<detail/>");
  xml += F("</event>");

  return xml;
}
