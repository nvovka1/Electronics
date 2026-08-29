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
