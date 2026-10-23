# GUI redesign plan

## Delivery and layout

Provide `/motor` as a standalone flash-resident HTML/CSS/JavaScript section in main.cpp. Leave `/` (Default) and `/door` byte-for-byte unchanged. The dashboard has no links to either legacy interface; visit `/motor` directly.

Responsive dark industrial interface, local system fonts and CSS indicators, no remote assets or dependencies. Desktop: compact connection strip, four overview cards, Motor Control alongside diagnostics, full-width CAN monitor, expandable CAN Tools/settings. Mobile: stacked panels, two-column then single-column cards, horizontally scrollable data tables. Visible focus states, labelled controls, compact live connection messages, reduced-motion support. Remove branding, visible page title, large banners, numbered labels and footer; keep essential details in tooltips or collapsed settings.

Component hierarchy:

- Header: browser connection chip and short offline/stale notice; accessible page heading is visually hidden. No branding or navigation.
- Overview: actual local CAN state; unavailable motor state, requested duty, motor faults.
- Motor Control: reusable command descriptors for START_FORWARD, START_REVERSE, STOP, SET_DUTY, SET_MAX_DUTY, GET_STATUS, CLEAR_FAULT. All disabled because protocol is unimplemented; one concise explanation and unavailable badge. STOP occupies the first and most prominent position. No legacy interface links or substituted door commands.
- Diagnostics: actual driver errors/missed RX, bitrate, uptime, heartbeat sent/fail/recovery and active cyclic jobs. Explicit distinction between controller RUNNING and remote motor connectivity.
- CAN Monitor: bounded aggregated latest RX/TX snapshots, timestamp in device-uptime milliseconds, direction, ID, type, DLC, payload, count, cycle. Filter by direction/ID and pause presentation. TX entries only after a row was actually sent; row edit timestamps are not transmissions. Heartbeat appears as counters rather than invented event rows; door TX rows are not retained by existing firmware and are described accordingly.
- Expandable CAN Debug: explicit add/save/send/delete, all four frame types, payload/cycle editing, existing cyclic TX toggle, existing clears and locks. No sends on initialization/reconnect. Show active jobs restored from device state without enabling any new job.
- Expandable settings: existing bitrate presets, explicit apply button; do not apply on selection/initialization.

## Boundaries and state policy

Browser transport (`request`, poll with timeout, one outstanding snapshot, no automatic POST retry), snapshot presentation, raw debug actions, and unavailable motor capability adapter are separate functions within MOTOR_HTML. Firmware only adds a read-only endpoint. No motor wire protocol code until authoritative opcode/layout/sequence/unit/response contracts and guards are integrated and tested.

States: initial CONNECTING; valid HTTP response CONNECTED independently of CAN state; failed request DISCONNECTED with old data labelled; no successful snapshot for >2500 ms STALE; busy POST disables raw mutations; raw auto rows retain edit/delete locks. Motor stopped/running/busy/fault are never synthesized. Unknown motor state remains unavailable under all CAN and browser states. STOP stays prominent but explicitly unavailable for the motor; existing Door STOP guard is unchanged.

Poll only the new endpoint, serially at 600 ms after completion, with 1800 ms abort. Hidden pages poll at 2000 ms. Watchdog marks a stalled snapshot stale without requiring another response. Browser tables capped by existing 120 RX / 40 TX limits. No persistent browser command queue, optimistic motor telemetry, timers that issue commands, or unbounded trace arrays.

## Firmware additions

`handleMotorPage` uses send_P. `handleDashboardState` copies RX into a fixed static snapshot under a short mutex, captures uptime and RX counter consistently, then serializes after releasing it. TX is read on the owning loop core. Reuse original appendRowJson output plus new lastMs only in the new endpoint; no legacy JSON changes. Expose existing diagnostics without new CAN processing or heartbeat instrumentation. Fixed snapshot buffer is 3840 bytes (32 bytes per CanRow, confirmed by the compiled RAM delta); HTML resides in flash. No new tasks, pin changes, logging rings, or dynamic allocation in RX/heartbeat paths.

## Validation

Compare original HTML strings, all original functions/configuration and CAN encoding against the audit snapshot. Compile the actual `esp32-s3-devkitm-1` environment with installed PlatformIO. Use a local browser mock harness to test live/offline/stale/bus-off data, bounded full tables, all raw API actions and busy/auto locks, draft persistence, refresh/reconnect with no POST, disabled motor buttons, keyboard and desktop/mobile overflow. Record mocks separately from hardware results. Physical verification must confirm actual TX bytes, RX timestamps, heartbeat cadence, bus-off recovery, cyclic timing, WiFi reconnect and sustained traffic/heap behavior.
