# Розшифрування crash-дампу ESP32

## Розширення

Встановлено `dankeboy36.esp-exception-decoder` v2.1.1 (тягне за собою
залежність `dankeboy36.boardlab`). Додано до `.vscode/extensions.json`,
тож на іншій машині VS Code запропонує його встановити автоматично.

Ручне встановлення:

```
code --install-extension dankeboy36.esp-exception-decoder
```

## Передумови

У `platformio.ini` вже виставлено `build_type = debug` — без символів
налагодження в `.elf` декодер поверне лише `??:0`.

## Як розшифрувати

1. `Ctrl+Shift+P` -> **ESP Exception Decoder: Show Decoder Terminal**.
2. Обрати `.elf` поточного оточення:
   `.pio/build/esp32dev/firmware.elf` (або `.pio/build/arduino_nano_esp32/firmware.elf`).
3. Вставити в термінал увесь блок дампу з Serial Monitor — від рядка
   `Guru Meditation Error:` до `Backtrace: ...` включно (див. `docs/crash-dump.txt`).
4. Розширення надрукує кожен кадр як `функція at файл:рядок` — рядки
   власного коду клікабельні й ведуть просто в редактор.

Альтернатива — вкладка **ESP Crash Capturer** (`ESP Exception Decoder: Create
Crash Capturer`): розширення саме слухає COM-порт і ловить дамп у момент
падіння, декодування відбувається без копіювання вручну.

## Результат декодування

Дамп із `docs/crash-dump.txt`:

| Кадр | Адреса | Функція | Файл | Рядок |
|---|---|---|---|---|
| #0 (місце падіння) | `0x400d15f3` | `crashDivideByZero()` | `src/main.cpp` | **53** |
| #1 (хто викликав) | `0x400d17bc` | `setup()` | `src/main.cpp` | 135 |
| #2 | `0x400d39d6` | `loopTask(void*)` | `framework-arduinoespressif32/cores/esp32/main.cpp` | 42 |

- **Файл:** `src/main.cpp`
- **Номер рядка:** `53` -> `result = 100 / zero;`
- **Функція:** `crashDivideByZero()`, викликана з `setup()` (рядок 135).

`EXCCAUSE: 0x00000006` = `IntegerDivideByZero` — ділення на нуль, як і очікувалось.

## Те саме без розширення (перевірка)

Розширення під капотом викликає `addr2line` із тулчейна PlatformIO:

```
C:\Users\<user>\.platformio\packages\toolchain-xtensa-esp32\bin\xtensa-esp32-elf-addr2line.exe -pfiaC -e .pio\build\esp32dev\firmware.elf 0x400d15f3 0x400d17bc 0x400d39d6
```

Вивід:

```
0x400d15f3: crashDivideByZero() at .../src/main.cpp:53
0x400d17bc: setup() at .../src/main.cpp:135
0x400d39d6: loopTask(void*) at .../cores/esp32/main.cpp:42
```

## Застереження щодо `docs/crash-dump.txt`

На момент підготовки жодної плати не було підключено (у системі немає COM-портів),
тому дамп у `docs/crash-dump.txt` — **шаблон, а не знімок із заліза**. Адреси
`PC` і `Backtrace` в ньому взято з реального `firmware.elf` цієї збірки
(`quos` — інструкція ділення за адресою `0x400d15f3`), тому декодування вище
справжнє. Значення регістрів `A2..A15`/`A1` — заповнювачі.

Після прошивки плати замініть вміст файлу справжнім текстом із Serial Monitor:
адреси зміняться при кожній перезбірці, але процедура декодування та сама.
