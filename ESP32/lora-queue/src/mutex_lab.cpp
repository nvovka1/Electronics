#include "mutex_lab.h"

#include <esp_task_wdt.h>

#ifndef SCENARIO
  #define SCENARIO 1
#endif

constexpr uint32_t TWDT_TIMEOUT_S = 5;

static bool labStarted = false;

#if SCENARIO == 1

constexpr uint32_t HANDSHAKE_MS = 50;   // guarantees the bad interleaving

static SemaphoreHandle_t mutexA = nullptr;
static SemaphoreHandle_t mutexB = nullptr;

struct WorkerPlan {
  const char*        name;
  SemaphoreHandle_t* firstMutex;
  const char*        firstName;
  SemaphoreHandle_t* secondMutex;
  const char*        secondName;
};

static const WorkerPlan PLAN_AB{"AB", &mutexA, "A", &mutexB, "B"};
static const WorkerPlan PLAN_BA{"BA", &mutexB, "B", &mutexA, "A"};

static void worker(void* arg) {
  const WorkerPlan& plan = *static_cast<const WorkerPlan*>(arg);

#ifdef WATCH_TASKS
  // Subscribe to the Task Watchdog. Without this the TWDT watches only the idle
  // tasks - and a deadlock does not starve those, so nothing would ever notice.
  esp_task_wdt_add(nullptr);
  Serial.printf("[lab %s core %d] subscribed to the task watchdog\n",
                plan.name, xPortGetCoreID());
#endif

  for (uint32_t cycle = 1;; ++cycle) {
#ifdef WATCH_TASKS
    esp_task_wdt_reset();
#endif

    Serial.printf("[lab %s core %d] cycle %lu: take %s\n",
                  plan.name, xPortGetCoreID(), (unsigned long)cycle, plan.firstName);
    xSemaphoreTake(*plan.firstMutex, portMAX_DELAY);

    Serial.printf("[lab %s] holds %s, sleeping %lu ms so the other task takes %s\n",
                  plan.name, plan.firstName, (unsigned long)HANDSHAKE_MS, plan.secondName);
    vTaskDelay(pdMS_TO_TICKS(HANDSHAKE_MS));

    Serial.printf("[lab %s] take %s   <-- blocks here forever\n",
                  plan.name, plan.secondName);
    xSemaphoreTake(*plan.secondMutex, portMAX_DELAY);

    // Never reached on cycle 1.
    Serial.printf("[lab %s] got both, releasing\n", plan.name);
    xSemaphoreGive(*plan.secondMutex);
    xSemaphoreGive(*plan.firstMutex);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

const char* mutexLabName() {
#ifdef TWDT_PANIC
  return "AB/BA deadlock, task watchdog in panic mode";
#elif defined(WATCH_TASKS)
  return "AB/BA deadlock, workers watched by the task watchdog";
#else
  return "AB/BA deadlock, nobody is watching";
#endif
}

static void labStart() {
  mutexA = xSemaphoreCreateMutex();
  mutexB = xSemaphoreCreateMutex();

#ifdef TWDT_PANIC
  // panic = true turns the watchdog warning into abort() -> Guru Meditation.
  esp_task_wdt_init(TWDT_TIMEOUT_S, true);
  Serial.printf("[lab] task watchdog reconfigured: %lu s, panic ON\n",
                (unsigned long)TWDT_TIMEOUT_S);
#endif

  xTaskCreatePinnedToCore(worker, "workerAB", 3072, (void*)&PLAN_AB, 3, nullptr, 0);
  xTaskCreatePinnedToCore(worker, "workerBA", 3072, (void*)&PLAN_BA, 3, nullptr, 1);
}

// ---------------------------------------------------------------- scenario 2
#elif SCENARIO == 2

// Priority inversion with a busy wait, both tasks pinned to core 0.
//
// The low-priority task takes the mutex first. The high-priority task then
// polls for it with timeout 0 and never yields, so the low-priority holder is
// never scheduled again and can never release. Nothing blocks - core 0 simply
// stops running anything else, including its idle task, and the Task Watchdog
// fires on IDLE0.

static SemaphoreHandle_t mutex = nullptr;

static void lowPriorityHolder(void* /*arg*/) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  Serial.printf("[lab low core %d] took the mutex, starting slow work\n", xPortGetCoreID());

  for (uint32_t step = 1;; ++step) {
    Serial.printf("[lab low] slow work step %lu (still holding the mutex)\n",
                  (unsigned long)step);
    vTaskDelay(pdMS_TO_TICKS(500));   // after the spinner starts, never runs again
  }
}

static void highPrioritySpinner(void* /*arg*/) {
  vTaskDelay(pdMS_TO_TICKS(1200));    // let the low-priority task take it first

  Serial.printf("[lab high core %d] polling for the mutex without yielding\n",
                xPortGetCoreID());
  Serial.flush();

  uint32_t spins = 0;
  while (xSemaphoreTake(mutex, 0) != pdTRUE) {
    ++spins;   // no vTaskDelay, no blocking take: core 0 is now ours forever
  }

  Serial.printf("[lab high] impossible: got the mutex after %lu spins\n",
                (unsigned long)spins);
  vTaskDelete(nullptr);
}

const char* mutexLabName() {
  return "priority inversion, high-priority task spins on core 0";
}

static void labStart() {
  mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(lowPriorityHolder,  "labLow",  3072, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(highPrioritySpinner, "labHigh", 3072, nullptr, 5, nullptr, 0);
}

// ---------------------------------------------------------------- scenario 3
#elif SCENARIO == 3

// Waiting for another task from inside a critical section.
//
// The holder takes the mutex, enters a critical section (which disables
// interrupts on this core) and then waits for a flag that only the releaser
// task can set - but with interrupts off there are no more scheduler ticks, so
// the releaser is never scheduled. The Interrupt Watchdog is the only thing
// left that can still run, and it panics after ~300 ms.

static SemaphoreHandle_t mutex    = nullptr;
static portMUX_TYPE      spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool     released = false;

static void holder(void* /*arg*/) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  Serial.printf("[lab holder core %d] took the mutex, entering critical section\n",
                xPortGetCoreID());
  Serial.flush();   // nothing will be printable after the next line

  portENTER_CRITICAL(&spinlock);
  while (!released) {
    // The releaser can never run: interrupts are off on this core.
  }
  portEXIT_CRITICAL(&spinlock);

  Serial.println("[lab holder] unreachable");
  vTaskDelete(nullptr);
}

static void releaser(void* /*arg*/) {
  vTaskDelay(pdMS_TO_TICKS(2000));
  Serial.println("[lab releaser] setting the flag");   // never gets this far
  released = true;
  vTaskDelete(nullptr);
}

const char* mutexLabName() {
  return "blocking inside a critical section, interrupt watchdog";
}

static void labStart() {
  mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(releaser, "labReleaser", 2560, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(holder,   "labHolder",   3072, nullptr, 4, nullptr, 0);
}

#else
  #error "SCENARIO must be 1, 2 or 3"
#endif

// ---------------------------------------------------------------- common
void mutexLabStart() {
  if (labStarted) {
    Serial.println("[lab] already armed - it is not coming back");
    return;
  }
  labStarted = true;

  Serial.println("\n================ MUTEX LAB ARMED ================");
  Serial.printf("scenario %d: %s\n", SCENARIO, mutexLabName());
  Serial.println("the LED keeps blinking for as long as the system survives");
  Serial.println("=================================================\n");

  labStart();
}
