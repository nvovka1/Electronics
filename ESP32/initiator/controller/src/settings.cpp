#include "settings.h"

#include <Preferences.h>

#include "config.h"
#include "log.h"

Settings settings;

namespace {

constexpr const char *NodeNamespace = "ctrl";

Preferences prefs;

// The highest counter this controller may hand out before it has to claim
// another block from NVS.
uint32_t counterCeiling = 0;
uint32_t counter = 0;

// Claims the next block and writes the new ceiling before handing out any of
// it. Written first on purpose: if the power goes between the write and the
// first use, the numbers in the block are simply skipped. The other order would
// hand out numbers the flash does not know about, and a reboot would reuse
// them - which the node would refuse as replays.
void reserveBlock() {
  counterCeiling = prefs.getULong("counter", 0) + CounterBlockSize;
  prefs.putULong("counter", counterCeiling);
  counter = counterCeiling - CounterBlockSize;
}

} // namespace

void settingsLoad() {
  prefs.begin(NodeNamespace, false);

  settings.nodeId = prefs.getUShort("node_id", DefaultNodeId);
  settings.targetId = prefs.getUShort("target", DefaultTargetId);
  settings.maxTargetId = prefs.getUShort("max_target", DefaultMaxTargetId);

  settings.bootCount = prefs.getULong("boots", 0) + 1;
  prefs.putULong("boots", settings.bootCount);

  reserveBlock();

  LOG_INFO(TagCfg, CodeCfgLoaded, (int32_t)settings.nodeId);
  LOG_INFO(TagSys, CodeBootCount, (int32_t)settings.bootCount);
}

bool settingsSaveNodeId(uint16_t nodeId) {
  settings.nodeId = nodeId;
  const bool ok = prefs.putUShort("node_id", nodeId) > 0;
  LOG_AT(ok ? LevelInfo : LevelError, TagCfg, ok ? CodeCfgSaved : CodeCfgSaveFail, nodeId);
  return ok;
}

bool settingsSaveTargetId(uint16_t targetId) {
  settings.targetId = targetId;
  // Written every time the target changes, so a controller picked up after a
  // power cut is still aimed where it was left. A button press is not enough
  // wear to worry about.
  return prefs.putUShort("target", targetId) > 0;
}

bool settingsSaveMaxTargetId(uint16_t maxTargetId) {
  settings.maxTargetId = maxTargetId == 0 ? 1 : maxTargetId;
  return prefs.putUShort("max_target", settings.maxTargetId) > 0;
}

bool settingsReset() {
  prefs.remove("node_id");
  prefs.remove("target");
  prefs.remove("max_target");

  // The counter is NOT reset. It is not a setting - it is a promise to every
  // node that this controller will never reuse a number. Clearing it here would
  // make every node refuse this controller's commands as replays until it
  // counted back up.
  LOG_WARN(TagCfg, CodeCfgDefaults, 0);

  settings.nodeId = DefaultNodeId;
  settings.targetId = DefaultTargetId;
  settings.maxTargetId = DefaultMaxTargetId;
  return true;
}

uint32_t counterNext() {
  if (counter >= counterCeiling) reserveBlock();
  return ++counter;
}

uint32_t counterCurrent() { return counter; }
