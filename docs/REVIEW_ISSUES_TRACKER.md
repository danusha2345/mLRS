# Трекер проблем mLRS

Снимок углублённого read-only аудита `main` на 2026-08-04.

- Проверенный commit: `a155e211` (`v1.4.03`, dev).
- ESP build matrix после генерации fastMAVLink: 32/32 конфигурации успешно.
- STM32 matrix не собиралась из-за отсутствия toolchain; отдельно проверена
  семантика build scripts.
- Этот файл фиксирует дефекты и критерии закрытия. Он не утверждает, что
  перечисленные исправления уже реализованы.

## Обозначения

Приоритет:

- `P0` — memory corruption, permanent halt, watchdog или доказанная потеря
  данных в основном protocol path.
- `P1` — существенная потеря данных, нарушение timing/recovery либо дефект,
  требующий аппаратного подтверждения точного механизма.
- `P2` — build/CI, диагностика, воспроизводимость и незавершённые функции.

Статус:

- `CONFIRMED` — дефект непосредственно следует из текущего кода или
  воспроизведён моделью/host test.
- `PARTIAL` — опасный механизм подтверждён, но точная связь с field issue ещё
  требует аппаратного A/B.
- `LIMITATION` — функция намеренно запрещена или не завершена; текущий
  production path защищён.
- `FIXED` — исправление реализовано и прошло указанные в карточке проверки.

## Сводка

| ID | P | Статус | Область | Краткое описание | Связь |
|---|---:|---|---|---|---|
| MLRS-001 | P0 | FIXED | MAVLinkX | Bounded decoder отклоняет overflow и malformed tokens | новый |
| MLRS-002 | P0 | CONFIRMED | ARQ | Старый 1-bit ACK подтверждает другой 3-bit `seq_no` | PR #185 |
| MLRS-003 | P0 | CONFIRMED | ARQ | Полный wrap скрывает потерю семи payload и parser reset | PR #185 |
| MLRS-004 | P0 | CONFIRMED | RF RX | `CHECK_ERROR_SYNCWORD` навсегда останавливает receiver | issue #342 |
| MLRS-005 | P0 | CONFIRMED | ESP RF ISR | SPI-команды из ISR способны вызвать interrupt watchdog | issue #342 |
| MLRS-006 | P0 | CONFIRMED | UDP bridge | Datagram >256 bytes навсегда блокирует UDP RX на ESP32 | новый |
| MLRS-007 | P1 | CONFIRMED | RF recovery | Fatal делает recovery после stale/unexpected IRQ недостижимым | issue #342 |
| MLRS-008 | P1 | CONFIRMED | RF IRQ | Неатомарный `volatile irq_status` способен терять IRQ | новый |
| MLRS-009 | P1 | PARTIAL | TCP bridge | Blocking/partial `client.write()` переполняет UART RX | issue #478 |
| MLRS-010 | P1 | PARTIAL | WLE5 timing | MAVLink/MSP loops не имеют byte/time budget | issue #283 |
| MLRS-011 | P1 | CONFIRMED | FIFO/UART | Frame silently обрезается при заполнении очереди | новый |
| MLRS-012 | P1 | CONFIRMED | ARQ | `SetRetryCntAuto()` всегда оставляет один retry | новый |
| MLRS-013 | P1 | CONFIRMED | ARQ | Retry budget меняется во время жизни одного payload | новый |
| MLRS-014 | P1 | CONFIRMED | Bridge UART | Ошибка выделения RX/TX buffer игнорируется | issue #478 class |
| MLRS-015 | P2 | CONFIRMED | Setup | `run_setup.py` не работает non-interactively и не описывает deps | PR #228 |
| MLRS-016 | P2 | CONFIRMED | Generator | fastMAVLink generator может завершиться кодом 0 после exception | новый |
| MLRS-017 | P2 | CONFIRMED | STM32 build | Compile/link/objcopy return codes игнорируются | новый |
| MLRS-018 | P2 | CONFIRMED | CI | На `main` нет CI; PR #155 не является рабочим PR check | PR #155 |
| MLRS-019 | P2 | LIMITATION | Diversity | Single-SPI antenna2-only скрыта, underlying capability не решена | issue #200 |
| MLRS-020 | P2 | CONFIRMED | Tests | Нет host/unit/integration tests и bridge build coverage | новый |

## Подробные карточки

### MLRS-001 — небезопасная MAVLinkX decompression

**Приоритет:** P0  
**Статус:** FIXED

Доказательства:

- compressed payload распаковывается до проверки MAVLink CRC:
  [`mavlinkx.h:724`](../mLRS/Common/thirdparty/mavlinkx.h#L724) и
  [`mavlinkx.h:765`](../mLRS/Common/thirdparty/mavlinkx.h#L765);
- decoder не получает capacity выходного buffer:
  [`mavlinkx.h:1082`](../mLRS/Common/thirdparty/mavlinkx.h#L1082);
- RLE branches выполняют `memset(count)` без runtime bound:
  [`mavlinkx.h:1141`](../mLRS/Common/thirdparty/mavlinkx.h#L1141);
- `CHECKRANGE` в release пустой:
  [`mavlinkx.h:326`](../mLRS/Common/thirdparty/mavlinkx.h#L326);
- реальные parser buffers имеют размер 300 bytes.

Воздействие: crafted или ошибочно сформированный compressed stream может
перезаписать память до того, как CRC отклонит сообщение.

Исправление:

- decoder принимает `out_capacity` и возвращает success/error;
- до каждой записи проверяется остаток buffer;
- на overflow/truncated token parser сбрасывается без использования partial
  output;
- validation выполняется до небезопасного преобразования либо используется
  отдельный bounded scratch buffer.

Definition of done:

- ASan/UBSan host tests для repeated RLE255, truncated token, invalid code и
  максимального корректного payload;
- ни один malformed input не пишет за пределы buffer;
- корректные MAVLinkX frames остаются wire-compatible.

Реализовано:

- `_fmavX_payload_decompress()` принимает `out_capacity`, возвращает
  success/error и проверяет каждую single-byte/RLE запись;
- truncated/invalid/zero-length tokens отклоняются, а parser сбрасывается без
  публикации partial payload;
- [`tests/host/test_mavlinkx.cpp`](../tests/host/test_mavlinkx.cpp) проверяет
  repeated RLE255, overflow после 255-го byte, truncated и invalid tokens,
  максимальный payload и frame round-trip;
- `tests/host/run_mavlinkx_tests.sh` проходит с ASan/UBSan;
- PlatformIO firmware builds `rx-generic-2400` (ESP8266) и
  `tx-radiomaster-rp4td-2400-sik-telem` (ESP32) проходят.

### MLRS-002 — stale ACK alias в ARQ

**Приоритет:** P0  
**Статус:** CONFIRMED

Доказательства:

- frame несёт `seq_no : 3`, но `ack : 1`:
  [`frame_types.h:62`](../mLRS/Common/frame_types.h#L62);
- sender сравнивает только parity:
  [`arq.h:125`](../mLRS/Common/arq.h#L125);
- после исчерпания retry sender принудительно увеличивает `payload_seq_no`:
  [`arq.h:139`](../mLRS/Common/arq.h#L139).

Минимальная последовательность:

1. Последний принятый payload — `seq=5`.
2. Обе попытки `seq=6` теряются.
3. Первая попытка `seq=7` теряется.
4. Старый `ACK=5` считается ACK для `seq=7`, поскольку parity совпадает.

Воздействие: sender считает непринятый serial payload доставленным и идёт
дальше. Локальная замена одного сравнения проблему не решает: потерянные два
старших ACK bits уже невозможно восстановить.

Исправление требует protocol decision:

- full 3-bit ACK с использованием существующих `spare` bits; и
- явный discontinuity/drop marker для forced advance;
- либо строгий stop-and-wait без forced advance с осознанным риском остановки
  telemetry при асимметричном линке.

Definition of done:

- regression sequence `5 -> lost 6 -> lost 7` не даёт false ACK;
- property test перебирает потери uplink/downlink;
- sender никогда не подтверждает payload, которого receiver не принимал.

### MLRS-003 — blind wrap ARQ

**Приоритет:** P0  
**Статус:** CONFIRMED

Receiver определяет свежесть простым `received_seq_no != last` и сам код
признаёт ограничение modulo-8:
[`arq.h:306`](../mLRS/Common/arq.h#L306).

Последовательность `last=5`, lost `6,7,0,1,2,3,4`, затем новый `5` даёт:

- `AcceptPayload=false`;
- `FrameLost=false`;
- новый payload ошибочно считается duplicate;
- MAVLink/MSP parser не получает reset:
  [`mlrs-tx.cpp:571`](../mLRS/CommonTx/mlrs-tx.cpp#L571).

Disconnect timeout 1250 ms не защищает от wrap: до disconnect проходит больше
восьми кадров во всех актуальных режимах.

Definition of done:

- end-to-end test полного modulo wrap;
- любой необратимый byte gap приводит к явному parser discontinuity;
- новый payload с совпавшим modulo number не теряется как duplicate.

### MLRS-004 — permanent halt на sync-word mismatch

**Приоритет:** P0  
**Статус:** CONFIRMED  
**GitHub:** [issue #342](https://github.com/olliw42/mLRS/issues/342)

RX вызывает `FAIL_WMSG` при `CHECK_ERROR_SYNCWORD`:
[`mlrs-rx.cpp:488`](../mLRS/CommonRx/mlrs-rx.cpp#L488). `fail()` никогда не
возвращается: [`fail.h:49`](../mLRS/Common/fail.h#L49).

TX ту же ошибку обрабатывает как `RX_STATUS_INVALID`:
[`mlrs-tx.cpp:651`](../mLRS/CommonTx/mlrs-tx.cpp#L651).

Исправление: отбросить frame, увеличить диагностический counter и продолжить
state machine. После bounded серии ошибок разрешена radio reinitialization,
но не infinite halt.

Definition of done:

- injected mismatch не вызывает fatal;
- 100–1000 hardware attenuation/link-loss cycles завершаются повторным
  `CONNECTED` без reboot.

### MLRS-005 — SPI/radio work в ESP ISR

**Приоритет:** P0  
**Статус:** CONFIRMED  
**GitHub:** [issue #342](https://github.com/olliw42/mLRS/issues/342)

DIO ISR выполняет `GetAndClearIrqStatus()` и `ReadBuffer()` через shared SPI:
[`mlrs-rx.cpp:177`](../mLRS/CommonRx/mlrs-rx.cpp#L177),
[`mlrs-tx.cpp:301`](../mLRS/CommonTx/mlrs-tx.cpp#L301).

ESP path использует `spiTransferBytesNL()` без lock:
[`esp-spi.h:45`](../mLRS/modules/esp-lib/esp-spi.h#L45). Историческая сборка
RP4-TD локализовала опубликованные crash return addresses в
`Sx128xDriver::SpiRead/SpiTransfer`; crash происходил в ISR context.

Исправление:

- ISR только ставит atomic pending flag;
- hardware IRQ и buffer читаются в main context;
- SPI/BUSY waits получают deadline;
- timeout приводит к reset/reconfigure radio, а не watchdog.

Definition of done:

- instrumented DIO handler не выполняет SPI;
- stuck BUSY/SPI fault завершается bounded recovery;
- одновременные DIO1/DIO2 events не теряются.

### MLRS-006 — UDP RX wedge после datagram >256 bytes

**Приоритет:** P0  
**Статус:** CONFIRMED

Bridge использует 256-byte buffer:
[`mlrs-wireless-bridge.ino:1272`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L1272),
а UDP handlers делают только один `read()`:
[`877`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L877),
[`942`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L942),
[`991`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L991).

На ESP32 Arduino core 3.3.10 unread tail остаётся в `rx_buffer`, после чего
`parsePacket()` возвращает 0. Bridge больше не вызывает `read()`, поэтому UDP
RX зависает до reinit. ESP8266 отбрасывает хвост, то есть получает truncation,
но не тот же wedge.

Исправление: drain всей текущей datagram чанками; увеличение buffer до 300
bytes само по себе недостаточно, поскольку datagram может содержать несколько
MAVLink frames.

Definition of done:

- tests для datagram 256, 257, 280 и 1460 bytes;
- следующий heartbeat принимается после каждого тестового пакета;
- проверены ESP32 core 3.3.10 и ESP8266.

### MLRS-007 — недостижимый recovery после unexpected IRQ

**Приоритет:** P1  
**Статус:** CONFIRMED

В нескольких ветках сначала вызывается fatal, а ниже расположен код очистки
IRQ и восстановления state:
[`mlrs-rx.cpp:679`](../mLRS/CommonRx/mlrs-rx.cpp#L679),
[`mlrs-tx.cpp:890`](../mLRS/CommonTx/mlrs-tx.cpp#L890).

Комментарии рядом признают поздний `RX_DONE` возможным при плохой связи, то
есть это не невозможный invariant.

Definition of done: stale `RX_DONE`, `TX_DONE` и `TIMEOUT` fault injection
очищает IRQ, переармирует radio и не вызывает fatal.

### MLRS-008 — потеря IRQ из-за read/process/clear race

**Приоритет:** P1  
**Статус:** CONFIRMED

ISR присваивает новое значение `irq_status`, main loop отдельно читает и затем
обнуляет его. `volatile` не делает эту последовательность атомарной. IRQ,
пришедший между чтением и очисткой, может быть затёрт.

Исправление: pending bitmask/event counter и atomic exchange; hardware IRQ
читается в main context.

Definition of done: stress test одновременных и повторных DIO events не теряет
ни одного события и не приводит state machine в невозможное состояние.

### MLRS-009 — TCP backpressure и starvation

**Приоритет:** P1  
**Статус:** PARTIAL  
**GitHub:** [issue #478](https://github.com/olliw42/mLRS/issues/478)

Результат `client.write()` игнорируется:
[`mlrs-wireless-bridge.ino:840`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L840).
ESP32 implementation пытается дописать всё, но может блокировать loop примерно
до 10 секунд и затем вернуть partial result. UART RX 2 KiB при 115200 baud
заполняется примерно за 178 ms.

Подтверждён механизм потери данных при медленно читающем TCP client, но не
доказано, что именно он вызвал конкретный отчёт #478.

Исправление:

- bounded non-blocking queue или короткий write budget;
- обработка partial result;
- counters: requested/written bytes, block duration, UART high-water/errors;
- ограничение `while(client.available())` по bytes/time.

Definition of done: bidirectional soak с паузами чтения GCS 0.2/1/10 s не
теряет sequence/hash и корректно восстанавливается после backpressure.

### MLRS-010 — нет execution budget в MAVLink/MSP path

**Приоритет:** P1  
**Статус:** PARTIAL  
**GitHub:** [issue #283](https://github.com/olliw42/mLRS/issues/283)

Commit `b86cdf` ограничил обработку одним корректным message, но не количеством
прочитанных bytes и не временем:
[`mavlink_interface_tx.h:382`](../mLRS/CommonTx/mavlink_interface_tx.h#L382).

Malformed/partial stream может просканировать весь FIFO/UART backlog. Time
guard выставляется ISR, но уже начавшийся `mavlink.Do()` внутри loops его не
проверяет.

Исправление: persistent parser state и per-call byte/time budget во всех
MAVLink/MSP loops.

Definition of done:

- DWT/GPIO histogram на WLE5;
- replay MAVFTP, max MAVLinkX, back-to-back frames и garbage;
- worst-case parser time остаётся внутри pre-transmit margin;
- многочасовой soak проходит без link drop.

### MLRS-011 — silent partial frame enqueue

**Приоритет:** P1  
**Статус:** CONFIRMED

`tFifo::PutBuf()` игнорирует неуспешный `Put()`:
[`fifo.h:31`](../mLRS/Common/libs/fifo.h#L31). STM32 `uart_putbuf()` аналогично
игнорирует отказ `uart_putc()`:
[`stdstm32-uart.h:653`](../mLRS/modules/stm32ll-lib/src/stdstm32-uart.h#L653).

Host reproduction: 266-byte MAVLink `FILE_TRANSFER_PROTOCOL` в 256-byte FIFO
оставляет 255 bytes без ошибки. Повреждённый префикс frame затем попадает в
stream.

Исправление: atomic `HasSpace(frame_len)` + enqueue либо полный drop; API
возвращает count/error и ведёт overflow counter.

Definition of done: для всех fill levels frame либо помещается полностью, либо
не меняет FIFO; partial prefix невозможен.

### MLRS-012 — adaptive retry всегда равен одному

**Приоритет:** P1  
**Статус:** CONFIRMED

После выбора 1–3 retries безусловно выполняется `SetRetryCnt(1)`:
[`arq.h:192`](../mLRS/Common/arq.h#L192).

Самостоятельно выпускать эту правку нельзя: она чаще активирует MLRS-002/003.

Definition of done после protocol fix:

- FLRC: 699→1, 700→2, 799→2, 800→3;
- остальные modes: 799→1, 800→2;
- unknown mode→1.

### MLRS-013 — retry budget меняется внутри payload

**Приоритет:** P1  
**Статус:** CONFIRMED

`SetRetryCntAuto()` вызывается и для fresh frame, и после retransmission:
[`mlrs-rx.cpp:345`](../mLRS/CommonRx/mlrs-rx.cpp#L345). Поэтому около порогов
700/800 один payload может начать с лимитом 2–3, а закончить с лимитом 1.

Исправление: вычислять и фиксировать budget только при создании fresh payload.

Definition of done: изменение link metric во время retries не меняет budget
текущего payload, но применяется к следующему.

### MLRS-014 — UART buffer allocation failure игнорируется

**Приоритет:** P1  
**Статус:** CONFIRMED

`setRxBufferSize(2048)` и `setTxBufferSize(512)` возвращают 0 при failure, но
результат только печатается:
[`mlrs-wireless-bridge.ino:1216`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L1216).

Definition of done: setup либо получает требуемые buffers, либо явно включает
degraded mode с counters/меньшим baud rate, либо прекращает запуск bridge с
понятной ошибкой.

### MLRS-015 — `run_setup.py` не автоматизируем

**Приоритет:** P2  
**Статус:** CONFIRMED  
**GitHub:** [PR #228](https://github.com/olliw42/mLRS/pull/228)

Проблемы:

- default setup падает на отсутствующем Python module `em`;
- dependencies не зафиксированы в requirements/lock file;
- ошибки и успешный конец требуют `input()`:
  [`run_setup.py:30`](../run_setup.py#L30),
  [`run_setup.py:134`](../run_setup.py#L134);
- non-interactive запуск завершается `EOFError`.

Draft PR #228 не закрывает цепочку полностью и должен быть перепроверен: его
wrapper добавляет venv Python перед командой, которая уже начинается с
`sys.executable`.

Definition of done: чистый Linux/Windows runner выполняет setup без ввода,
использует pinned dependencies и возвращает корректный exit code.

### MLRS-016 — generator сообщает успех после exception

**Приоритет:** P2  
**Статус:** CONFIRMED

`fmav_generate_c_library.py` сначала удаляет `out`, а при exception вызывает
`exit()` без ненулевого кода:
[`fmav_generate_c_library.py:52`](../mLRS/Common/mavlink/fmav_generate_c_library.py#L52).

Definition of done: failure сохраняет понятную диагностику, завершается
non-zero и не может быть принят wrapper/CI за успешную генерацию.

### MLRS-017 — STM32 build допускает false success

**Приоритет:** P2  
**Статус:** CONFIRMED

Return codes игнорируются для:

- compile: [`run_make_firmwares.py:641`](../tools/run_make_firmwares.py#L641);
- link: [`run_make_firmwares.py:711`](../tools/run_make_firmwares.py#L711);
- size/objcopy: [`run_make_firmwares.py:769`](../tools/run_make_firmwares.py#L769).

Дополнительно результаты `ThreadPoolExecutor.map()` не потребляются. Скрипт
может закончиться кодом 0 после неполной/неуспешной сборки.

Definition of done: любая compile/link/objcopy failure немедленно даёт
non-zero; проверяется наличие и размер каждого ожидаемого artifact; CI имеет
negative test с намеренно сломанным source/flag.

### MLRS-018 — отсутствует работающий PR CI

**Приоритет:** P2  
**Статус:** CONFIRMED  
**GitHub:** [PR #155](https://github.com/olliw42/mLRS/pull/155)

На `main` нет `.github/workflows`. PR #155:

- запускается только на `push`, а не `pull_request`;
- вызывает интерактивный и зависимый от незадекларированного `em`
  `run_setup.py`;
- не имеет reported checks;
- проверяет только ESP targets.

Definition of done:

- required checks на каждом PR;
- setup/generators, 32 ESP targets, representative STM32 matrix и bridge
  variants;
- pinned toolchain/dependencies;
- artifacts не публикуются при любой partial failure.

### MLRS-019 — single-SPI antenna2-only остаётся незавершённой

**Приоритет:** P2  
**Статус:** LIMITATION  
**GitHub:** [issue #200](https://github.com/olliw42/mLRS/issues/200)

Mask `0b11011` запрещает antenna2-only для single-SPI устройств. Проверены
UI, EEPROM startup sanitation, bind и remote `SET_RX_PARAMS`: рабочего обхода
до runtime не найдено. Remote command может оставить запрещённое значение в
EEPROM, но restart санитизирует его до antenna1.

Точная историческая причина crash из #200 текущими исходниками не доказана:
`sx.Init()` и `sx2.Init()` вызываются безусловно. Поэтому issue нельзя считать
активным подтверждённым crash текущего `main`.

Definition of done: antenna2-only либо полноценно поддерживается аппаратным
тестом, либо запрещается на всех API/runtime boundaries и в EEPROM хранится
уже санитизированное значение.

### MLRS-020 — отсутствует автоматическое тестовое покрытие

**Приоритет:** P2  
**Статус:** CONFIRMED

В основном проекте найден только ручной [`Common/test.h`](../mLRS/Common/test.h).
Нет host tests для ARQ/frame/FIFO/MAVLinkX, hardware recovery suite, bridge
build matrix и timing regression tests.

Минимальная программа:

1. Host: ARQ state/property tests, MAVLinkX ASan, FIFO atomicity, frame
   pack/check.
2. ESP: все PlatformIO targets плюс отдельные bridge protocol/board variants.
3. STM32: representative MCU families и полный release matrix.
4. Hardware: RP4-TD attenuation/recovery, WLE5 timing, TCP/UDP bridge soak.

Definition of done: эти suites являются required PR checks, а hardware tests
сохраняют raw counters/logs и привязаны к exact firmware hash.

## Исправленные или недоказанные первоначальные выводы

- `SERIAL.write(buf,len)` на ESP32 core 3.3.10 не делает silent partial write:
  он полностью enqueue-ит данные либо блокируется. Риск — starvation.
- `client.setNoDelay(true)` после accept не исправляет #478: установленный на
  server `setNoDelay(true)` уже наследуется accepted socket.
- Для #478 доказан TCP backpressure mechanism, но не доказано, что именно он
  является причиной конкретной пользовательской regression.
- #200 сейчас является незавершённой capability, а не подтверждённым crash
  текущего `main`.
- Успешные 32/32 ESP builds доказывают compileability только после ручной
  генерации fastMAVLink; они не доказывают runtime correctness.

## Рекомендуемый порядок работ

1. MLRS-001: bounded MAVLinkX decoder и sanitizer tests.
2. MLRS-005, MLRS-004, MLRS-007, MLRS-008: ISR/recovery redesign.
3. MLRS-006: UDP draining.
4. MLRS-002, MLRS-003, затем MLRS-012/013: ARQ protocol redesign.
5. MLRS-011 и MLRS-014: atomic buffering и observable overflow.
6. MLRS-010: WLE5 execution budgets и timing proof.
7. MLRS-009: TCP queues/backpressure и hardware A/B #478.
8. MLRS-015–018, MLRS-020: воспроизводимый setup, fail-fast builds и CI.
9. MLRS-019: закрыть либо документировать antenna2-only limitation.

До закрытия P0 итоговый статус dev `v1.4.03`: **NO-GO для production fork**.
