# GUI changelog

2026-10-23

## Current: remove Diagnostics, add event log

- Removed Diagnostics and its DOM updates. Motor Control fills the available width.
- Added CAN Log as the final expandable section, with timestamp, RX/TX, CAN ID/type, DLC/payload, direction filter, CSV download and local Clear log.
- Logs individual events, including repeated IDs/timestamps, successful RX and queued manual/cyclic/door/heartbeat TX.
- Added fixed 256-event firmware ring, 64-event batches, read-only cursor API, boot-session detection and missed-record reporting. Producer code allocates no heap and performs no JSON/HTTP operations.
- Browser retains 2000 events across refresh through localStorage; expanded view renders latest 200 matches. CSV exports all retained matches, with boot and sequence columns. Storage failures do not prevent live history or export.
- Clear log only affects browser history; Monitor pause does not pause capture. No motor controls enabled or commands replayed.
- Static RAM 66,212 bytes; application flash 1,064,526 bytes. Compared with preceding standalone UI: +10,272 RAM bytes and +9384 application flash bytes.

## Preserved behavior

Legacy HTML, prior routes/API schemas, CAN pins/settings/drivers, task priorities/cores/stacks, payload encodings/IDs, heartbeat interval/recovery, RX aggregation and manual/cyclic guards remain unchanged. Required firmware changes are three bounded capture hooks, log support code, per-boot session initialization and one GET route.

Source verification removes those exact additions and restores previous MOTOR_HTML to obtain a byte-for-byte pre-log source match. No unrelated source/configuration or dependency changes. All shipped implementation remains in main.cpp.

## Earlier revisions

- Standalone cleanup removed branding/title, legacy links, banners, numbered labels and verbose explanations.
- Initial dashboard added /motor, overview, timestamped aggregate Monitor, raw CAN tools/settings and the read-only snapshot API.
- Audit found the implemented door protocol incompatible with Phase 4A; motor controls remain unavailable.

## Limits

Device retains 256 events in volatile RAM; browser retains 2000. Missed and retention counts are separate. Timestamp is uptime; TX is queue acceptance. Real CAN timing/throughput needs the bench checks in GUI_TEST_REPORT.md.
