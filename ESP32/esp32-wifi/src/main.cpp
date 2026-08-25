#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"

// NAPT (NAT) support differs by core version:
//   - arduino-esp32 3.x (IDF 5.x): esp_netif_napt_enable() + WiFi.AP.netif()
//   - arduino-esp32 2.x (IDF 4.4): lwip ip_napt_enable(), only if the core's
//     lwip was built with IP_NAPT (many 2.x builds ship it disabled).
#if ESP_ARDUINO_VERSION_MAJOR < 3
extern "C" {
#include "lwip/lwip_napt.h"
}
#endif

// ---------------------------------------------------------------------------
// Small serial helpers
// ---------------------------------------------------------------------------

// Read a whole line from Serial (blocking). Returns trimmed string.
static String readLine() {
  String line;
  while (true) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line.trim();
        return line;
      }
      line += c;
    }
    delay(5);
  }
}

// True if the user typed 'q' (to abort a running mode) - non-blocking.
static bool userWantsQuit() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'q' || c == 'Q') return true;
  }
  return false;
}

static const char* encryptionName(wifi_auth_mode_t enc) {
  switch (enc) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
    default:                        return "UNKNOWN";
  }
}

// ---------------------------------------------------------------------------
// Mode 1: WiFi scanner
// ---------------------------------------------------------------------------
static void modeScanner() {
  Serial.println();
  Serial.println(F("=== Mode 1: WiFi scanner (press 'q' + Enter to stop) ==="));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  while (true) {
    if (userWantsQuit()) break;

    Serial.println(F("Scanning..."));
    int n = WiFi.scanNetworks(/*async*/ false, /*show_hidden*/ true);
    if (n <= 0) {
      Serial.println(F("No networks found."));
    } else {
      Serial.printf("Found %d networks:\n", n);
      Serial.println(F(" #  RSSI  Ch  Encryption      BSSID              SSID"));
      Serial.println(F("--- ----- --- --------------- ------------------ ------------------------"));
      for (int i = 0; i < n; i++) {
        Serial.printf("%2d  %4d  %2d  %-15s %-18s %s\n",
                      i + 1,
                      WiFi.RSSI(i),
                      WiFi.channel(i),
                      encryptionName(WiFi.encryptionType(i)),
                      WiFi.BSSIDstr(i).c_str(),
                      WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>");
      }
    }
    WiFi.scanDelete();
    Serial.println();

    // Wait ~4s before next scan, but stay responsive to 'q'.
    for (int t = 0; t < 40; t++) {
      if (userWantsQuit()) { Serial.println(F("Stopping scanner.")); return; }
      delay(100);
    }
  }
  Serial.println(F("Stopping scanner."));
}

// ---------------------------------------------------------------------------
// Mode 2: WiFi repeater (STA + AP with NAPT)
// ---------------------------------------------------------------------------
static void modeRepeater() {
  Serial.println();
  Serial.println(F("=== Mode 2: WiFi repeater ==="));

  // Convenience defaults for the local test network (press Enter to accept).
  const char* kDefaultSsid = "BBC";
  const char* kDefaultPass = "liza2017";

  Serial.printf("Upstream WiFi SSID to join [%s]: ", kDefaultSsid);
  String upSsid = readLine();
  if (upSsid.isEmpty()) upSsid = kDefaultSsid;
  Serial.println(upSsid);

  Serial.print(F("Upstream WiFi password [default for above SSID]: "));
  String upPass = readLine();
  if (upPass.isEmpty()) upPass = kDefaultPass;
  Serial.println(F("(hidden)"));

  Serial.print(F("New AP (repeater) SSID [ESP32-Repeater]: "));
  String apSsid = readLine();
  if (apSsid.isEmpty()) apSsid = "ESP32-Repeater";
  Serial.print(F("New AP password (>=8 chars, blank = open): "));
  String apPass = readLine();
  Serial.println(F("(hidden)"));

  WiFi.mode(WIFI_AP_STA);

  // Join the upstream network.
  Serial.printf("Joining '%s' ...\n", upSsid.c_str());
  WiFi.begin(upSsid.c_str(), upPass.isEmpty() ? nullptr : upPass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print('.');
    if (userWantsQuit()) { Serial.println(F("\nAborted.")); WiFi.disconnect(true); return; }
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Failed to connect to upstream network. Aborting."));
    WiFi.disconnect(true);
    return;
  }
  Serial.printf("Connected. STA IP: %s  gateway: %s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());

  // Start our own AP.
  bool apOk = WiFi.softAP(apSsid.c_str(), apPass.length() >= 8 ? apPass.c_str() : nullptr);
  if (!apOk) { Serial.println(F("softAP() failed. Aborting.")); return; }
  Serial.printf("AP '%s' up. AP IP: %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());

  // Enable NAPT (NAT) on the AP interface so clients get internet through STA.
  bool napt = false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // Core 3.x: use the esp_netif NAPT API on the AP interface.
  esp_netif_t* apNetif = WiFi.AP.netif();
  if (apNetif != nullptr) {
    napt = (esp_netif_napt_enable(apNetif) == ESP_OK);
  }
#elif defined(IP_NAPT) && IP_NAPT
  // Core 2.x with NAPT compiled into lwip: enable on the AP IP.
  napt = (ip_napt_enable((uint32_t)WiFi.softAPIP(), 1) == ERR_OK);
#endif

  if (napt) {
    Serial.println(F("NAPT enabled - clients on the repeater AP now route to the upstream network."));
  } else {
    Serial.println(F("NOTE: NAT/NAPT is not available in this build, so clients can"));
    Serial.println(F("      associate to the repeater AP but won't reach the internet."));
    Serial.println(F("      For a true routing repeater, build against arduino-esp32 3.x"));
    Serial.println(F("      (pioarduino platform), which ships NAPT enabled."));
  }

  Serial.println(F("Repeater running. Press 'q' + Enter to stop."));
  while (true) {
    if (userWantsQuit()) break;
    delay(200);
  }

  Serial.println(F("Stopping repeater."));
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
}

// ---------------------------------------------------------------------------
// Mode 3: broadcast 100 fake APs test1..test100 via raw beacon frames
// ---------------------------------------------------------------------------

// Beacon frame template. SSID and rate/DS tags are appended at runtime.
static uint8_t beaconTemplate[] = {
  /* 0  */ 0x80, 0x00,                         // Frame Control: beacon
  /* 2  */ 0x00, 0x00,                         // Duration
  /* 4  */ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Destination: broadcast
  /* 10 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (BSSID) - filled per SSID
  /* 16 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID          - filled per SSID
  /* 22 */ 0x00, 0x00,                         // Seq/frag
  /* 24 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
  /* 32 */ 0x64, 0x00,                         // Beacon interval (0x64 = 100 TU)
  /* 34 */ 0x31, 0x04,                         // Capability info (ESS)
  /* 36 */ 0x00, 0x00                          // SSID tag: id=0, len=0 (len filled in)
};

static void sendBeacon(const String& ssid, uint8_t channel, uint8_t idx) {
  uint8_t packet[200];
  const size_t headLen = sizeof(beaconTemplate); // through SSID tag header
  memcpy(packet, beaconTemplate, headLen);

  // Unique, locally-administered BSSID/source per SSID index.
  uint8_t mac[6] = { 0x02, 0xDE, 0xAD, 0x00, 0x00, idx };
  memcpy(&packet[10], mac, 6);
  memcpy(&packet[16], mac, 6);

  // SSID tag length + bytes.
  uint8_t ssidLen = (uint8_t)ssid.length();
  packet[37] = ssidLen;
  size_t pos = headLen;
  memcpy(&packet[pos], ssid.c_str(), ssidLen);
  pos += ssidLen;

  // Supported rates tag (id=1).
  static const uint8_t rates[] = { 0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
  memcpy(&packet[pos], rates, sizeof(rates));
  pos += sizeof(rates);

  // DS Parameter set tag (id=3): current channel.
  packet[pos++] = 0x03;
  packet[pos++] = 0x01;
  packet[pos++] = channel;

  // RSN information element (tag 48): advertise WPA2-PSK / CCMP so scanners show
  // these as password-protected (secured) networks. The actual passphrase is
  // only used during the 4-way handshake at association time, which raw beacon
  // injection does not perform - so these appear secured but are not joinable.
  static const uint8_t rsn[] = {
    0x30, 0x14,             // tag 48 (RSN), length 20
    0x01, 0x00,             // RSN version 1
    0x00, 0x0F, 0xAC, 0x04, // group cipher: CCMP (AES)
    0x01, 0x00,             // pairwise cipher count = 1
    0x00, 0x0F, 0xAC, 0x04, // pairwise cipher: CCMP (AES)
    0x01, 0x00,             // AKM count = 1
    0x00, 0x0F, 0xAC, 0x02, // AKM: PSK
    0x00, 0x00              // RSN capabilities
  };
  memcpy(&packet[pos], rsn, sizeof(rsn));
  pos += sizeof(rsn);

  esp_wifi_80211_tx(WIFI_IF_AP, packet, pos, false);
}

// Each SSID is one word of Ukraine's anthem (official first verse + chorus),
// prefixed with a zero-padded index so a WiFi scanner sorted by name shows the
// words in the correct reading order. UTF-8 Cyrillic is displayed by most
// modern phones/laptops.
static const char* const kAnthemSsids[] = {
  "01 Ще", "02 не", "03 вмерла", "04 України", "05 і", "06 слава,", "07 і",
  "08 воля,", "09 Ще", "10 нам,", "11 браття", "12 молодії,", "13 усміхнеться",
  "14 доля.", "15 Згинуть", "16 наші", "17 воріженьки,", "18 як", "19 роса",
  "20 на", "21 сонці.", "22 Запануєм", "23 і", "24 ми,", "25 браття,", "26 у",
  "27 своїй", "28 сторонці.", "29 Душу", "30 й", "31 тіло", "32 ми",
  "33 положим", "34 за", "35 нашу", "36 свободу,", "37 І", "38 покажем,",
  "39 що", "40 ми,", "41 браття,", "42 козацького", "43 роду."
};
static const int kAnthemCount = sizeof(kAnthemSsids) / sizeof(kAnthemSsids[0]);

static void modeAnthemBeacons() {
  Serial.println();
  Serial.printf("=== Mode 3: broadcasting %d APs - anthem of Ukraine (press 'q' + Enter to stop) ===\n",
                kAnthemCount);
  Serial.println(F("Sort your WiFi list by NAME to read the anthem in order (01, 02, 03, ...)."));

  // Bring the radio up as an AP, then inject a beacon per anthem word.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kAnthemSsids[0]);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  Serial.println(F("Radio up. Injecting beacon frames..."));

  const uint8_t channels[] = { 1, 6, 11 };
  uint8_t chIndex = 0;
  unsigned long lastReport = 0;

  while (true) {
    if (userWantsQuit()) break;

    uint8_t ch = channels[chIndex];
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    for (int i = 0; i < kAnthemCount; i++) {
      sendBeacon(String(kAnthemSsids[i]), ch, (uint8_t)(i + 1));
      delayMicroseconds(500);
    }
    chIndex = (chIndex + 1) % (sizeof(channels));

    if (millis() - lastReport > 3000) {
      Serial.printf("Broadcasting %d anthem SSIDs (cycling channels 1/6/11, now ch %d)...\n",
                    kAnthemCount, ch);
      lastReport = millis();
    }
    delay(10);
  }

  Serial.println(F("Stopping beacon broadcast."));
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

// ---------------------------------------------------------------------------
// Mode 4: promiscuous sniffer (monitor mode)
// ---------------------------------------------------------------------------
//
// Listens to raw 802.11 frames on the air (no association needed) while hopping
// channels 1..13. Counts every frame by class and prints the interesting ones:
//   - EAPOL frames = WPA 4-way handshake (capture these + the PSK to later
//     decrypt that device's traffic in Wireshark).
//   - Deauth / disassoc (often a sign of an attack or a roaming event).
//   - Probe requests (devices searching for known networks).
// Data payloads on WPA2/WPA3 are encrypted, so this shows frame metadata, not
// plaintext contents. Press 'q' + Enter to stop.

static volatile uint32_t gCntMgmt = 0, gCntCtrl = 0, gCntData = 0;
static volatile uint32_t gCntEapol = 0, gCntDeauth = 0, gCntProbeReq = 0;
static volatile bool gSniffing = false;
static volatile bool gShowData = false;   // print per-data-frame addresses

static void macToStr(const uint8_t* m, char* out /*>=18*/) {
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

// Is this data frame an unencrypted EAPOL (WPA handshake) frame?
static bool isEapol(const uint8_t* p, int len) {
  uint8_t fc0 = p[0];
  uint8_t type = (fc0 >> 2) & 0x3;
  uint8_t subtype = (fc0 >> 4) & 0xF;
  if (type != 2) return false;            // must be a data frame
  int hdr = 24;
  if (subtype & 0x08) hdr += 2;           // QoS data -> +2 bytes
  if ((p[1] & 0x03) == 0x03) hdr += 6;    // ToDS+FromDS -> addr4 present
  if (len < hdr + 8) return false;
  const uint8_t* llc = p + hdr;           // LLC/SNAP: AA AA 03 00 00 00 <ethertype>
  return (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
          llc[6] == 0x88 && llc[7] == 0x8E);   // ethertype 0x888E = EAPOL
}

static void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t /*type*/) {
  if (!gSniffing) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;
  int rssi = pkt->rx_ctrl.rssi;
  int ch = pkt->rx_ctrl.channel;

  uint8_t fc0 = p[0];
  uint8_t ftype = (fc0 >> 2) & 0x3;
  uint8_t fsub = (fc0 >> 4) & 0xF;

  char dst[18], src[18];
  macToStr(p + 4, dst);   // addr1 = receiver / destination
  macToStr(p + 10, src);  // addr2 = transmitter / source

  switch (ftype) {
    case 0: gCntMgmt++; break;
    case 1: gCntCtrl++; break;
    case 2: gCntData++; break;
  }

  if (isEapol(p, len)) {
    gCntEapol++;
    Serial.printf("[EAPOL handshake] ch=%2d rssi=%4d src=%s dst=%s\n", ch, rssi, src, dst);
  } else if (ftype == 0 && (fsub == 0x0C || fsub == 0x0A)) { // deauth / disassoc
    gCntDeauth++;
    Serial.printf("[%s]     ch=%2d rssi=%4d src=%s dst=%s\n",
                  fsub == 0x0C ? "DEAUTH  " : "DISASSOC", ch, rssi, src, dst);
  } else if (ftype == 0 && fsub == 0x04) { // probe request
    gCntProbeReq++;
    Serial.printf("[PROBE-REQ]       ch=%2d rssi=%4d src=%s\n", ch, rssi, src);
  } else if (ftype == 2 && gShowData) { // data frame: show who talks to whom
    char bssid[18];
    macToStr(p + 16, bssid);            // addr3 = BSSID (the AP)
    Serial.printf("[DATA] ch=%2d rssi=%4d ta=%s ra=%s bssid=%s len=%d\n",
                  ch, rssi, src, dst, bssid, len);
  }
}

static void modeSniffer() {
  Serial.println();
  Serial.println(F("=== Mode 4: promiscuous sniffer (press 'q' + Enter to stop) ==="));

  // Channel lock: parking on the target's channel is required to reliably catch
  // the fast WPA 4-way handshake (hopping misses it ~98% of the time).
  Serial.print(F("Channel to lock 1-13 (0 = hop all channels): "));
  String chStr = readLine();
  Serial.println(chStr);
  int lockChannel = chStr.toInt();
  if (lockChannel < 1 || lockChannel > 13) lockChannel = 0;
  Serial.print(F("Show data-frame addresses too (who talks to whom)? (y/n): "));
  String dataStr = readLine();
  Serial.println(dataStr);
  gShowData = (dataStr == "y" || dataStr == "Y");

  if (lockChannel == 0)
    Serial.println(F("Hopping channels 1..13. Highlighting EAPOL / deauth / probe-req."));
  else
    Serial.printf("Locked to channel %d. Highlighting EAPOL / deauth / probe-req.\n", lockChannel);
  if (gShowData)
    Serial.println(F("[DATA] lines show ta=transmitter ra=receiver bssid=AP (payload is encrypted)."));

  gCntMgmt = gCntCtrl = gCntData = 0;
  gCntEapol = gCntDeauth = gCntProbeReq = 0;

  // Bring the radio up in STA mode but do NOT power it off (disconnect(true)
  // would kill the radio and the sniffer would capture nothing).
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(100);

  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL };
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  gSniffing = true;
  esp_wifi_set_promiscuous(true);

  uint8_t ch = (lockChannel == 0) ? 1 : (uint8_t)lockChannel;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  unsigned long lastHop = 0, lastReport = 0;

  while (true) {
    if (userWantsQuit()) break;

    unsigned long now = millis();
    if (lockChannel == 0 && now - lastHop > 250) {
      ch = (ch % 13) + 1;
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      lastHop = now;
    }
    if (now - lastReport > 1000) {
      Serial.printf("-- totals: mgmt=%lu data=%lu ctrl=%lu | EAPOL=%lu deauth=%lu probeReq=%lu (ch=%d)\n",
                    (unsigned long)gCntMgmt, (unsigned long)gCntData, (unsigned long)gCntCtrl,
                    (unsigned long)gCntEapol, (unsigned long)gCntDeauth, (unsigned long)gCntProbeReq, ch);
      lastReport = now;
    }
    delay(10);
  }

  gSniffing = false;
  esp_wifi_set_promiscuous(false);
  Serial.println(F("Stopping sniffer."));
  WiFi.mode(WIFI_STA);
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
static void printMenu() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("        ESP32 WiFi Toolbox menu"));
  Serial.println(F("========================================"));
  Serial.println(F("  1) Scan all WiFi networks in range"));
  Serial.println(F("  2) WiFi repeater (join + rebroadcast)"));
  Serial.println(F("  3) Broadcast anthem-of-Ukraine APs"));
  Serial.println(F("  4) Promiscuous sniffer (monitor mode)"));
  Serial.println(F("----------------------------------------"));
  Serial.print(F("Choose 1, 2, 3 or 4 and press Enter [default: 3]: "));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("ESP32 WiFi Toolbox ready."));

  // Auto-start Mode 3 on boot (no key press needed). Press 'q' + Enter to stop
  // it and drop into the menu.
  modeAnthemBeacons();
}

void loop() {
  printMenu();
  String choice = readLine();
  if (choice.isEmpty()) choice = "3";   // Enter with no input runs Mode 3
  Serial.println(choice);

  if (choice == "1") {
    modeScanner();
  } else if (choice == "2") {
    modeRepeater();
  } else if (choice == "3") {
    modeAnthemBeacons();
  } else if (choice == "4") {
    modeSniffer();
  } else {
    Serial.println(F("Invalid choice. Please type 1, 2, 3 or 4."));
  }
}
