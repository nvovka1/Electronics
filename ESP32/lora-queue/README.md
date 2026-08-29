# Дві задачі на різних ядрах ESP32

Дві FreeRTOS-задачі, закріплені за різними ядрами, спілкуються **лише через
чергу** — жодна спільна змінна не пишеться однією задачею й не читається іншою.

| Ядро | Задача | Що робить |
|------|--------|-----------|
| 0 | `button` | стежить за кнопкою, вирішує новий інтервал і кладе `BlinkCommand` у чергу |
| 1 | `led` | блимає світлодіодом з інтервалом, який дістав із черги |

## Кнопка

- **одинарне натискання** — наступний інтервал по колу:
  `0,25 с → 0,5 с → 1 с → 2 с → 0,25 с`
- **подвійне натискання** — один крок назад (`1 с → подвійне → 0,5 с`)

Одинарне натискання підтверджується через 300 мс (`DOUBLE_GAP_MS`) — саме стільки
задача чекає, чи не буде другого кліку. Дребезг гаситься 25 мс.

## Як влаштована черга

`blinkCommandQueue` створюється у [setup()](src/main.cpp) і передається обом
задачам як параметр запуску:

```cpp
blinkCommandQueue = xQueueCreate(BLINK_QUEUE_LENGTH, sizeof(BlinkCommand));
ledTaskStart(blinkCommandQueue, 2, LED_CORE);
buttonTaskStart(blinkCommandQueue, 3, BUTTON_CORE);
```

Індекс поточного інтервалу живе всередині задачі кнопки і назовні не видний.
Задача світлодіода не має жодного іншого входу: до першої команди LED не блимає,
а очікування на черзі водночас є паузою між перемиканнями:

```cpp
const TickType_t wait = (intervalMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(intervalMs);
if (xQueueReceive(blinkCommandQueue, &command, wait) == pdTRUE) {
  intervalMs = command.intervalMs;   // нова команда — застосовуємо негайно
} else {
  ledOn = !ledOn;                    // таймаут — це і є півперіод блимання
}
```

Стартову команду надсилає сама задача кнопки, тому й початковий інтервал
приходить із черги.

## Підключення

Нічого паяти не треба — використовуються тільки вбудовані LED і кнопка `BOOT`.

| Сигнал | LoRa32 (`-e lora32`) | ESP32 DevKit (`-e esp32dev`) |
|--------|----------------------|------------------------------|
| LED    | GPIO 25 | GPIO 2 |
| Кнопка | GPIO 0 (`BOOT`, натиснута = LOW) | GPIO 0 (`BOOT`) |

Зовнішню кнопку, якщо потрібно, вішають між GPIO 0 і GND (внутрішній
підтягуючий резистор вмикається у коді).

## Збірка

```
pio run -e lora32   -t upload -t monitor
pio run -e esp32dev -t upload -t monitor
```

У монітор кожна задача друкує своє ядро, тож розподіл по ядрах видно одразу:

```
[setup  core 1] button task -> core 0, LED task -> core 1
[button core 0] start  -> step 0 = 250 ms
[led    core 1] interval := 250 ms (step 0)
[button core 0] single -> step 1 = 500 ms
[led    core 1] interval := 500 ms (step 1)
[button core 0] double -> step 0 = 250 ms
[led    core 1] interval := 250 ms (step 0)
```

---

# Частина 2. Навмисно «зламана» багатозадачність

Та сама прошивка. **Потрійний клік** озброює «лабораторію» — набір задач із
mutex, який гарантовано вішає систему ([mutex_lab.cpp](src/mutex_lab.cpp)).
Задачі блимання з частини 1 навмисно лишаються працювати: **світлодіод і є
індикатором життя**.

| Що робить LED після зависання | Що це означає |
|---|---|
| блимає далі | померли тільки задачі лабораторії, ОС жива |
| завмер, але чіп не перезавантажився | померло ядро, на якому живе задача LED |
| почав з початку з 250 мс | спрацював watchdog і чіп перезавантажився |

## Сценарії

Сценарій обирається середовищем збірки (`-D SCENARIO`), код один і той самий.

### 1. Класичний AB/BA deadlock — `pio run -e lora32`

`workerAB` (ядро 0) бере mutex A, чекає 50 мс, просить B.
`workerBA` (ядро 1) бере B, чекає 50 мс, просить A. Пауза гарантує «погане»
чергування, тому взаємне блокування стається на першому ж циклі.

**Очікувана реакція: жодного watchdog.** Заблоковані на `xSemaphoreTake`
задачі чесно віддають процесор, idle-задачі продовжують «годувати» Task WDT —
і в Serial Monitor не з'являється **нічого**. LED блимає далі. Це найгірший тип
зависання: система мертва, але ніхто про це не повідомляє.

### 2. Той самий deadlock, але під наглядом — `pio run -e deadlock-twdt`

Різниця в одному рядку: перед циклом кожен worker робить
`esp_task_wdt_add(nullptr)` і далі щоцикл `esp_task_wdt_reset()`. Тепер Task WDT
стежить не лише за idle, а й за ними.

Через 5 с у монітор піде (кожні 5 с, без перезавантаження — у Arduino
`CONFIG_ESP_TASK_WDT_PANIC` вимкнено):

```
E (12345) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (12345) task_wdt:  - workerAB (CPU 0)
E (12345) task_wdt:  - workerBA (CPU 1)
E (12345) task_wdt: Tasks currently running:
E (12345) task_wdt: CPU 0: IDLE0
E (12345) task_wdt: CPU 1: IDLE1
```

### 3. Те саме з перезавантаженням — `pio run -e deadlock-panic`

Додає `esp_task_wdt_init(5, true)` — режим паніки. Замість попередження
викликається `abort()`: Guru Meditation, backtrace, перезавантаження.
`monitor_filters = esp32_exception_decoder` розшифрує backtrace у `файл:рядок`.

### 4. Інверсія пріоритетів + активне очікування — `pio run -e starvation`

Обидві задачі на **ядрі 0**. Низькопріоритетна (prio 1) бере mutex.
Високопріоритетна (prio 5) через 1,2 с починає крутити
`while (xSemaphoreTake(mutex, 0) != pdTRUE) {}` — без `vTaskDelay`, без
блокування. Вона ніколи не віддає ядро, тому власник mutex більше ніколи не
виконується і не може його віддати.

**Реакція: Task WDT, але на `IDLE0`** — бо голодує саме idle-задача ядра 0:

```
E (12345) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (12345) task_wdt:  - IDLE (CPU 0)
```

Ядро 1 живе, тому LED блимає далі — а кнопка (задача на ядрі 0) вже мертва.

### 5. Очікування всередині критичної секції — `pio run -e interrupt-wdt`

Власник mutex входить у `portENTER_CRITICAL()` (переривання на ядрі вимкнено) і
чекає на прапорець, який має виставити інша задача. Але без переривань немає й
тіків планувальника — та задача не виконається ніколи.

Єдине, що ще працює, — **Interrupt watchdog** (300 мс). Через третину секунди:

```
Guru Meditation Error: Core  0 panic'ed (Interrupt wdt timeout on CPU0)
Core 0 register dump: ...
Backtrace: 0x... 0x...
Rebooting...
```

## Підсумок: який watchdog що ловить

| Сценарій | Хто помічає | Через скільки | Чи буде reboot |
|---|---|---|---|
| 1. deadlock без нагляду | **ніхто** | ніколи | ні |
| 2. deadlock + `esp_task_wdt_add` | Task WDT (наші задачі) | 5 с | ні (тільки лог) |
| 3. + `esp_task_wdt_init(5, true)` | Task WDT → `abort()` | 5 с | так |
| 4. інверсія пріоритетів | Task WDT (`IDLE0`) | 5 с | ні |
| 5. критична секція | Interrupt WDT | ~300 мс | так |

Головний висновок: **deadlock сам собою watchdog не вмикає**. Task WDT ловить
не «зависання», а те, що задача перестала його годувати, — тож поки мертві
задачі чемно блокуються, а idle працює, система «висить» абсолютно тихо.

## Місце для журналу

Реальні логи з плати — вставити сюди після прошивки:

```
(TODO: paste Serial Monitor output)
```
