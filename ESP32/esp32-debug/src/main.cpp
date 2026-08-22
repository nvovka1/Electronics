/*
 * ESP32 crash demo — навмисне падіння прошивки з виводом помилки в Serial Monitor.
 *
 * Після старту чекає 10 секунд на вибір типу аварії в Serial Monitor.
 * Якщо нічого не натиснути — виконує ділення на нуль.
 *
 * Панічний обробник ESP32 друкує в UART "Guru Meditation Error", регістри
 * і Backtrace, після чого чіп перезавантажується. Щоб побачити не голі
 * адреси, а файл:рядок, у platformio.ini увімкнено фільтр
 * monitor_filters = esp32_exception_decoder.
 */

#include <Arduino.h>
#include <esp_system.h>

static const uint32_t kChoiceTimeoutMs = 10000;

// volatile — щоб компілятор не згорнув вираз ще на етапі компіляції
static volatile int zero = 0;
static volatile int result = 0;

static void printResetReason() {
  Serial.print(F("Причина попереднього старту: "));
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  Serial.println(F("ESP_RST_POWERON (подано живлення)")); break;
    case ESP_RST_SW:       Serial.println(F("ESP_RST_SW (програмний перезапуск)")); break;
    case ESP_RST_PANIC:    Serial.println(F("ESP_RST_PANIC (аварія — попередня прошивка впала!)")); break;
    case ESP_RST_INT_WDT:  Serial.println(F("ESP_RST_INT_WDT (interrupt watchdog)")); break;
    case ESP_RST_TASK_WDT: Serial.println(F("ESP_RST_TASK_WDT (task watchdog)")); break;
    case ESP_RST_WDT:      Serial.println(F("ESP_RST_WDT (інший watchdog)")); break;
    case ESP_RST_BROWNOUT: Serial.println(F("ESP_RST_BROWNOUT (просадка живлення)")); break;
    default:               Serial.println(F("інша")); break;
  }
}

static void printMenu() {
  Serial.println();
  Serial.println(F("=== ESP32 crash demo ==="));
  Serial.println(F("Оберіть тип аварії (надішліть символ у Serial Monitor):"));
  Serial.println(F("  1 - ділення на нуль        -> IntegerDivideByZero"));
  Serial.println(F("  2 - запис за NULL-адресою  -> StoreProhibited"));
  Serial.println(F("  3 - читання за NULL-адресою-> LoadProhibited"));
  Serial.println(F("  4 - переповнення стека     -> Stack canary watchpoint"));
  Serial.println(F("  5 - abort() / assert       -> abort() was called"));
  Serial.println(F("  6 - зависання в циклі      -> Task watchdog (TWDT)"));
  Serial.printf("Без вибору через %lu с виконається варіант 1.\n\n", kChoiceTimeoutMs / 1000);
}

// --- 1. Ділення на нуль -------------------------------------------------
static void crashDivideByZero() {
  Serial.println(F(">>> Ділимо 100 на 0..."));
  Serial.flush();
  result = 100 / zero;          // Guru Meditation Error: IntegerDivideByZero
  Serial.println(result);       // сюди виконання вже не дійде
}

// --- 2. Запис за некоректною адресою ------------------------------------
static void crashNullWrite() {
  Serial.println(F(">>> Пишемо 42 за адресою 0x00000000..."));
  Serial.flush();
  volatile int *pointer = reinterpret_cast<volatile int *>(0);
  *pointer = 42;                // Guru Meditation Error: StoreProhibited
}

// --- 3. Читання за некоректною адресою ----------------------------------
static void crashNullRead() {
  Serial.println(F(">>> Читаємо з адреси 0x00000000..."));
  Serial.flush();
  volatile int *pointer = reinterpret_cast<volatile int *>(0);
  result = *pointer;            // Guru Meditation Error: LoadProhibited
}

// --- 4. Переповнення стека ----------------------------------------------
static uint32_t recurse(uint32_t depth) {
  volatile uint8_t eatStack[256];   // "з'їдаємо" стек на кожному виклику
  eatStack[0] = static_cast<uint8_t>(depth);
  return eatStack[0] + recurse(depth + 1);
}

static void crashStackOverflow() {
  Serial.println(F(">>> Нескінченна рекурсія — вичерпуємо стек..."));
  Serial.flush();
  result = static_cast<int>(recurse(0));  // Stack canary watchpoint triggered
}

// --- 5. abort() ---------------------------------------------------------
static void crashAbort() {
  Serial.println(F(">>> Порушуємо інваріант і викликаємо abort()..."));
  Serial.flush();
  configASSERT(zero != 0);      // умова хибна -> abort() was called
  abort();                      // на випадок, якщо assert вимкнено
}

// --- 6. Task watchdog ---------------------------------------------------
static void crashTaskWatchdog() {
  Serial.println(F(">>> Блокуємо задачу назавжди — чекаємо на watchdog..."));
  Serial.flush();
  noInterrupts();               // не даємо навіть перервам врятувати ситуацію
  while (true) {
    // порожній цикл: idle-задача не отримує часу -> спрацьовує TWDT/INT_WDT
  }
}

static char readChoice() {
  uint32_t startedAt = millis();
  while (millis() - startedAt < kChoiceTimeoutMs) {
    if (Serial.available() > 0) {
      char choice = static_cast<char>(Serial.read());
      if (choice >= '1' && choice <= '6') {
        return choice;
      }
    }
    delay(10);
  }
  return '1';   // варіант за замовчуванням
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);                  // чекаємо на USB CDC там, де він є
  }
  delay(500);

  Serial.println();
  printResetReason();
  printMenu();

  char choice = readChoice();
  Serial.printf("Обрано варіант %c\n", choice);
  Serial.flush();
  delay(100);

  switch (choice) {
    case '1': crashDivideByZero();  break;
    case '2': crashNullWrite();     break;
    case '3': crashNullRead();      break;
    case '4': crashStackOverflow(); break;
    case '5': crashAbort();         break;
    case '6': crashTaskWatchdog();  break;
  }

  Serial.println(F("!!! Ми не мали сюди дійти — аварія не спрацювала."));
}

void loop() {
  delay(1000);
}
