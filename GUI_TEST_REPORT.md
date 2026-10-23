# GUI validation report

Updated 2026-10-23. Target: C:\Users\Admin\Desktop\Code\webCAn\webCAn.

## Build

Ran PlatformIO run -e esp32-s3-devkitm-1 in the actual target using C:\Users\Admin\.platformio\penv\Scripts\platformio.exe.

SUCCESS, 22.29 seconds. Espressif32 8.0.0, Arduino ESP32/WebServer/WiFi 3.3.12, Xtensa toolchain 14.2.0+20260121. No package/configuration changes.

| Measurement | Original | Standalone UI before log | Current |
| --- | ---: | ---: | ---: |
| Static RAM / 327680 | 52100 | 55940 | 66212 (20.2%) |
| Application flash / 6553600 | 1029698 | 1055142 | 1064526 (16.2%) |

Final firmware.bin: 1,082,096 bytes. Event logging adds 10,272 static RAM bytes: 8192-byte ring, 2048-byte batch and cursor/session/lock storage/alignment. Log JSON temporarily reserves 12 KB; dashboard snapshot still reserves 32 KB. WebServer processes requests serially. Actual heap margin/transfer latency need hardware testing.

Same four existing volatile-increment warnings for heartbeat/recovery and door counters; no errors.

Installed main.cpp SHA-256: C88DF8F87A78CB96D94815CA6FF6CF43B39586E2EFC3A3A62FBA85660D3EF3AE.

Unchanged platformio.ini SHA-256: 2A99CB21EF586857928F4A70918E8AEAB1F489E428CCA8DFC13DC19F0DA10CA5.

## Source/protocol preservation

PASS: restore previous MOTOR_HTML and remove exactly log core/handler, three capture hooks, session initialization and GET route; remainder matches pre-log source byte-for-byte. Both legacy HTML strings unchanged.

Capture follows successful twai_receive and successful twai_transmit queue acceptance. Failed TX is not logged as accepted. No wire bytes, ID/DLC/flags, driver calls/timeouts, bitrate, task configuration, heartbeat cadence/recovery or guards changed.

Door protocol remains standard 0x100/DLC 2 commands, 0x101/DLC 6 status, 0x102/DLC 1/A5 heartbeat, 0x103/DLC 8 travel. Phase 4A remains unimplemented and motor commands disabled.

## Native C++ checks — passed

Compiled actual event-core and handler fragments with installed g++ C++17/threads. Host mutexes replace portMUX and String/WebServer/TWAI types have stubs; tests do not execute ESP32 drivers or FreeRTOS critical sections.

- Empty/incremental reads; repeated same-ID/same-ms events; no duplicate replay.
- RX/TX, EXT/RTR, DLC clamp and RTR payload clearing.
- Exact overwrite lost count, 64-entry pagination, independent readers, future cursor recovery and boot-session reset.
- Sequence beyond 32 bits; timestamp wrap.
- Three concurrent producers, 30,000 events; retained sequences/payloads remain consistent.
- Actual handler JSON fields and invalid negative/nonnumeric/fractional/overlong/JavaScript-unsafe cursor rejection.

## Browser checks — 34 passed

Node/Playwright with installed headless Chrome and in-memory API fixtures. Restricted browser downloads were canceled by the execution sandbox; the same harness reran with approved local download access and successfully read the exported CSV.

Regression coverage: raw add/save/send/delete/clear/bitrate, STD/EXT/RTR, busy/cyclic locks, overlapping GET race, unsupported motor controls, stale/offline/controller states, no command POST on open/refresh/reconnect, Monitor filters/pause, draft preservation, escaped content and full 120 RX/40 TX tables.

New log coverage:

1. Diagnostics removed; CAN Log last and initially collapsed.
2. Repeated ID/timestamp events and heartbeat TX stay distinct.
3. TX/RX filter and actual downloaded CSV timestamp/direction/payload.
4. Clear affects only local history; cursor retained without replay or CAN/job mutation.
5. Refresh persistence and incremental deduplication.
6. Device reboot resets cursor and distinguishes boot histories.
7. Ring overwrite exact missed count and paginated catch-up.
8. At most 2000 retained / 200 rendered; storage quota failure still permits export.
9. Offline history retained; reconnect/log actions produce no POST.

Final run: 54 dashboard GETs, maximum one outstanding snapshot. Log has a separate one-request guard. No browser exceptions/external dependencies. Desktop/mobile screenshots visually reviewed; no page-wide overflow at 1440/768/390/320 px. Keyboard check is a smoke test.

## Log semantics/limits

- Firmware retains latest 256 events continuously in volatile RAM; no permanent flash/SD storage.
- Browser retains latest 2000 in localStorage for that origin. Storage restrictions/data deletion can prevent persistence.
- Timestamp is uint32 device uptime in milliseconds, not absolute date/time. CSV boot and sequence distinguish reboots/capture order.
- TX means driver queue acceptance, not remote ACK or measured wire timestamp.
- CSV includes all retained records matching the direction filter, independent of the 200-row display limit.
- Device overwrite and browser-retention removals are reported separately. Saturated-bus logging is not guaranteed lossless.

## Physical verification still required

No hardware was flashed or operated.

1. Upload configured build, open /motor, verify Diagnostics absent/CAN Log last and legacy pages intact.
2. Compare real STD/EXT/RTR bursts, repeated IDs and all DLCs against an independent analyzer.
3. Check manual/cyclic/door/heartbeat events, failed TX exclusion, Add/Save not sending, unchanged bytes and 200 ms heartbeat.
4. Check refresh/reconnect/reboot, CSV for all/RX/TX, local clear, persistence and no automatic commands or job changes.
5. Overflow device/browser history; verify gaps, retention notices and responsiveness.
6. Measure heartbeat/cyclic jitter, RX missed/errors, minimum heap and watchdog/task behavior under sustained traffic and multiple clients, log expanded/collapsed. Native/browser tests do not establish real-time ESP32 behavior.
7. Recheck bitrate restart/bus-off recovery and original state guards. Keep unsupported motor actions disabled.

## Local test artifacts

Outside the repository under C:\Users\Admin\.codex\visualizations\2026\10\23\01a16764-360d-7bd0-acef-a6ea1fad65e7\webcan-modernization:

- test-can-log.cpp/exe: native actual-core/handler tests.
- test-dashboard.cjs, browser-test-results.json: 34 browser checks.
- dashboard-desktop.png, dashboard-mobile.png: reviewed previews.
- before-log-main.cpp, main.cpp: before/current source.

Harness fragments/scripts are external; shipped implementation remains entirely in src/main.cpp.
