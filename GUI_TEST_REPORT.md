# GUI validation report

Date: 2026-10-23. Target: `C:\Users\Admin\Desktop\Code\webCAn\webCAn`.

## Build

Ran the actual configured environment before modification and after implementation:

```powershell
& 'C:\Users\Admin\.platformio\penv\Scripts\platformio.exe' run -e esp32-s3-devkitm-1
```

Environment: Espressif32 8.0.0, Arduino ESP32 3.3.12 (`4.30312.261016`), Xtensa GCC toolchain `14.2.0+20260121`, WebServer/WiFi 3.3.12, release build. No package/config changes.

| Measurement | Original | Dashboard |
| --- | ---: | ---: |
| Static RAM | 52,100 / 327,680 bytes (15.9%) | 55,940 / 327,680 bytes (17.1%) |
| Application flash | 1,029,698 / 6,553,600 bytes (15.7%) | 1,055,142 / 6,553,600 bytes (16.1%) |
| Status | SUCCESS (15.96 s) | SUCCESS (22.58 s, standalone cleanup) |

Final firmware binary: `.pio/build/esp32-s3-devkitm-1/firmware.bin`, 1,072,592 bytes. Application flash increased by 25,444 bytes over the original firmware and decreased by 3824 bytes from the first dashboard. Installed main.cpp matches the browser-tested candidate, SHA-256 `5D83CBB5BB9D12E684404B5ED4D3DB2D7C270DE5D7A0CE8993BFC7E5AFD95100`. Unchanged platformio.ini SHA-256 `2A99CB21EF586857928F4A70918E8AEAB1F489E428CCA8DFC13DC19F0DA10CA5`.

Standalone cleanup: verified only MOTOR_HTML differs from the previously delivered main.cpp. Removed visible branding/title, navigation, banners and verbose copy; retained compact connection/stale indicators, disabled motor controls, accessible labels and all CAN actions. Screenshots were regenerated and visually reviewed at desktop/mobile sizes.

The 3840-byte RAM addition is the fixed 120-row snapshot. HTML/JavaScript are flash-resident; browser tables and filtering run on the client. The new JSON reserves 32 KB temporarily, like the existing state handler; actual heap margin and synchronous HTTP transfer latency need hardware measurement. The PlatformIO board summary reports an 8 MB board default while existing project overrides configure 16 MB; no memory/partition settings were changed. Confirm the physical module on the bench.

Both builds report the same four existing `-Wvolatile` deprecated increment warnings: hbRecover, hbSent, hbFail and doorCmdTotal. These original statements are unchanged.

## Source and protocol preservation

PASS: after removing exactly the new MOTOR_HTML section, dashboard handlers and two route registrations from the candidate, the entire remainder equals the original main.cpp snapshot byte-for-byte. This includes both original HTML strings, all original API handlers/JSON, CAN parser/transmitter/receiver, door mapping, heartbeat task and setup/loop. Target source is also compared with the tested candidate before delivery. No PlatformIO/configuration/dependency edits were made.

This verifies code preservation and successful compilation; physical behavior has not been exercised. No claim is made that a browser mock validates actual TWAI transmission or real motor operation.

Verified protocol stays standard 0x100 / DLC 2 for door commands, 0x101 / DLC 6 status, 0x102 / DLC 1 / A5 heartbeat, 0x103 / DLC 8 travel. Raw CAN byte encoding, ID bounds and RTR handling are unchanged. Phase 4A command sequencing, little-endian argument encoding and 0x180 response parsing do not exist and were not added.

## Browser validation — 25 automated checks passed

Ran Node/Playwright with installed Google Chrome headless. Served the extracted candidate dashboard and API fixtures through browser request interception; no ESP32 connected. Initial local-server navigation was denied by the execution sandbox, so requests were fulfilled in memory for the successful run. No external network resources are required. Syntax verified using `node --check dashboard.js`. Desktop and mobile screenshots were visually inspected.

1. Every original source byte preserved outside the additive sections/routes.
2. Initialization uses GET only; all seven motor commands, including STOP, stay disabled.
3. Local CAN status is distinct from unavailable motor telemetry and unknown motor faults.
4. Monitor displays timestamps, directions, DLC/payload; unsent TX rows excluded.
5. ID/direction filters.
6. Pause freezes presentation while snapshots continue.
7. Draft survives polling; all four frame types present.
8. RTR draft disables payload.
9. Add stores a row without sending; Save calls the original update API.
10. Send is explicit; busy locks prevent duplicate mutations.
11. Cyclic start locks editing/deleting; cyclic stop remains available.
12. Delayed pre-action GET cannot release the busy guard with an outdated post-action state.
13. Refresh reads active cyclic state and sends no POST or restart.
14. Delete, Clear RX and Clear TX use original endpoints.
15. Bitrate selection sends nothing; explicit Apply uses existing configuration endpoint.
16. CAN STOPPED, BUS_OFF, RECOVERING and NOT_READY plus error counters display without synthesized motor state.
17. Stopped/running/busy/fault-looking 0x180 bytes remain raw; no motor-state decoding is assumed.
18. Stalled snapshot watchdog marks STALE and disables raw mutations.
19. Disconnection labels old data; reconnect generates no POST.
20. Full 120 RX / 40 TX rows remain bounded through 20 repeated updates.
21. Concurrent refresh requests coalesce; maximum one in-flight snapshot GET.
22. Payload strings are escaped rather than interpreted as HTML.
23. No page-wide horizontal overflow at 1440, 768, 390 and 320 px; tables scroll internally.
24. Keyboard navigation smoke check; standalone dashboard has no links, visible page heading, logo or warning banners.
25. No browser exceptions, external dependencies or motor/door command POSTs during dashboard tests.

Final cleanup harness run: 52 snapshot GETs; maximum concurrency 1. All 25 checks passed. Tests exercise existing raw API contracts using mocks, not the live device. Timeout cleanup, ambiguous POST reporting and absence of retry were also inspected in source; real WiFi timeout behavior remains a hardware step. Keyboard check is a smoke test, not a full accessibility certification.

Test artifacts are outside the repository to honor the main.cpp-only implementation requirement:
`C:\Users\Admin\.codex\visualizations\2026\10\23\01a16764-360d-7bd0-acef-a6ea1fad65e7\webcan-modernization\`

- `test-dashboard.cjs`: reproducible mock harness (installed local Playwright/Chrome paths).
- `browser-test-results.json`: assertions, requests and concurrency result.
- `dashboard-desktop.png`, `dashboard-mobile.png`: reviewed previews.
- `original-main.cpp`: pre-edit snapshot; `main.cpp`: tested candidate.

## State coverage and limitations

| State / behavior | Verified without hardware | Still requires physical hardware |
| --- | --- | --- |
| Browser connected/disconnected/stale | Mock transport + watchdog, old-data labels and mutation locks | WiFi range/loss, AP reconnect, partial replies |
| Local CAN stopped/running/bus-off/recovering | Actual values rendered from snapshots; source/compile preservation | Driver state transitions and recovery on wired bus |
| Motor stopped/running/busy/fault/stale | Unsupported in every state; controls disabled and values unknown; raw 0x180 cannot fabricate telemetry | Verified future Phase 4A implementation; currently unavailable |
| Raw request busy / cyclic row guards | Browser locks + original server code unchanged | On-device concurrent clients and transmit errors |
| Refresh/reconnect | No command POSTs or motor restart; existing auto state restored visually | Existing device jobs/heartbeat remain continuous under page loads |
| High traffic | Full bounded tables + repeated browser updates; short RX snapshot lock by source inspection | RX loss, timing, heap and task latency under actual CAN saturation |

No real ESP32-S3 or S32K144 was flashed or operated during this task.

## Bench verification steps

1. Build/upload using the unchanged environment; confirm physical module/PSRAM/flash, GPIO 47 TX / 48 RX, transceiver and termination. Open `http://192.168.4.1/motor` on TCOM_CAN (use the serial-reported AP IP if different). Confirm `/` and `/door` still operate.
2. Capture the bus with an independent analyzer. On page open/refresh/reconnect, confirm no new application command frames; existing standard heartbeat 0x102 / DLC 1 / A5 continues every 200 ms. Verify no automatic motor start/restart or inferred direction change.
3. From Default CAN and the dashboard, add/save/send equivalent STD/EXT and RTR frames, including 0- and 8-byte payloads. Compare ID, flags, DLC and bytes. Confirm Add/Save alone sends nothing. Test malformed IDs/data and full TX table rejection. Driver acceptance must not be treated as remote application ACK.
4. Enable up to eight cyclic jobs with representative intervals (including 10 ms), verify timing using the analyzer, active edit/delete guards, explicit stop, Clear TX cancellation, and job preservation during browser refresh. Do not infer high-rate precision from the mock tests.
5. Feed RX bursts with repeated/mixed IDs, STD/EXT/RTR and all DLCs; verify timestamps/cycle/count, RX eviction at 120 keys and Clear RX. Monitor heartbeat sent/fail/recovery and TX/RX/bus/missed counters. Check sample rows against bus captures; this is an aggregated monitor, not a trace logger.
6. With the existing door setup, validate all six original command mappings and 0x101 DLC 6 / 0x103 DLC 8 freshness/bad-DLC behavior. Confirm Door STOP remains operable while another door request is busy. Do not substitute motor Phase 4A frames on the shared 0x100 ID.
7. Disconnect the remote node, induce bus-off/recovery safely on the bench, and exercise each existing bitrate preset. Confirm RX and heartbeat tasks retain their timing, driver restart behavior and queue settings.
8. Under sustained representative maximum CAN traffic, load/refresh all three pages from multiple browsers. Measure heartbeat inter-frame timing, cyclic jitter, RX missed/error counters, free/minimum heap, watchdog behavior and task responsiveness. Include WiFi dropout, slow clients, blocked requests and reconnect. Existing synchronous WebServer/legacy JSON behavior remains a possible throughput limitation.
9. Confirm motor START/STOP/duty/status/fault controls remain disabled and values unavailable in every case. A future integration needs authoritative opcodes, byte offsets, argument units/ranges, sequence/ACK rules, response layouts, stale threshold and direction/fault guards before these controls can be enabled.
