#include "app/shell.h"

#include "app/safe_mode.h"
#include "core/calib.h"
#include "core/config.h"
#include "core/health.h"
#include "core/log.h"
#include "core/post.h"
#include "core/version.h"
#include "hal/battery.h"
#include "tasks/radio_task.h"
#include "tasks/ui_task.h"

static constexpr size_t SHELL_LINE_MAX = 96;
static constexpr uint8_t SHELL_ARG_MAX = 4;

static char s_line[SHELL_LINE_MAX];
static size_t s_len = 0;

// --- helpers --------------------------------------------------------------

static void printUptime(Print &out) {
  const uint32_t s = millis() / 1000u;
  out.printf("%lud %02lu:%02lu:%02lu", (unsigned long)(s / 86400u),
             (unsigned long)((s / 3600u) % 24u), (unsigned long)((s / 60u) % 60u),
             (unsigned long)(s % 60u));
}

static bool parseU32(const char *text, uint32_t &out) {
  if (!text || !*text) return false;
  char *end = nullptr;
  const unsigned long v = strtoul(text, &end, 0); // 0 accepts 0x for sync_word
  if (end == text || *end != '\0') return false;
  out = (uint32_t)v;
  return true;
}

// --- commands -------------------------------------------------------------

// The first thing anyone asks a device is what it is. Everything needed to
// start a diagnosis is on this one screen, and the same facts go out over the
// air as a health frame - same data, different wrapper.
static void cmdVersion(Print &out) {
  uint16_t migratedFrom = 0;
  const bool migrated = configWasMigrated(migratedFrom);

  out.printf("fw      %s %s\n", fwVersionString(), FW_BUILD_TYPE);
  out.printf("built   %s\n", FW_BUILD_UTC);
  out.printf("hw      %s\n", FW_HARDWARE);
  out.printf("proto   %u\n", PROTO_VERSION);
  out.printf("serial  %s%s\n", calibSerial(),
             calibIsProvisioned() ? "" : " (from MAC, not provisioned)");
  out.printf("node    %u\n", config().node_id);

  out.print("uptime  ");
  printUptime(out);
  out.println();

  out.printf("reboots %lu   last: %s", (unsigned long)totalBootCount(),
             lastResetReasonName());
  if (abnormalBootCount()) out.printf("   abnormal streak %u", abnormalBootCount());
  if (safeModeActive()) out.print("   SAFE MODE");
  out.println();

  out.printf("post    0x%04X %s\n", postMask(), postMask() ? "FAIL" : "OK");
  out.printf("cfg     v%u  seq %lu  slot %s", config().cfg_version,
             (unsigned long)configSeq(), cfg_slot_name(configSlot()));
  if (migrated) out.printf("  (migrated from v%u)", migratedFrom);
  out.println();

  if (fwIsDirty())
    out.println("WARNING this image was built with uncommitted changes and must "
                "not be deployed");
}

// The whole state in one dump, because this is what gets pasted into a ticket.
// The bounds are printed alongside every value: they live in the device, and
// an operator should not have to go and look them up.
static void cmdConfigGet(Print &out) {
  for (size_t i = 0; i < CFG_FIELD_COUNT; i++) {
    const cfg_field_t *f = &CFG_FIELDS[i];
    const uint32_t v = config_field_get(&config(), f);
    out.printf("%-16s %10lu %-4s [%lu..%lu]%s\n", f->name, (unsigned long)v,
               f->unit, (unsigned long)f->min, (unsigned long)f->max,
               f->applies_live ? "" : " (reboot)");
  }
  out.printf("%-16s %10u\n", "cfg_version", config().cfg_version);
  out.printf("%-16s %10lu  slot %s\n", "cfg_seq", (unsigned long)configSeq(),
             cfg_slot_name(configSlot()));
}

static void cmdConfigSet(Print &out, const char *key, const char *valueText) {
  uint32_t value = 0;
  if (!parseU32(valueText, value)) {
    out.printf("ERR parse: '%s' is not a number\n", valueText ? valueText : "");
    return;
  }

  const cfg_field_t *f = config_field_by_name(key);
  uint32_t oldValue = 0;
  const cfg_set_result_t r = configSet(key, value, oldValue);

  switch (r) {
    case CFG_SET_OK:
      out.printf("OK  %s %lu -> %lu  (saved, %s)\n", key, (unsigned long)oldValue,
                 (unsigned long)value,
                 f && f->applies_live ? "applied" : "applies after reboot");
      uiRefresh();
      break;

    case CFG_SET_UNKNOWN_FIELD:
      out.printf("ERR unknown field: %s  (try `config get`)\n", key);
      break;

    case CFG_SET_OUT_OF_RANGE:
      // The device explains its own refusal. A dashboard can be older than the
      // firmware and not know the current limits, so it cannot do this.
      if (f)
        out.printf("ERR range: %s must be %lu..%lu\n", f->name,
                   (unsigned long)f->min, (unsigned long)f->max);
      else
        out.printf("ERR range: %s rejected\n", key);
      break;

    case CFG_SET_LOW_POWER:
      out.printf("ERR power: vbat %u mV < %u mV, refusing flash write\n",
                 configLastRefusedMillivolts(), config().vbat_min_mv);
      out.println("    writing flash is the hungriest thing this node does; on a "
                  "flat pack it is how a device stops coming back");
      break;

    case CFG_SET_WRITE_FAILED:
      out.println("ERR nvs: write failed, config unchanged");
      break;
  }
}

static void cmdConfigReset(Print &out) {
  const cfg_set_result_t r = configReset();
  if (r == CFG_SET_OK) {
    out.printf("OK  defaults of fw %s; node_id, serial and calibration kept\n",
               FW_SEMVER);
    uiRefresh();
  } else {
    out.printf("ERR %s\n", configSetResultText(r));
  }
}

static void cmdConfig(Print &out, char **argv, uint8_t argc) {
  if (argc < 2) {
    out.println("usage: config get | set <key> <value> | reset");
    return;
  }

  if (strcmp(argv[1], "get") == 0) {
    cmdConfigGet(out);
  } else if (strcmp(argv[1], "set") == 0) {
    if (argc < 4) {
      out.println("usage: config set <key> <value>");
      return;
    }
    cmdConfigSet(out, argv[2], argv[3]);
  } else if (strcmp(argv[1], "reset") == 0) {
    cmdConfigReset(out);
#if BUILD_TEST_COMMANDS
  } else if (strcmp(argv[1], "seed_v1") == 0) {
    // Writes a v1 record so the migration can be demonstrated on real
    // hardware. Not compiled into the field image.
    if (configSeedV1()) {
      out.println("OK  wrote a synthetic cfg v1 record (tx_power 11, period_s 120)");
      out.println("    reboot to watch it migrate 1 -> 2");
    } else {
      out.println("ERR could not write the v1 record");
    }
#endif
  } else {
    out.printf("ERR unknown subcommand: %s\n", argv[1]);
  }
}

static void cmdLog(Print &out, char **argv, uint8_t argc) {
  if (argc < 2) {
    out.println("usage: log dump [n] | log level [0..5] | log clear");
    return;
  }

  if (strcmp(argv[1], "dump") == 0) {
    uint32_t n = 0;
    if (argc >= 3 && !parseU32(argv[2], n)) n = 0;
    logDump(out, (uint16_t)n);
  } else if (strcmp(argv[1], "level") == 0) {
    if (argc < 3) {
      out.printf("log level %u (%s), compiled up to %u\n", logGetLevel(),
                 logLevelName(logGetLevel()), LOG_COMPILE_LEVEL);
      return;
    }
    // Routed through the config so it is persisted: the level survives a
    // reboot, and one node can be left verbose without a reflash.
    cmdConfigSet(out, "log_level", argv[2]);
  } else if (strcmp(argv[1], "clear") == 0) {
    logClear();
    out.println("OK  ring cleared");
  } else {
    out.printf("ERR unknown subcommand: %s\n", argv[1]);
  }
}

static void cmdSelfTest(Print &out) {
  const uint16_t mask = postRun();
  postPrint(out);
  if (mask) out.printf("critical failures: 0x%04X\n", postCriticalMask());
  uiRefresh();
}

static void cmdScreen(Print &out, char **argv, uint8_t argc) {
  if (argc >= 2 && strcmp(argv[1], "info") == 0) {
    uiShowInfoPage();
    out.println("OK  info page");
  } else if (argc >= 2 && strcmp(argv[1], "main") == 0) {
    uiShowMainPage();
    out.println("OK  main page");
  } else {
    out.println("usage: screen info | main");
  }
}

void shellPrintBanner(Print &out) {
  out.println();
  out.println("commands");
  out.println("  version              build, serial, uptime, reboots, POST, cfg");
  out.println("  self-test            re-run the POST and print the mask");
  out.println("  health               the 12 bytes that go out as telemetry");
  out.println("  health send          transmit one now instead of waiting");
  out.println("  radio                link settings and counters");
  out.println("  frames               last TX and RX frame in hex, decoded");
  out.println("  log dump [n]         newest n records from the ring");
  out.println("  log level [0..5]     runtime threshold (persisted)");
  out.println("  log clear");
  out.println("  config get           every setting with its bounds");
  out.println("  config set <k> <v>   validated, then saved, then applied");
  out.println("  config reset         firmware defaults; keeps id and calibration");
#if BUILD_TEST_COMMANDS
  out.println("  config seed_v1       [test] write a v1 record to show migration");
  out.println("  crash                [test] force a panic to exercise the handler");
#endif
#if BUILD_PROVISIONING
  out.println("  serial set <sn>      [factory] write the serial number");
  out.println("  calib vbat <q10>     [factory] battery divider correction");
#endif
  out.println("  screen info | main");
  out.println("  reboot");
  out.println();
}

// --- dispatch -------------------------------------------------------------

static void dispatch(Print &out, char *line) {
  char *argv[SHELL_ARG_MAX] = {nullptr};
  uint8_t argc = 0;

  for (char *tok = strtok(line, " \t"); tok && argc < SHELL_ARG_MAX;
       tok = strtok(nullptr, " \t"))
    argv[argc++] = tok;

  if (argc == 0) return;

  if (strcmp(argv[0], "version") == 0) {
    cmdVersion(out);
  } else if (strcmp(argv[0], "self-test") == 0 || strcmp(argv[0], "selftest") == 0) {
    cmdSelfTest(out);
  } else if (strcmp(argv[0], "health") == 0) {
    if (argc >= 2 && strcmp(argv[1], "send") == 0) {
      // Forces a check-in instead of waiting out health_period_s, which is how
      // an operator confirms the uplink without keying a symbol.
      out.println(radioRequestHealth() ? "OK  health frame queued"
                                       : "ERR radio task not running");
    } else {
      healthPrint(out);
    }
  } else if (strcmp(argv[0], "radio") == 0) {
    radioPrintStats(out);
  } else if (strcmp(argv[0], "frames") == 0) {
    radioPrintLastFrames(out);
  } else if (strcmp(argv[0], "log") == 0) {
    cmdLog(out, argv, argc);
  } else if (strcmp(argv[0], "config") == 0) {
    cmdConfig(out, argv, argc);
  } else if (strcmp(argv[0], "screen") == 0) {
    cmdScreen(out, argv, argc);
  } else if (strcmp(argv[0], "reboot") == 0) {
    out.println("OK  rebooting");
    out.flush();
    delay(50);
    esp_restart();
#if BUILD_TEST_COMMANDS
  } else if (strcmp(argv[0], "crash") == 0) {
    // A panic handler that has never fired does not work. This is how it gets
    // fired on purpose, on the bench, where watching it is free.
    out.println("OK  forcing a panic");
    out.flush();
    delay(50);
    volatile int *nowhere = (int *)0x00000000;
    *nowhere = 1;
#endif
#if BUILD_PROVISIONING
  } else if (strcmp(argv[0], "serial") == 0 && argc >= 3 &&
             strcmp(argv[1], "set") == 0) {
    out.println(calibSetSerial(argv[2]) ? "OK  serial written" : "ERR write failed");
  } else if (strcmp(argv[0], "calib") == 0 && argc >= 3 &&
             strcmp(argv[1], "vbat") == 0) {
    uint32_t q10 = 0;
    if (!parseU32(argv[2], q10)) {
      out.println("ERR parse");
    } else {
      out.println(calibSetVbatScale((uint16_t)q10) ? "OK  scale written"
                                                   : "ERR out of range (512..2048)");
    }
#endif
  } else if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
    shellPrintBanner(out);
  } else {
    out.printf("ERR unknown command: %s  (try `help`)\n", argv[0]);
  }
}

static void shellTask(void * /*arg*/) {
  for (;;) {
    while (Serial.available()) {
      const char c = (char)Serial.read();

      if (c == '\r') continue;
      if (c == '\n') {
        s_line[s_len] = '\0';
        if (s_len) dispatch(Serial, s_line);
        Serial.print("> ");
        s_len = 0;
        continue;
      }

      // An over-long line is truncated rather than dropped: the operator gets
      // an error naming a mangled command instead of silence.
      if (s_len < SHELL_LINE_MAX - 1) s_line[s_len++] = c;
    }

    // Every command answers. Polling at 20 ms keeps the shell responsive
    // without spending a core on it.
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool shellTaskStart(UBaseType_t priority, BaseType_t core) {
  return xTaskCreatePinnedToCore(shellTask, "shell", 4096, nullptr, priority,
                                 nullptr, core) == pdPASS;
}
