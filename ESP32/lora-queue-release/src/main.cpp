#include <Arduino.h>
#include <esp_task_wdt.h>

#include "app/safe_mode.h"
#include "app/shell.h"
#include "core/calib.h"
#include "core/config.h"
#include "core/log.h"
#include "core/post.h"
#include "core/version.h"
#include "hal/battery.h"
#include "net/netcfg.h"
#include "hal/board_pins.h"
#include "net/net_task.h"
#include "net/ota.h"
#include "tasks/button_task.h"
#include "tasks/radio_task.h"
#include "tasks/tone.h"
#include "tasks/ui_task.h"

// Cores: the button task shares the Arduino core with loop(); the radio and UI
// tasks run on the other one, so drawing or a beep never delays key sampling.
constexpr BaseType_t KEY_CORE = 1;
constexpr BaseType_t WORK_CORE = 0;

// Long enough that a slow OLED redraw or a full ACK retry cycle never trips
// it, short enough that a wedged task becomes a reboot rather than a node that
// has quietly stopped answering.
constexpr uint32_t WDT_TIMEOUT_S = 8;

constexpr uint32_t SPLASH_MS = 2500;

// The order here is the dependency order: the log exists before anything can
// report, the config before anything is configured by it, and the POST before
// anything acts on its result.
//
// Note what is missing compared to the demo this grew from: there is no halt()
// any more. A node that stops tidily on a failed subsystem is a node somebody
// has to drive to. A node that comes up degraded can still be asked what is
// wrong with it.
void setup() {
  Serial.begin(115200);
  delay(50);  // let the USB-serial bridge come up before the first line

  logBegin();
  safeModeBegin();

  // Before anything else asks a question about this image: is it one that has
  // just been installed over the air and has not yet proved itself? Everything
  // downstream - what the screen says, whether an update is allowed, whether
  // this boot ends in a confirmation or a revert - depends on the answer.
  otaBegin();

  if (fwIsDirty()) LOG_W(TAG_SYS, E_FW_DIRTY, 0);

  calibBegin();
  batteryBegin();

  // False means nothing valid was in NVS. The node still starts, on the
  // firmware's defaults, and the POST carries the fact - so the operator knows
  // the settings were lost rather than simply never changed.
  const bool cfgOk = configBegin();
  postObserveNvs(cfgOk);

  // Reads the four network credentials out of NVS. No association happens
  // here: joining a network can take twenty seconds, and the point of this
  // stretch of setup() is to reach the POST and the splash quickly.
  netBegin();

  const bool displayOk = uiBegin();
  postObserveDisplay(displayOk);

  // Three attempts, because an SPI or supply glitch at boot is transient far
  // more often than a radio is actually dead.
  const bool radioOk = radioBeginWithRetries(3);
  postObserveRadio(radioOk);

  const bool toneOk = toneBegin();

  const uint16_t mask = postRun();

  // The first thing the screen ever shows is what this node is and whether it
  // passed. Drawn from setup() so it is on the glass before any task runs.
  if (displayOk) {
    uiDrawSplash();
    delay(SPLASH_MS);
  }

  // Every task start is checked, and a failure is a logged bit rather than a
  // halt: a node that can still answer `version` is worth far more than one
  // that stopped cleanly.
  if (!shellTaskStart(1, WORK_CORE)) LOG_E(TAG_SYS, E_TASK_START_FAIL, 0);

  // The uplink starts in safe mode too, and that is deliberate: a node that
  // keeps falling over is exactly the one that has to be reachable, and a new
  // image delivered over WiFi is the only cure that does not involve a drive.
  // It is also the task that decides whether an image on trial keeps its
  // place, so it has to run before anything else can matter.
  if (!netTaskStart(1, WORK_CORE)) LOG_E(TAG_SYS, E_TASK_START_FAIL, 5);
  if (displayOk && !uiTaskStart(1, WORK_CORE)) LOG_E(TAG_SYS, E_TASK_START_FAIL, 1);
  if (toneOk && !toneTaskStart(1, WORK_CORE)) LOG_E(TAG_SYS, E_TASK_START_FAIL, 2);

  // In safe mode the parts that can crash stay off. What remains is exactly
  // what is needed to diagnose and cure the node from a distance: the shell,
  // the radio and its telemetry.
  if (safeModeActive()) {
    uiPostBanner("SAFE MODE");
  } else {
    if (radioOk && !radioTaskStart(2, WORK_CORE)) LOG_E(TAG_SYS, E_TASK_START_FAIL, 3);
    if (!buttonTaskStart(3, KEY_CORE)) LOG_E(TAG_SYS, E_TASK_START_FAIL, 4);
  }

  if (otaIsOnTrial())
    uiPostBanner("NEW IMAGE ON TRIAL");
  else if (!radioOk)
    uiPostBanner("RADIO FAIL");
  else if (mask)
    uiPostBanner("POST FAIL");
  else
    uiPostBanner("1 tap=.  2 taps=-");

  esp_task_wdt_init(WDT_TIMEOUT_S, /*panic=*/true);
  esp_task_wdt_add(nullptr);  // the Arduino loop task
  LOG_I(TAG_SYS, E_WDT_SUBSCRIBED, WDT_TIMEOUT_S);

  Serial.printf("\n%s %s  node %u  serial %s  post 0x%04X\n", fwVersionString(),
                FW_BUILD_TYPE, config().node_id, calibSerial(), mask);

  if (otaIsOnTrial())
    Serial.printf(
        "THIS IMAGE IS ON TRIAL. It reverts to the previous one in %lu s unless\n"
        "it passes its POST and checks in with the fleet service. `ota confirm`\n"
        "keeps it now; `ota` shows the state.\n",
        (unsigned long)otaTrialSecondsLeft());
  shellPrintBanner(Serial);
  Serial.print("> ");
}

// The dispatcher: block on the button queue, translate the press type into a
// Morse symbol, hand it to the radio task. Nothing else belongs here.
void loop() {
  KeyEvent event;

  // The one-second timeout is what feeds the watchdog and runs the
  // housekeeping. Blocking forever would make a node with nobody pressing
  // anything look wedged to the watchdog.
  if (!buttonWaitForPress(event, pdMS_TO_TICKS(1000))) {
    esp_task_wdt_reset();
    safeModeTick();
    return;
  }
  esp_task_wdt_reset();

  // A long hold is not a Morse symbol: it is how the identity page is reached
  // in the field, where there is no laptop to type `screen info` into.
  if (event.press == KeyPress::Long) {
    uiToggleInfoPage();
    return;
  }

  const char symbol = (event.press == KeyPress::Double) ? '-' : '.';
  radioSendSymbol(symbol, event.stamp, pdMS_TO_TICKS(50));
}
