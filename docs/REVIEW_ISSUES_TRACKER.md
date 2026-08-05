# Трекер проблем mLRS

Снимок углублённого read-only аудита `main` на 2026-08-04.

- Проверенный commit: `a155e211` (`v1.4.03`, dev).
- ESP build matrix после генерации fastMAVLink: 32/32 конфигурации успешно.
- Установлены Arm GNU Toolchain 11.3.Rel1 и официальный 14.3.Rel1; полная
  STM32 matrix 55/55 собирается обоими compiler lines.
- Доказательства относятся к исходному снимку; статусы `IMPLEMENTED` и `FIXED`
  ниже отражают последующие изменения в рабочей ветке.
- Аппаратная validation в текущем рабочем контуре недоступна. Активный scope —
  только code review, host/model tests, reproducible builds, static analysis и
  CI. Hardware acceptance сохраняется как внешнее ограничение и не выдаётся за
  выполненную проверку.

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
- `IN_PROGRESS` — часть исправления проверена и зафиксирована, но открытая
  часть Definition of done ещё требует реализации или выбора policy.
- `IMPLEMENTED` — исправление и автоматические проверки реализованы, но часть
  platform/hardware validation из Definition of done ещё не выполнена.
- `FIXED` — исправление реализовано и прошло указанные в карточке проверки.

## Сводка

| ID | P | Статус | Область | Краткое описание | Связь |
|---|---:|---|---|---|---|
| MLRS-001 | P0 | FIXED | MAVLinkX | Bounded decoder отклоняет overflow и malformed tokens | новый |
| MLRS-002 | P0 | IMPLEMENTED | ARQ | Полный 3-bit ACK и resync marker исключают stale-ACK alias | PR #185 |
| MLRS-003 | P0 | IMPLEMENTED | ARQ | Marked resync принимается после modulo wrap и сбрасывает parser | PR #185 |
| MLRS-004 | P0 | IMPLEMENTED | RF RX | Sync mismatch отбрасывается; серия ошибок запускает bounded reinit | issue #342 |
| MLRS-005 | P0 | IMPLEMENTED | ESP RF ISR | ISR публикует pending event; radio/SPI work выполняется в main | issue #342 |
| MLRS-006 | P0 | IMPLEMENTED | UDP bridge | Datagram полностью вычитывается чанками во всех UDP handlers | новый |
| MLRS-007 | P1 | IMPLEMENTED | RF recovery | Unexpected IRQ переводит state machine в безопасное состояние | issue #342 |
| MLRS-008 | P1 | IMPLEMENTED | RF IRQ | Saturating pending counter передаёт IRQ из ISR атомарно | новый |
| MLRS-009 | P1 | IMPLEMENTED | TCP bridge | Bounded queues и partial writes ограничивают TCP starvation | issue #478 |
| MLRS-010 | P1 | IMPLEMENTED | WLE5 timing | MAVLink/MSP loops ограничены 64 bytes за вызов | issue #283 |
| MLRS-011 | P1 | IMPLEMENTED | FIFO/UART | FIFO и STM32 UART принимают frame целиком либо полностью отбрасывают | новый |
| MLRS-012 | P1 | FIXED | ARQ | Adaptive retry thresholds больше не затираются значением 1 | новый |
| MLRS-013 | P1 | IMPLEMENTED | ARQ | Retry budget вычисляется только для fresh payload | новый |
| MLRS-014 | P1 | IMPLEMENTED | Bridge UART | Неуспешный UART startup останавливает bridge с диагностическим кодом | issue #478 class |
| MLRS-015 | P2 | IN_PROGRESS | Setup | Non-interactive path и pinned dependencies проверены на Linux; Windows ещё открыт | PR #228 |
| MLRS-016 | P2 | FIXED | Generator | Exception печатается в stderr и завершает generator с code 1 | новый |
| MLRS-017 | P2 | IMPLEMENTED | STM32 build | Fail-fast для compile/link/size/objcopy и проверка artifacts | новый |
| MLRS-018 | P2 | IMPLEMENTED | CI | PR workflows покрывают STM32, 32 ESP targets и три AT-mode bridge variants | PR #155 |
| MLRS-019 | P2 | LIMITATION | Diversity | Single-SPI antenna2-only скрыта, underlying capability не решена | issue #200 |
| MLRS-020 | P2 | IN_PROGRESS | Tests | Host, ESP и bridge CI добавлены; frame/hardware coverage ещё открыто | новый |
| MLRS-021 | P2 | IMPLEMENTED | Toolchain | GCC 11.3/14.3 проходят 55/55; broad guard заменён code-validated верхней границей 14 | issue #159 |
| MLRS-022 | P2 | IMPLEMENTED | ESP build | Portable fail-fast runner проверяет полный набор текущих artifacts | новый |

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
**Статус:** IMPLEMENTED

Доказательства исходного снимка:

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

Исправление требовало protocol decision:

- full 3-bit ACK с использованием существующих `spare` bits; и
- явный discontinuity/drop marker для forced advance;
- либо строгий stop-and-wait без forced advance с осознанным риском остановки
  telemetry при асимметричном линке. Выбран первый вариант.

Definition of done:

- regression sequence `5 -> lost 6 -> lost 7` не даёт false ACK;
- property test перебирает потери uplink/downlink;
- sender никогда не подтверждает payload, которого receiver не принимал.

Реализовано:

- полный ACK 0…7 кодируется текущим low bit и двумя бывшими `spare` bits;
  размер status остаётся 5 bytes, RF frame — 91 bytes, payload — 64/82 bytes;
- high bit `frame_type` используется как forced-discontinuity marker в Rx frame
  и как его echo в следующем Tx ACK;
- второй ранее свободный `frame_type` bit является обязательным ARQ-v2 flag,
  поэтому mixed old/new pair отклоняется сразу вместо скрытой деградации;
- после forced advance marked resync payload не заменяется следующим payload,
  пока receiver не вернёт совпадающие 3-bit ACK и marker echo;
- [`tests/host/test_arq.cpp`](../tests/host/test_arq.cpp) проверяет stale ACK и
  65 536 комбинаций прямых/обратных потерь без false acknowledgement.

Wire format требует одновременно обновлённых TX и RX firmware. До `FIXED`
остаётся hardware loss/attenuation test matched-пары.

### MLRS-003 — blind wrap ARQ

**Приоритет:** P0  
**Статус:** IMPLEMENTED

В исходном снимке receiver определял свежесть простым
`received_seq_no != last`, а код признавал ограничение modulo-8:
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

Реализовано:

- receiver сравнивает полный `(seq_no, discontinuity)` token;
- marked payload принимается даже при совпавшем modulo-8 `seq_no`, выставляет
  `FrameLost()` до передачи payload parser-ам и возвращает marker echo;
- повтор того же marked payload считается duplicate и второй раз не выдаётся;
- host regression проверяет normal `seq=5` → duplicate `5` → marked wrap `5`.

До `FIXED` остаётся end-to-end hardware wrap/loss test на matched firmware.

### MLRS-004 — permanent halt на sync-word mismatch

**Приоритет:** P0  
**Статус:** IMPLEMENTED

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

Реализовано:

- RX и TX больше не вызывают `FAIL_WMSG` при `CHECK_ERROR_SYNCWORD`: frame
  отбрасывается как invalid, а диагностический счётчик ошибки увеличивается;
- успешный `RX_DONE`/`TX_DONE` сбрасывает consecutive streak, три ошибки подряд
  запрашивают полную переинициализацию соответствующих radio в main context;
- reinitialization имеет конечные BUSY waits и возвращает управление main loop
  как при успехе, так и при аппаратной ошибке;
- после неудачной переинициализации следующая попытка откладывается на 1 s,
  поэтому постоянная hardware fault не превращается в tight recovery loop;
- host test проверяет порог `3`, reset streak после успеха и сохранение общего
  счётчика; source regression запрещает возврат fatal в `do_receive()`.

Осталось до `FIXED`: hardware attenuation/link-loss suite из Definition of
done с сохранёнными recovery counters и exact firmware hash.

### MLRS-005 — SPI/radio work в ESP ISR

**Приоритет:** P0  
**Статус:** IMPLEMENTED

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

Реализовано:

- DIO ISR очищает только MCU EXTI flag и увеличивает saturating pending counter;
- чтение/очистка hardware IRQ, проверка prefix и чтение radio buffer перенесены
  в main context;
- `SX128x`, `SX126x`, `LR11xx` и `LR20xx` BUSY waits ограничены 20 ms и
  защёлкивают timeout вместо бесконечного ожидания;
- BUSY timeout запрашивает `Init()`/`StartUp()` radio в main context, не
  останавливая firmware навсегда;
- source regression проверяет отсутствие radio/SPI/config операций в четырёх
  RX/TX DIO handlers, а ESP8266 и ESP32 firmware builds проходят.

Осталось до `FIXED`: instrumented hardware fault injection для DIO и stuck
BUSY; representative STM32 compile проходит с GCC 11.3, runtime validation
остаётся аппаратной.

### MLRS-006 — UDP RX wedge после datagram >256 bytes

**Приоритет:** P0  
**Статус:** IMPLEMENTED

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

Реализовано:

- `udp_read_datagram_to_serial()` вычитывает ровно весь `packetSize` чанками
  рабочего buffer и передаёт каждый chunk в UART;
- общий helper используется в `UDP`, `UDPSTA` и `UDPCl`, поэтому unread tail не
  остаётся в ESP32 `rx_buffer`;
- [`tests/host/test_udp_drain.cpp`](../tests/host/test_udp_drain.cpp) моделирует
  ESP32 wedge и проверяет datagram 256, 257, 280 и 1460 bytes, следующий
  heartbeat и partial reads;
- host test проходит с ASan/UBSan;
- `pio ci` успешно собирает default UDP для ESP32 Arduino core 3.3.8 и ESP8266
  core 3.1.2; отдельная ESP32 AT-mode сборка включает все три UDP handlers.

Осталось до `FIXED`: прогон на ESP32 core 3.3.10 и hardware/network smoke с
реальными UDP datagrams.

### MLRS-007 — недостижимый recovery после unexpected IRQ

**Приоритет:** P1  
**Статус:** IMPLEMENTED

В нескольких ветках сначала вызывается fatal, а ниже расположен код очистки
IRQ и восстановления state:
[`mlrs-rx.cpp:679`](../mLRS/CommonRx/mlrs-rx.cpp#L679),
[`mlrs-tx.cpp:890`](../mLRS/CommonTx/mlrs-tx.cpp#L890).

Комментарии рядом признают поздний `RX_DONE` возможным при плохой связи, то
есть это не невозможный invariant.

Definition of done: stale `RX_DONE`, `TX_DONE` и `TIMEOUT` fault injection
очищает IRQ, переармирует radio и не вызывает fatal.

Реализовано:

- ожидаемые IRQ bits потребляются отдельно, а любой остаток учитывается как
  recoverable radio error без `FAIL`;
- RX возвращается в `RECEIVE`, TX — в `IDLE`, после чего radio переармируется
  штатной state machine;
- после трёх consecutive errors выполняется bounded radio reinitialization;
  total/reinit/failure counters остаются доступными в debugger и готовы для
  последующего вывода в telemetry.

Осталось до `FIXED`: аппаратная fault injection для stale `RX_DONE`, `TX_DONE`
и `TIMEOUT` на single- и dual-radio targets.

### MLRS-008 — потеря IRQ из-за read/process/clear race

**Приоритет:** P1  
**Статус:** IMPLEMENTED

ISR присваивает новое значение `irq_status`, main loop отдельно читает и затем
обнуляет его. `volatile` не делает эту последовательность атомарной. IRQ,
пришедший между чтением и очисткой, может быть затёрт.

Исправление: pending bitmask/event counter и atomic exchange; hardware IRQ
читается в main context.

Definition of done: stress test одновременных и повторных DIO events не теряет
ни одного события и не приводит state machine в невозможное состояние.

Реализовано:

- ISR использует отдельный saturating `uint8_t` counter на каждое radio;
- main забирает counter атомарно: ESP32 через critical mux, ESP8266 через
  `noInterrupts()`/`interrupts()`, STM32 через IRQ disable/enable;
- hardware IRQ очищается только по прочитанному snapshot, поэтому новый
  отличный IRQ bit не стирается маской `ALL`;
- host regression проверяет накопление, однократный drain и saturation без
  wrap; source regression проверяет snapshot-clear для radio wrappers.

Осталось до `FIXED`: аппаратный DIO burst/stress на single- и dual-radio
targets. Счётчик предотвращает software race, но не обещает очередность двух
одинаковых hardware bits внутри одного radio IRQ latch.

### MLRS-009 — TCP backpressure и starvation

**Приоритет:** P1  
**Статус:** IMPLEMENTED
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

Реализовано:

- в обоих направлениях добавлены фиксированные очереди по 2 KiB и budget не
  более 256 bytes на один этап обработки за проход loop;
- входной источник не читается при заполненной очереди, а partial write
  удаляет только фактически записанные bytes и продолжает с прежней позиции;
- ESP32 пишет в socket через `send(..., MSG_DONTWAIT)`, ESP8266 проверяет
  `availableForWrite()` и использует timeout 1 ms;
- pending bytes старого соединения явно отбрасываются при disconnect/reconnect
  и учитываются счётчиком, поэтому они не попадают следующему TCP client;
- добавлены saturating counters requested/written, partial/stall/error/full,
  write duration и high-water marks UART/очередей;
- host test с ASan/UBSan проверяет budget, заполнение, partial writes, wrap и
  saturation; source regression запрещает возврат unbounded
  `while(client.available())`;
- полный AT-mode bridge собран для ESP32 Pico/ESP32-C3 Arduino Core 3.3.8 и
  ESP8266 Core 3.1.2, поэтому обе platform-specific TCP write ветки реально
  проходят compiler/linker;
  обнаруженная ESP8266-зависимость от автогенерации прототипа устранена явным
  объявлением `setup_wifipower()`.

Осталось до `FIXED`: hardware bidirectional soak 0.2/1/10 s с sequence/hash и
UART overflow telemetry. Фиксированная очередь ограничивает starvation и
корректно передаёт backpressure, но не обещает бесконечно сохранять serial
stream, если удалённый client не читает и у UART нет flow control.

### MLRS-010 — нет execution budget в MAVLink/MSP path

**Приоритет:** P1  
**Статус:** IMPLEMENTED
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

Реализовано:

- семь входных MAVLink/MSP loops на TX и RX используют общий одноразовый
  budget и обрабатывают не более 64 bytes за вызов;
- parser state и непрочитанные FIFO/UART bytes сохраняются, поэтому длинный
  frame продолжается на следующих итерациях main loop без изменения wire
  format, payload capacity или baud rate;
- host regression проверяет точную границу 64 bytes, garbage prefix, два
  последовательных максимальных MAVLink frames и максимальный 768-byte MSP
  payload без потерь, дублирования и перестановки;
- source regression закрепляет budget во всех семи ранее неограниченных loops.
- PlatformIO builds проходят для ESP8266 RX и ESP32 TX, WLE5 RX/TX builds — с
  Arm GNU Toolchain 11.3.Rel1 и 14.3.Rel1.

Осталось до `FIXED`: DWT/GPIO histogram и hardware soak на WLE5. Code-only
контракт ограничивает число parser steps, но без аппаратного измерения не
выдаётся за подтверждённый worst-case execution time.

### MLRS-011 — silent partial frame enqueue

**Приоритет:** P1  
**Статус:** IMPLEMENTED

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

Реализовано в parent repo:

- `tFifo::PutBuf()` сначала проверяет capacity и возвращает `bool`; при нехватке
  места FIFO и существующие данные не меняются;
- отказ `Put()`/`PutBuf()` увеличивает saturating `OverflowCount()`, который
  сбрасывается вместе с FIFO через `Init()`;
- ASan/UBSan host regression перебирает все fill levels малой очереди,
  oversized frames и wraparound, проверяя контракт all-or-nothing и порядок.

Реализовано в submodule `mLRS/modules/stm32ll-lib`:

- hardware UART `uart_putbuf()` и его generated варианты сначала вычисляют
  свободную ёмкость ISR ring buffer, затем либо ставят весь buffer одним
  commit `txwritepos`, либо возвращают `0`, не меняя очередь;
- тот же контракт применён к software UART `swuart_putbuf()`;
- API возвращает принятый `len` либо `0`, а отдельный saturating overflow
  counter доступен через `*_tx_overflow_count()` и сбрасывается при `Init()`;
- generator regression проверяет идентичность шести generated headers, source
  ordering и all-or-nothing ring model для всех fill levels малой очереди;
- STM32 builds с hardware UART и SWUART проходят на G4 и WL.

До `FIXED` остаётся аппаратный burst/overflow test, подтверждающий отсутствие
partial frame на проводе и корректное увеличение counter.

### MLRS-012 — adaptive retry всегда равен одному

**Приоритет:** P1  
**Статус:** FIXED

В исходном снимке после выбора 1–3 retries безусловно выполнялся
`SetRetryCnt(1)`:
[`arq.h:192`](../mLRS/Common/arq.h#L192).

Самостоятельно выпускать эту правку было нельзя: она чаще активировала
MLRS-002/003. Теперь она включена вместе с новым ACK/resync protocol.

Definition of done после protocol fix:

- FLRC: 699→1, 700→2, 799→2, 800→3;
- остальные modes: 799→1, 800→2;
- unknown mode→1.

Реализовано: `SetRetryCntAuto()` возвращается после выбранной mode branch и
больше не затирает результат финальным `SetRetryCnt(1)`. Host test проверяет
все перечисленные threshold boundaries.

### MLRS-013 — retry budget меняется внутри payload

**Приоритет:** P1  
**Статус:** IMPLEMENTED

В исходном снимке `SetRetryCntAuto()` вызывался и для fresh frame, и после
retransmission:
[`mlrs-rx.cpp:345`](../mLRS/CommonRx/mlrs-rx.cpp#L345). Поэтому около порогов
700/800 один payload может начать с лимитом 2–3, а закончить с лимитом 1.

Исправление: вычислять и фиксировать budget только при создании fresh payload.

Definition of done: изменение link metric во время retries не меняет budget
текущего payload, но применяется к следующему.

Реализовано: `SetRetryCntAuto()` вызывается только в fresh-payload branch;
retransmission сохраняет budget, выбранный при создании payload. До `FIXED`
остаётся hardware/timing проверка около thresholds 700/800.

### MLRS-014 — UART buffer allocation failure игнорируется

**Приоритет:** P1  
**Статус:** IMPLEMENTED

Первоначальная формулировка была неточной. В проверенных Arduino cores вызов
`setRxBufferSize(2048)` до `begin()` не выделяет память: он сохраняет requested
size и возвращает его. ESP32 `setTxBufferSize(512)` ведёт себя так же. Реальное
выделение происходит позже внутри `begin()` (`uart_driver_install()` на
ESP32, цепочка `malloc()` в `uart_init()` на ESP8266). Поэтому вывод
`2048/512` в
[`mlrs-wireless-bridge.ino:1212`](../esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino#L1212)
подтверждает только принятую конфигурацию, но не успешный старт UART.

Bridge после `SERIAL.begin()` не проверяет `operator bool()`. Ошибка проявится
в следующих случаях:

1. Для RX нужен contiguous block около 2 KiB, а для ESP32 дополнительно TX
   ring 512 bytes, event queue и driver structures. При малом либо
   фрагментированном heap `begin()` может не установить driver. Риск выше в
   custom builds, после дополнительных ранних allocations или при повторной
   инициализации; в стандартном setup UART стартует рано, поэтому вероятность
   мала, но failure path остаётся реальным.
2. Если выбранный `SERIAL` уже запущен, setters возвращают `0` и новые размеры
   не применяются. Текущие штатные board/debug mappings обычно используют
   разные UARTs, но custom mapping или ранний `Serial.begin()` активирует этот
   сценарий.
3. Ошибка pins/config/driver install даёт тот же внешний результат, хотя это
   уже не allocation failure: bridge всё равно продолжает setup без рабочего
   UART.

Последствия зависят от core:

- ESP8266 core 3.1.2 оставляет `_uart == nullptr`; `available/read/write`
  превращаются в безопасные `0`, и Wi-Fi bridge внешне запускается, но serial
  traffic бесследно не проходит;
- проверенный ESP32 Arduino core 2.0.17 содержит дополнительный defect в error
  path: после `uartEnd()` указатель обнуляется, а затем разыменовывается в
  diagnostic log. Allocation/driver failure поэтому может дать crash/reboot
  ещё внутри `begin()`, до проверки из sketch;
- в текущем upstream Arduino-ESP32 порядок исправлен: `begin()` возвращает с
  `_uart == nullptr`, а `HardwareSerial::operator bool()` сообщает, установлен
  ли driver. Сам setter по-прежнему не является allocation probe.

Варианты исправления:

1. **Fail closed (рекомендуется).** Сначала pin/upgrade Arduino-ESP32 core, где
   error path не разыменовывает `nullptr`; после setters проверить requested
   sizes, после `begin()` — `if (!SERIAL)`, вывести distinct diagnostic/LED
   code и не запускать protocol handler. Это не создаёт bridge, который
   выглядит живым, но молча теряет весь serial stream.
2. **Явный degraded mode.** До `begin()` проверить largest free block как
   advisory signal, при нехватке выбрать заранее определённую ступень
   RX `2048 -> 1024 -> 512` и ESP32 TX `512 -> 0`, затем обязательно проверить
   `operator bool()`. Активный размер/degraded state и UART overrun/drop
   counters должны быть доступны в диагностике. Простое снижение baud rate не
   уменьшает allocation и само по себе не является fallback; без согласования
   producer rate оно, наоборот, дольше держит bytes в очереди.
3. **Снизить вероятность.** Перенести UART init до Preferences/String и
   protocol-specific initialization. Это улучшает шанс получить contiguous
   block, но не заменяет observable success check.
4. **Убрать dynamic driver allocation.** Собственный ESP-IDF/static UART path
   делает память предсказуемой, но заметно увеличивает maintenance scope и
   оправдан только если fail-closed/degraded policy недостаточна.

Definition of done: закреплён core без crash в failure path; fault injection
для setter-after-begin и failed allocation; setup либо получает требуемые
buffers, либо входит в явно наблюдаемый degraded mode, либо прекращает запуск
bridge с понятной ошибкой.

Реализован выбранный fail-closed policy:

- production README закрепляет ESP32 Arduino core 3.3.11, а compile guard не
  допускает затронутый ESP32 core 2.x;
- requested RX 2048 и ESP32 TX 512 проверяются по результатам setters;
- после `begin()` проверяется `HardwareSerial::operator bool()`;
- protocol handler инициализируется только после успешного UART startup;
- ошибка навсегда останавливает setup, выдаёт `FATAL: serial startup error N`
  на отдельный debug UART, если он доступен, и повторяет LED-код: один импульс
  для RX config, два для TX config, три для driver/allocation failure;
- host fault-injection test моделирует setter-after-begin (`0`) и неуспешный
  driver allocation для ESP32 и ESP8266;
- compile/link проверены для ESP32-C3 и ESP32 Pico на Arduino core 3.3.8, а
  также для production ESP8266 mapping на core 3.1.2; PlatformIO probe для
  ESP8266 использовал explicit forward declaration, которое Arduino IDE
  обычно генерирует автоматически.

До статуса `FIXED` остаётся hardware-проверка boot и диагностической индикации
на ESP32-C3/ESP32 и ESP8266, включая принудительный failure path.

### MLRS-015 — `run_setup.py` не автоматизируем

**Приоритет:** P2  
**Статус:** IN_PROGRESS
**GitHub:** [PR #228](https://github.com/olliw42/mLRS/pull/228)

Проблемы исходного снимка:

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

Реализовано:

- child scripts запускаются тем же `sys.executable`, которым запущен setup,
  поэтому venv/interpreter не теряется;
- при non-TTY setup больше не вызывает `input()`; для интерактивного запуска
  добавлен явный `--no-pause`/`-np`;
- child failure завершается через `sys.exit(1)` без попытки читать закрытый
  stdin;
- [`tests/host/test_run_setup.py`](../tests/host/test_run_setup.py) проверяет
  interpreter propagation, non-interactive success и failure paths.

Dependency policy выбрана: корневой
[`requirements-tools.txt`](../requirements-tools.txt) фиксирует версии и
SHA-256 для `empy`, `pexpect`, `ptyprocess` и `dronecan`. Установка с
`--require-hashes` и полный `--copy --mavlink --dronecan` setup проверены в
чистом Linux venv. Открыты Windows setup и optional `lxml`: без него
fastMAVLink generator работает, но предупреждает, что XML validation отключена.

### MLRS-016 — generator сообщает успех после exception

**Приоритет:** P2  
**Статус:** FIXED

`fmav_generate_c_library.py` сначала удаляет `out`, а при exception вызывает
`exit()` без ненулевого кода:
[`fmav_generate_c_library.py:52`](../mLRS/Common/mavlink/fmav_generate_c_library.py#L52).

Definition of done: failure сохраняет понятную диагностику, завершается
non-zero и не может быть принят wrapper/CI за успешную генерацию.

Реализовано:

- exception diagnostic направляется в `stderr`;
- generator завершает failure через `sys.exit(1)`, поэтому `run_setup.py` и CI
  видят non-zero child status;
- [`tests/host/test_fmav_generator.py`](../tests/host/test_fmav_generator.py)
  проверяет success path и exception path с точным exit code.

### MLRS-017 — STM32 build допускает false success

**Приоритет:** P2  
**Статус:** IMPLEMENTED

В исходном снимке return codes игнорировались для:

- compile: [`run_make_firmwares.py:756`](../tools/run_make_firmwares.py#L756);
- link: [`run_make_firmwares.py:828`](../tools/run_make_firmwares.py#L828);
- size/objcopy: [`run_make_firmwares.py:888`](../tools/run_make_firmwares.py#L888).

Дополнительно результаты `ThreadPoolExecutor.map()` не потребляются. Скрипт
может закончиться кодом 0 после неполной/неуспешной сборки.

Definition of done: любая compile/link/objcopy failure немедленно даёт
non-zero; проверяется наличие и размер каждого ожидаемого artifact; CI имеет
negative test с намеренно сломанным source/flag.

Реализовано:

- compile, link, size и objcopy проверяют return code и прерывают сборку через
  `RuntimeError` при любом non-zero;
- результат `ThreadPoolExecutor.map()` потребляется, поэтому exception из
  compile worker не теряется и link не запускается;
- после compile, link и objcopy каждый ожидаемый `.o`, `.elf`, `.hex` или
  `.elrs` должен существовать и иметь ненулевой размер;
- [`tests/host/test_stm32_build_failures.py`](../tests/host/test_stm32_build_failures.py)
  без STM32 toolchain проверяет non-zero child exit, missing/empty artifact и
  остановку до link при exception из parallel compile.

Полная STM32 matrix 55/55 проходит с реальными GCC 11.3.Rel1 и 14.3.Rel1. До
`FIXED` остаётся CI negative test с намеренно сломанным source/flag.

### MLRS-018 — PR CI покрывает не все build surfaces

**Приоритет:** P2  
**Статус:** IMPLEMENTED
**GitHub:** [PR #155](https://github.com/olliw42/mLRS/pull/155)

В исходном снимке на `main` не было `.github/workflows`. Старый PR #155:

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

Реализован первый рабочий CI layer:

- [`.github/workflows/stm32-toolchains.yml`](../.github/workflows/stm32-toolchains.yml)
  запускается на `pull_request`, `main` push и вручную;
- generator dependencies и архивы Arm GNU 11.3.Rel1/14.3.Rel1 закреплены
  версиями и SHA-256, setup выполняется non-interactive;
- PR gate собирает F1/F3/G4/WL smoke matrix с USB, ELRS bootloader и SiK
  variants и запускает host regression suites; manual input включает полную
  matrix из 55 targets;
- size reports публикуются как job summary и отдельные artifacts;
- workflow проходит локальную проверку `actionlint`, а его setup/build команды
  воспроизведены на чистом Linux venv.

Добавлен второй CI layer:

- [`.github/workflows/esp-builds.yml`](../.github/workflows/esp-builds.yml)
  запускается на `pull_request`, `main` push и вручную с read-only permissions;
- PlatformIO Core закреплён на `6.1.19`; firmware job генерирует fastMAVLink,
  запускает общий host suite, fail-fast собирает 32/32 ESP environments и
  требует ровно 32 непустых опубликованных binaries;
- отдельный bridge job изолирован от official ESP Core 2.x matrix и собирает
  три AT-mode variants: ESP32-C3, ESP32 Pico и ESP8266; AT-mode включает все
  доступные для платформы TCP/UDP/UDPSTA/UDPCl/BLE/BT/ESP-NOW handlers;
- [`esp/mlrs-wireless-bridge/platformio.ini`](../esp/mlrs-wireless-bridge/platformio.ini)
  закрепляет pioarduino Arduino Core 3.3.8, ESP8266 Core 3.1.2, `no_ota.csv` и
  ESP8266 Preferences 2.2.2;
- workflow проходит `actionlint` и четыре source regressions; точные bridge
  команды прошли в полностью новом `PLATFORMIO_CORE_DIR`, а firmware runner —
  полную локальную matrix 32/32.

Осталось до `FIXED`: первый реальный GitHub Actions run, включение обоих job как
required branch-protection checks и Windows setup validation.

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
**Статус:** IN_PROGRESS

В исходном снимке основного проекта был найден только ручной
[`Common/test.h`](../mLRS/Common/test.h). Не было host tests для
ARQ/frame/FIFO/MAVLinkX, hardware recovery suite, bridge build matrix и timing
regression tests.

Минимальная программа:

1. Host: ARQ state/property tests, MAVLinkX ASan, FIFO atomicity, frame
   pack/check.
2. ESP: все PlatformIO targets плюс отдельные bridge protocol/board variants.
3. STM32: representative MCU families и полный release matrix.
4. Hardware: RP4-TD attenuation/recovery, WLE5 timing, TCP/UDP bridge soak.

Definition of done: эти suites являются required PR checks, а hardware tests
сохраняют raw counters/logs и привязаны к exact firmware hash.

Реализовано:

- единый [`tests/host/run_host_tests.sh`](../tests/host/run_host_tests.sh)
  fail-fast запускает Python discovery и sanitizer suites;
- host regression покрывает malformed/overflow MAVLinkX, UDP datagram drain,
  ARQ state/property invariants, generator exit semantics, non-interactive
  setup, STM32 build failure propagation, атомарный IRQ handoff и recovery
  threshold, а также bounded MAVLink/MSP parsing с продолжением frame между
  вызовами;
- source regressions запрещают radio/SPI work в DIO ISR, fatal sync mismatch,
  очистку непрочитанных IRQ bits и бесконечные BUSY waits.
- отдельный PR workflow запускает host suite, полную ESP matrix 32/32 и три
  AT-mode bridge variants на ESP32-C3, ESP32 Pico и ESP8266; каждый ожидаемый
  binary проверяется на наличие и ненулевой размер.

Открыто: отдельные frame suites, первый реальный GitHub run/required-check
policy и hardware/timing tests из минимальной программы выше.

### MLRS-021 — STM32 toolchain искусственно зафиксирован на GCC 11

**Приоритет:** P2
**Статус:** IMPLEMENTED
**GitHub:** [issue #159](https://github.com/olliw42/mLRS/issues/159)

В исходном снимке [`glue.h`](../mLRS/Common/hal/glue.h) безусловно запрещал
`__GNUC__ > 11`, а `findSTM32CubeIDEGnuTools()` пропускал все CubeIDE plugins
с GCC >=12. Standalone compiler из `PATH` не проверялся заранее: новый GCC
доходил до compile и падал только на `#error`.

Guard появился в апреле 2024 после реального runtime defect из #159: GCC 12
firmware мог уронить TX при MAVLinkX, активном serial stream и особенно
230400 baud. Это не была compile error. В марте 2025 тот же reporter получил
двухчасовой стабильный прогон current code и с GCC 13, и повторно с GCC 12;
в марте 2026 issue закрыт как исчезнувший после redesign. Guard после этого
не пересматривался.

Первичный Debian GCC 14.2 probe показал рост `.text` до 4.12%, но точный
официальный Arm GNU 14.3.Rel1 дал другой результат. После полного setup оба
compiler lines собрали 55/55 STM32 configurations, включая F1/F3/G4/L4/WL,
RX/TX, USB, ELRS bootloader, SiK telemetry, DroneCAN и diversity/dual-band
variants. Сравнение одинаковых `-Os`, `gnu11`/`gnu++14` builds:

| Target | GCC 11.3 `.text` | GCC 14.3 `.text` | Изменение |
|---|---:|---:|---:|
| `rx-matek-mr24-30-g431kb` | 55 084 | 54 752 | -0.60% |
| `rx-R9M-f103c8` | 53 692 | 53 276 | -0.77% |
| `rx-R9MLitePro-v15-f303cc` | 52 052 | 51 588 | -0.89% |
| `rx-matek-mr900-22-wle5cc` | 56 656 | 56 196 | -0.81% |
| `tx-matek-mr24-30-g431kb-default` | 100 324 | 99 812 | -0.51% |

То есть официальный 14.3 не только source-compatible с полной matrix, но и
слегка уменьшает `.text` на representative targets. Это code/build evidence;
оно не является доказательством поведения реального радио.

Что изменилось в актуальной линии Arm GNU:

- Arm публикует ветки 12.3, 13.3, 14.3, 15.2 и 15.3; новые releases начиная с
  15.3.Rel1 перенесены на официальный Arm GitLab. GCC, Binutils, GDB и newlib
  существенно новее, чем в 11.3.Rel1;
- GCC 12/13/15 уменьшили число случайных transitive C++ includes, поэтому
  legacy code чаще требует явных headers; GCC 14 строже отклоняет старые C
  implicit declarations и несовместимые pointer conversions;
- GCC 15 по умолчанию переключил C на `gnu23`, но STM32 build script уже явно
  задаёт `-std=gnu11` и `-std=gnu++14`, поэтому этот default проект не меняет.
  Риски перехода здесь — optimizer/code layout, размер, новая newlib/binutils
  и hardware timing, а не смена language dialect.

Реализованный code-only переход:

1. Arm GNU 11.3.Rel1 оставлен reproducible reference; официальный Linux архив
   14.3.Rel1 закреплён SHA-256
   `8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd`.
   Windows ZIP закреплён SHA-256
   `864c0c8815857d68a1bbba2e5e2782255bb922845c71c97636004a3d74f60986`.
2. Build script использует строгий parser: `--help` безопасно выводит справку,
  неизвестные параметры и параметры без значения завершаются с code 2;
3. Добавлены `--toolchain-dir DIR` и совместимый alias `--toolchain DIR`;
  каталог проверяется на наличие `gcc`, `g++`, `size` и `objcopy`, а перед
  сборкой печатается фактическая строка версии `arm-none-eabi-gcc`;
4. Target с нулём совпадений завершается с code 2 до выбора toolchain и очистки
  `tools/build`, поэтому опечатка больше не выглядит успешной сборкой;
5. Старые aliases `-t`/`-T`, `-d`/`-D`, `-np`, `-v`/`-V`, автоматический поиск
  CubeIDE и fallback через `PATH` сохранены и покрыты host regression tests.
6. CubeIDE discovery принимает GCC 12–14, script отклоняет major >14 до
   очистки build, а broad `#error` в `glue.h` заменён верхней code-validated
   границей GCC 14. GCC 15 остаётся отдельным следующим migration step.
7. Dual-toolchain workflow выполняет pinned setup и smoke matrix на PR, умеет
   вручную запускать полные 55/55 builds и сохраняет size reports.
8. В двух NiceRF LR2021 linker scripts секции `.ARM*` и init arrays помечены
   `READONLY`; это устраняет обнаруженные GCC 14 `RWX LOAD segment` warnings.

Code-only Definition of done выполнен: pinned Linux/Windows 14.3 artifacts,
dual-toolchain build gate, полный local 55/55 result, size comparison,
обновлённый script и снятый broad GCC 11 guard.

Внешнее ограничение: hardware reproducer #159 выполнить негде. Поэтому runtime
поведение GCC 14 на STM32 TX с MAVLinkX/230400 baud остаётся `UNVERIFIED`, и
эта ветка не заявляет hardware/production acceptance.

### MLRS-022 — ESP build script допускает false success

**Приоритет:** P2
**Статус:** IMPLEMENTED

[`run_make_esp_firmwares.py`](../tools/run_make_esp_firmwares.py) жёстко задаёт
`C:/Users/Olli/.platformio/penv/Scripts`, не обрабатывает `--help` и неизвестные
аргументы, а return codes `platformio` игнорирует через `os.system()`.

Code-only reproduction на Linux: `python3 tools/run_make_esp_firmwares.py
--help` попытался выполнить отсутствующий Windows `platformio.exe`, после чего
продолжил copy stage и завершился с code 0. При наличии старого `.pio/build`
скрипт может переупаковать stale `.bin`, не собранные из текущих исходников.

Исправление:

- строгий `argparse` с безопасным `--help` и code 2 для invalid arguments;
- portable поиск `pio`/`platformio` либо явный `--platformio PATH`;
- `subprocess.run(..., check=True)` для clean/build и прекращение copy stage
  после первой ошибки;
- очистка/проверка ожидаемых environments и non-empty artifacts до копирования;
- negative host tests для missing tool, compile failure и stale build tree.

Definition of done: Linux/Windows runner либо создаёт полный набор текущих ESP
artifacts, либо завершается non-zero до copy stage; stale `.bin` не может быть
принят за успешную сборку.

Реализовано:

- CLI переведён на строгий `argparse`: `--help` не запускает build, invalid
  options получают code 2, aliases `-t`/`-T`, `-d`/`-D`, `-np`, `-v`/`-V`
  сохранены;
- `pio`/`platformio` ищется через `PATH`, а `--platformio PATH` принимает
  executable или содержащий его каталог;
- `--target` выбирает только точное environment из `platformio.ini`, а
  repeatable `--define` реально добавляется через `PLATFORMIO_BUILD_FLAGS`;
- clean и build выполняются через `subprocess.run()` с обязательным zero exit
  code; каталоги ожидаемых environments удаляются между clean и build, поэтому
  no-op clean не может сохранить stale `firmware.bin`;
- до публикации проверяется наличие и ненулевой размер каждого ожидаемого
  artifact; копирование сначала формирует staging set и только затем заменяет
  `tools/esp-build/firmware`;
- detached checkout больше не создаёт пустой branch suffix и двойной дефис в
  имени; dev version по-прежнему получает текущий Git hash;
- 10 host regressions покрывают CLI, aliases/defines, missing tool, unknown
  target, build failure, no-op/stale tree, incomplete artifact set и detached
  checkout;
- Linux end-to-end runner успешно пересобрал все 32/32 environments и только
  после общей проверки опубликовал ровно 32 непустых binaries.

Осталось до `FIXED`: тот же runner должен пройти на Windows. До этого
platform-specific acceptance сохраняется как `IMPLEMENTED`, а не `FIXED`.

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
- Результаты `setRxBufferSize()`/`setTxBufferSize()` до `begin()` — requested
  sizes, а не подтверждение успешного выделения UART buffers.

## Рекомендуемый порядок работ

Текущий рабочий scope — только код и автоматические проверки:

1. MLRS-015/018/020: проверить workflows на GitHub, включить required checks,
   завершить frame coverage и Windows setup.
2. MLRS-019: запретить antenna2-only на всех code/API boundaries либо явно
   документировать limitation.

Hardware validation MLRS-002–008, MLRS-011/014 и runtime acceptance MLRS-021
перенесены во внешний `UNVERIFIED` backlog: в текущем контуре выполнить их
невозможно.

До закрытия P0 итоговый статус dev `v1.4.03`: **NO-GO для production fork**.
