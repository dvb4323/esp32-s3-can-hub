# GUI redesign plan — current

Updated 2026-10-23. All implementation remains in src/main.cpp. The standalone /motor page has no legacy navigation. Default and Door HTML remain unchanged.

## Layout

- Compact connection strip and four overview cards.
- Full-width Motor Control; unsupported motor commands remain disabled and STOP prominent.
- Existing aggregate CAN Monitor, CAN Tools and Settings.
- Last item: collapsed CAN Log, with timestamp, RX/TX, ID/type, DLC/payload, All/RX/TX filter, Download CSV and local Clear log.
- Diagnostics and all its DOM updates removed. No large title, branding, banners or footer.

## Event capture

Aggregate snapshots cannot reconstruct repeated messages. Capture one event after each successful RX, successful manual/cyclic/door TX queue acceptance and successful heartbeat TX queue acceptance. No changes to CAN bytes, flags, identifiers, driver calls or timing configuration.

Fixed 256-entry ring, 32 bytes/entry, protected by a short portMUX critical section. Producer path has no heap allocation, formatting, HTTP or blocking semaphore. Records retain sequence, device-uptime milliseconds, identifier, DLC, TX/EXT/RTR flags and payload.

GET /api/can/log?after=<sequence>&session=<boot> copies at most 64 records into fixed static storage under the log lock, then serializes outside it. Independent per-browser cursors do not drain shared history. Response includes boot session, next cursor, lost count, more and entries. Invalid cursors rejected. No new POST action.

## Browser history and limits

Log polling runs even while collapsed or Monitor presentation is paused. Cadence: 600 ms after completion; 100 ms catch-up; hidden pages 2000 ms. At most one outstanding log GET. No automatic command retry.

Retain 2000 events in memory/localStorage; render at most 200 matching records when expanded. Preserve cursor/session/history across refresh. Device restart resets the cursor while retained records remain distinguishable by boot ID. Storage failure leaves live history and CSV export available.

CSV exports every retained match chronologically: boot, sequence, timestamp_ms, direction, can_id, type, dlc, payload. Clear log resets local history/counters only and retains the cursor; it never clears CAN tables or stops jobs. No flash/SD logging.

Firmware ring overwrite and browser retention are bounded and reported separately. Timestamp is uptime, not wall clock. TX means accepted into the driver queue, not remote ACK or wire timestamp. This is not a guaranteed lossless saturated-bus trace.

## Validation

Verify legacy preservation, raw control workflows, actual event-core/handler tests, browser persistence/filter/export/retention, responsive layouts and the configured PlatformIO build. Physical tests must measure heartbeat/cyclic jitter, RX loss and heap under traffic and multiple clients.
