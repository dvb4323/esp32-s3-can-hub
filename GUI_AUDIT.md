# GUI audit — before implementation

Audited 2026-10-23 at `C:\Users\Admin\Desktop\Code\webCAn\webCAn`. No source changes preceded this audit. Line references below refer to the original main.cpp.

## Repository and stack

- `platformio.ini`: sole environment `esp32-s3-devkitm-1`, espressif32 / Arduino, serial 115200. Installed platform is 8.0.0; the project does not pin it.
- `src/main.cpp`: entire application, two flash-resident raw HTML strings, inline CSS/JavaScript, synchronous Arduino `WebServer` on port 80. No LVGL, display driver, WebSocket, filesystem assets, or third-party GUI framework.
- `src/main.zip`: archived source; not a build input and will remain untouched.
- `include/`, `lib/`, `test/`: only PlatformIO instructional README files. `data/` does not exist.
- `.gitignore` ignores build/editor artifacts. `.vscode` contains generated include paths, debug launch settings, and extension recommendations; no extra firmware configuration. No applicable AGENTS.md found along the target directory ancestry or in src.
- This task's supplied working directory is a different S32K144 project. Changes must target the explicitly requested Desktop repository only. Git status currently fails with “this operation must be run in a work tree”; preservation will also be checked directly against a pre-edit source snapshot.

## Hardware and resources to preserve

ESP32-S3 at 240 MHz, flash 80 MHz QIO, configured 16 MB flash, OPI PSRAM 8 MB assumption, `qio_opi`, `default_16MB.csv`, `BOARD_HAS_PSRAM`. Physical module capacities are not confirmed by source. CAN TX GPIO 47 / RX GPIO 48, overridable existing macros. Normal-mode built-in TWAI, accept-all filter, TX queue 20, RX queue 50. Default 500 kbit/s; existing 125/250/500/1000 presets use TWAI timing macros. AP mode `TCOM_CAN`, fixed existing credentials, default softAP IP shown by firmware. None will change.

Fixed tables: 40 TX rows, 120 RX rows, 8 auto jobs. RX task: core 0, priority 10, stack 4096, receive timeout 50 ms. Heartbeat task: core 0, priority 8, stack 3072, period 200 ms. Arduino loop serves HTTP, schedules auto TX, delays 1 ms. `rxMutex` guards RX and door snapshots. Existing state JSON reserves 32 KB and serializes RX while holding this mutex, a contention risk under traffic. New monitoring must avoid extending this critical section.

## Original feature and API map

| Feature | Browser functions/state | Firmware handlers/state |
| --- | --- | --- |
| Default page `/` | `poll` every 400 ms; `connect`, `canState`, counters, IP/build | `handleRoot`, `INDEX_HTML`; GET `/api/state` -> `handleState` |
| Bitrate selection | `setPresetUi`, `applyConfig`, `userEditingConfig` | POST `/api/config` -> `handleConfig`, `restartCan`, `currentBitrate`, `canReady` |
| Compose/edit TX | `txDraftParams`, `addTxRow`, `saveTxRow`, focus guards, `editingTx`, `selectedTx` | POST `/api/tx/add`, `/update` -> `handleTxAdd`, `handleTxUpdate`, `updateTxRowFromArgs`, `readFrameArgs`, `txRows` |
| Send/delete TX | `sendSelectedTx`, `deleteSelectedTx` | POST `/api/tx/send`, `/delete` -> `handleTxSend`, `handleTxDelete`, `sendTxRow` |
| Cyclic TX | `toggleTxAuto`, `setTxRowLock` | POST `/api/tx/auto` -> `handleTxAuto`, `txJobs`, `processAutoTx`; 8 jobs; minimum 10 ms; enables with immediate first send |
| Clear tables | `clearTx`, `clearRx` | POST `/api/clear_tx`, `/api/clear_rx`; TX clear also stops all auto jobs; counters reset |
| RX aggregation | `renderRxRows`: type/ID/DLC/name/data/cycle/count | `canRxTask`, `updateRxRow`, `rxRows`, `rxTotal`; key ID+EXT+RTR, evicts oldest when full |
| Door page `/door` | `render`, `setCard`, 300 ms poll, 1000 ms freshness | `DOOR_HTML`, `handleDoorPage`, GET `/api/door/state` -> `handleDoorState`, `door101`, `door103` |
| Door actions | `sendCmd`, `busy`, `setBusy`; STOP exempt from busy lock; no automatic commands/retries | POST `/api/door/cmd` -> `handleDoorCmd`, fixed mapping, `doorCmdTotal` |
| Door diagnostics | Fault bit labels, obstacle latch from received data, PWM, status age, network errors | `appendDoorFrameJson`, bad-DLC counts, heartbeat sent/fail/recovery counters |

Default TX supports STD, EXT, STD RTR, EXT RTR; ID is hexadecimal, max standard 0x7FF, extended 0x1FFFFFFF. Payload accepts spaced, comma/semicolon-separated or compact hexadecimal; max 8 bytes. RTR sends DLC 0. Invalid cycle below 10 defaults to 1000 on row update. Auto rows cannot be edited/deleted while active. Manual transmission is deliberately a raw debug tool without motor semantics.

## Complete data flow

TX: explicit user action -> URL-encoded HTTP POST -> handler validates arguments -> `CanRow` -> `sendTxRow` -> `transmitFrame` -> `twai_message_t` with identifier/DLC/EXT/RTR flags and unchanged byte copy -> `twai_transmit` (up to 20 ms). Successful queue acceptance increments `txTotal`; row sends increment count/update lastMs. This does not establish remote application acknowledgement. Auto requests register deadlines; `processAutoTx` advances deadlines and skips catch-up bursts. Door sends use `transmitFrame` directly. Heartbeat uses `twai_transmit` directly with zero timeout; its totals are separate.

RX: transceiver/TWAI -> `canRxTask` waits on driver -> `millis()` timestamp -> DLC clamped to 8 -> `updateRxRow` under mutex -> latest payload and count/cycle per key -> optional standard non-RTR `updateDoorFrame` -> HTTP snapshot -> browser render. Multiple frames in a millisecond retain the previous nonzero cycle. Monitor is an aggregated latest-value table, not a lossless trace. Firmware currently retains lastMs but does not expose it in `/api/state`.

Heartbeat: independently sends standard `0x102`, DLC 1, `A5`, every 200 ms from startup. Detects BUS_OFF and initiates recovery; restarts STOPPED TWAI. This existing traffic must continue on page refresh/reconnect. Driver restart waits for RX/heartbeat busy flags, uninstalls, delays 20 ms, reinstalls selected timing.

## Verified application protocol (not Phase 4A)

| Direction / ID | Encoding |
| --- | --- |
| TX standard `0x100`, DLC 2 | STOP `00 00`; OPEN `01 50`; CLOSE `02 50`; LOCK `03 00`; UNLOCK `04 00`; CLEAR FAULT `05 00` |
| RX standard `0x101`, DLC 6 | Door state byte 0; lock byte 1; fault bitmask byte 2; PWM percent byte 3; remaining bytes retained raw |
| TX standard `0x102`, DLC 1 | `A5` heartbeat |
| RX standard `0x103`, DLC 8 | Travel frame retained raw by firmware; current door page does not interpret it |

Incorrect DLC increments the per-door-frame bad counter; extended/RTR frames never feed the door decoder. Door fault bits: obstacle, communication lost, bus-off, encoder/stall, travel timeout, limit switch, position unset, reset information. Door state names UNKNOWN/CLOSED/OPENING/OPENED/CLOSING/STOPPED are door-specific. They must not be presented as DC motor state.

No START_FORWARD/START_REVERSE, SET_DUTY, SET_MAX_DUTY, GET_STATUS implementation; no command sequence field, little-endian argument contract, ACK_NACK/MOTOR_STATUS/MOTOR_ERROR dispatch, or `0x180` decoder exists. Phase 4A shares `0x100` with incompatible existing door commands. Neither opcode numbers nor byte offsets are supplied for a safe integration. Motor controls, including motor STOP and CLEAR_FAULT, must be disabled with explanations. Existing operational Door STOP remains accessible at `/door`.

## Risks and exact change scope

1. Protocol collision: never send guessed Phase 4A commands or reuse door state as motor telemetry.
2. CAN RUNNING means local controller state, not connection to a motor or remote ACK. Display separately from browser connectivity and telemetry capability.
3. Browser disconnection cannot prove motor stopped. Requested duty/state/fault values must remain unavailable, never zero/healthy/stopped by inference.
4. Existing raw TX intentionally permits arbitrary identifiers and bytes; retain it in a clearly separated debug area and retain server guards.
5. HTTP server and auto scheduling share loop. Avoid added per-frame logs, serialization under RX lock, framework assets, timer fan-out, or blocking task changes. Existing 20 ms TX timeout and synchronous page transfers still require hardware load testing.
6. Existing cross-core volatile/state handling, RX eviction timestamp rollover, and heap/mutex behavior are outside this GUI change; no claim of fixing real-time guarantees.
7. Only `src/main.cpp` needs executable changes: add `MOTOR_HTML`, GET `/motor`, and a separate read-only dashboard snapshot endpoint exposing existing timestamps/counters. Preserve original HTML strings and existing functions byte-for-byte; do not alter `/api/state` schema.
8. Add `GUI_AUDIT.md`, `GUI_REDESIGN_PLAN.md`, `GUI_CHANGELOG.md`, `GUI_TEST_REPORT.md`. No config/dependency/pin/driver or archive modifications. Test harnesses may live outside the repository so all shipped implementation stays in main.cpp.
