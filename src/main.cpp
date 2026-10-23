#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "driver/twai.h"

// ================== CHÂN CAN ==================
#ifndef CAN_TX_PIN
#define CAN_TX_PIN 47
#endif

#ifndef CAN_RX_PIN
#define CAN_RX_PIN 48
#endif

static const gpio_num_t CAN_TX_GPIO = (gpio_num_t)CAN_TX_PIN;
static const gpio_num_t CAN_RX_GPIO = (gpio_num_t)CAN_RX_PIN;

// ================== WIFI AP ==================
const char *AP_SSID = "TCOM_CAN";
const char *AP_PASS = "67896789";

// ================== CAN ==================
#define MAX_ROWS       40
#define MAX_RX_ROWS    120
#define MAX_AUTO_JOBS  8

WebServer server(80);

bool canReady = false;
uint32_t currentBitrate = 500;

uint32_t rxTotal = 0;
uint32_t txTotal = 0;

// ================== DATA STRUCT ==================
struct CanRow {
  bool used;
  bool ext;
  bool rtr;
  uint32_t id;
  uint8_t len;
  uint8_t data[8];
  uint32_t cycleMs;
  uint32_t lastMs;
  uint32_t count;
};

struct TxJob {
  bool used;
  int rowIndex;
  uint32_t intervalMs;
  uint32_t nextMs;
};

CanRow rxRows[MAX_RX_ROWS];
CanRow txRows[MAX_ROWS];
TxJob txJobs[MAX_AUTO_JOBS];

// rxRows được ghi từ task nhận CAN (core 0) và đọc từ web server (core 1)
SemaphoreHandle_t rxMutex = nullptr;
volatile bool rxTaskBusy = false;
volatile bool hbTaskBusy = false;

// ================== DOOR CONTROL (S32K144 qua CAN) ==================
#define DOOR_CMD_ID     0x100   // ESP32 -> S32K144: lệnh
#define DOOR_STATUS_ID  0x101   // S32K144 -> ESP32: trạng thái chính (DLC 6)
#define DOOR_HB_ID      0x102   // ESP32 -> S32K144: heartbeat (DLC 1, A5)
#define DOOR_TRIP_ID    0x103   // S32K144 -> ESP32: hành trình (DLC 8)
#define HB_PERIOD_MS    200

struct DoorFrame {
  bool valid;
  uint8_t len;
  uint8_t data[8];
  uint32_t lastMs;
  uint32_t count;
  uint32_t bad;     // số frame đúng ID nhưng sai DLC / sai loại (bị loại bỏ)
};

DoorFrame door101 = {};
DoorFrame door103 = {};   // cả hai được bảo vệ bởi rxMutex

volatile uint32_t hbSent = 0;
volatile uint32_t hbFail = 0;
volatile uint32_t hbRecover = 0;
volatile uint32_t doorCmdTotal = 0;

// ================== HTML ==================
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 CAN Monitor</title>

  <style>
    body {
      margin: 0;
      font-family: Segoe UI, Arial, sans-serif;
      background: #f4f5f6;
      color: #222;
      font-size: 13px;
    }

    .topbar {
      height: 30px;
      background: white;
      border-bottom: 1px solid #d8dde1;
      display: flex;
      align-items: flex-end;
      gap: 16px;
      padding: 0 8px;
    }

    .tab {
      padding-bottom: 6px;
      color: #333;
    }

    .tab.active {
      border-bottom: 1px solid #333;
    }

    .status {
      margin-left: auto;
      margin-bottom: 5px;
      padding: 2px 8px;
      border-radius: 10px;
      background: #e7edf2;
      color: #334;
      font-size: 12px;
    }

    .status.ok {
      background: #dff3e4;
      color: #126b2f;
    }

    .status.err {
      background: #fde3e3;
      color: #9a1d1d;
    }

    .page {
      padding: 10px 12px;
    }

    .settings {
      width: 320px;
      margin-bottom: 18px;
    }

    .settingsHead {
      display: flex;
      align-items: center;
      gap: 8px;
      margin-bottom: 8px;
      color: #333;
      font-size: 14px;
    }

    .settingsGrid {
      display: grid;
      grid-template-columns: 130px 1fr;
      gap: 9px 12px;
      align-items: center;
    }

    .settingsGrid label {
      color: #4e5862;
    }

    select, input {
      font-family: Segoe UI, Arial, sans-serif;
      font-size: 12px;
      box-sizing: border-box;
    }

    .settingsGrid select,
    .settingsGrid input {
      width: 150px;
      height: 24px;
      border: 1px solid #cfd5da;
      background: white;
      padding: 2px 5px;
    }

    .settingsGrid input:disabled,
    .settingsGrid select:disabled {
      background: #f0f0f0;
      color: #555;
    }

    .cfgMsg {
      grid-column: 1 / span 2;
      color: #61707c;
      font-size: 12px;
      margin-top: 2px;
    }

    .section {
      margin-top: 10px;
      margin-bottom: 28px;
    }

    .sectionHead {
      display: flex;
      align-items: center;
      gap: 14px;
      min-height: 30px;
      padding: 4px 0 4px 9px;
      color: #44505a;
      font-size: 14px;
      flex-wrap: wrap;
    }

    .title {
      min-width: 66px;
    }

    .toolbar {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
    }

    .toolBtn {
      border: none;
      background: transparent;
      cursor: pointer;
      font-size: 20px;
      color: #5a646c;
      height: 26px;
      line-height: 24px;
      padding: 0 2px;
    }

    .toolBtn:hover {
      color: #1f72b8;
    }

    .txCompose span {
      color: #66727b;
      font-size: 12px;
    }

    .txCompose input,
    .txCompose select {
      height: 24px;
      border: 1px solid #cfd5da;
      background: white;
      padding: 2px 5px;
    }

    .txCompose input:disabled {
      background: #f1f1f1;
      color: #777;
    }

    #txNewType {
      width: 78px;
    }

    #txNewId {
      width: 72px;
    }

    #txNewData {
      width: 220px;
      font-family: Consolas, monospace;
    }

    #txNewCycle {
      width: 72px;
    }

    .info {
      color: #66727b;
      font-size: 12px;
      margin-left: 8px;
    }

    .tablebox {
      border: 1px solid #bfc6cc;
      background: white;
      height: 226px;
      overflow: auto;
    }

    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 12px;
    }

    th, td {
      border-right: 1px solid #edf0f3;
      border-bottom: 1px solid #edf0f3;
      padding: 0;
      text-align: left;
      white-space: nowrap;
      height: 25px;
    }

    th {
      position: sticky;
      top: 0;
      background: white;
      color: #000;
      font-weight: 600;
      z-index: 2;
      padding: 4px 6px;
    }

    td {
      padding: 2px 5px;
    }

    tr.selected {
      background: #e9f3ff;
    }

    .empty {
      color: #8a949b;
      text-align: center;
      padding: 36px;
    }

    .bytes {
      font-family: Consolas, monospace;
      letter-spacing: .3px;
    }

    .rowInput, .rowSelect {
      width: 100%;
      height: 23px;
      border: 1px solid transparent;
      background: transparent;
      font-size: 12px;
      box-sizing: border-box;
    }

    .rowInput:focus, .rowSelect:focus {
      border: 1px solid #8ab8e8;
      background: #fff;
      outline: none;
    }

    .rowInput:disabled, .rowSelect:disabled {
      background: #f1f1f1;
      color: #777;
    }

    .rowId {
      width: 78px;
    }

    .rowLen {
      width: 45px;
      text-align: center;
    }

    .rowCycle {
      width: 70px;
    }

    .rowData {
      min-width: 260px;
      font-family: Consolas, monospace;
    }

    .centerCell {
      text-align: center;
    }

    .footer {
      height: 24px;
      display: flex;
      align-items: center;
      justify-content: center;
      color: #69747c;
      font-size: 11px;
      border-top: 1px solid #d8dde1;
      background: white;
    }
  </style>
</head>

<body>
  <div class="topbar">
    <div class="tab active">Connect</div>
    <div class="tab">Trace</div>
    <a class="tab" href="/door" style="text-decoration:none;color:#1f72b8">Door Control &#9656;</a>
    <div id="connect" class="status">Connecting...</div>
  </div>

  <div class="page">
    <div class="settings">
      <div class="settingsHead">
        <span>Settings</span>
        <button class="toolBtn" onclick="applyConfig()" title="Apply settings">↻</button>
        <span id="canState" class="status">CAN</span>
      </div>

      <div class="settingsGrid">
        <label>Mode:</label>
        <select id="mode" disabled>
          <option>CAN Classic</option>
        </select>

        <label>Clock Frequency:</label>
        <select id="clockFreq" disabled>
          <option>8 MHz</option>
        </select>

        <label>Database Entry:</label>
        <select id="dbEntry" onchange="userEditingConfig=true; setPresetUi(this.value)">
          <option value="125">125 kBit/s</option>
          <option value="250">250 kBit/s</option>
          <option value="500" selected>500 kBit/s</option>
          <option value="1000">1 MBit/s</option>
        </select>

        <label>Bit Rate [kbit/s]:</label>
        <input id="bitRate" value="500" disabled>

        <label>Bus Timing Register 0:</label>
        <input id="btr0" value="13" disabled>

        <label>Bus Timing Register 1:</label>
        <input id="btr1" value="2" disabled>

        <label>Sample Count:</label>
        <select id="sampleCount" disabled>
          <option>0</option>
        </select>

        <label>Prescaler:</label>
        <input id="prescaler" value="1" disabled>

        <div id="cfgMsg" class="cfgMsg">Đang dùng 500 kBit/s</div>
      </div>
    </div>

    <div class="section">
      <div class="sectionHead">
        <div class="title">Transmit</div>

        <div class="toolbar txCompose">
          <span>Type</span>
          <select id="txNewType" onchange="updateTxDraftDataState()">
            <option value="STD">STD</option>
            <option value="EXT">EXT</option>
            <option value="STD_RTR">STD RTR</option>
            <option value="EXT_RTR">EXT RTR</option>
          </select>

          <span>ID</span>
          <input id="txNewId" value="336">

          <span>Data</span>
          <input id="txNewData" value="01 00 00 00 00 00 00 00">

          <span>Cycle</span>
          <input id="txNewCycle" type="number" min="10" value="1000">

          <button class="toolBtn" onclick="addTxRow()" title="Thêm message TX từ phần đang chỉnh">＋</button>
          <button class="toolBtn" onclick="sendSelectedTx()" title="Gửi message đang chọn">↗</button>
          <button class="toolBtn" onclick="deleteSelectedTx()" title="Xóa message đang chọn">⌫</button>
          <button class="toolBtn" onclick="clearTx()" title="Clear TX">⋯</button>
        </div>

        <span id="txInfo" class="info">TX: 0</span>
      </div>

      <div class="tablebox">
        <table>
          <thead>
            <tr>
              <th style="width:70px">Type</th>
              <th style="width:95px">CAN-ID</th>
              <th style="width:55px">Length</th>
              <th style="width:110px">Name</th>
              <th>Data</th>
              <th style="width:80px">Cycle Time</th>
              <th style="width:45px">Auto</th>
              <th style="width:55px">Count</th>
            </tr>
          </thead>
          <tbody id="txBody">
            <tr><td colspan="8" class="empty">Chưa có message TX</td></tr>
          </tbody>
        </table>
      </div>
    </div>

    <div class="section">
      <div class="sectionHead">
        <div class="title">Receive</div>

        <div class="toolbar">
          <button class="toolBtn" onclick="clearRx()" title="Clear RX">⌫</button>
        </div>

        <span id="rxInfo" class="info">RX: 0</span>
        <span id="errInfo" class="info">ERR: 0</span>
      </div>

      <div class="tablebox">
        <table>
          <thead>
            <tr>
              <th style="width:70px">Type</th>
              <th style="width:95px">CAN-ID</th>
              <th style="width:55px">Length</th>
              <th style="width:110px">Name</th>
              <th>Data</th>
              <th style="width:80px">Cycle Time</th>
              <th style="width:55px">Count</th>
            </tr>
          </thead>
          <tbody id="rxBody">
            <tr><td colspan="7" class="empty">Chưa nhận được CAN frame</td></tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>

  <div class="footer">
    ESP32-S3 CAN Webserver | IP: <span id="ip">192.168.4.1</span>
  </div>

<script>
let userEditingConfig = false;
let editingTx = false;
let selectedTx = -1;

const bitratePreset = {
  125:  {bit: 125,  btr0: 53, btr1: 2, prescaler: 1},
  250:  {bit: 250,  btr0: 27, btr1: 2, prescaler: 1},
  500:  {bit: 500,  btr0: 13, btr1: 2, prescaler: 1},
  1000: {bit: 1000, btr0: 4,  btr1: 1, prescaler: 1}
};

function $(id) {
  return document.getElementById(id);
}

function esc(v) {
  return String(v ?? '').replace(/[&<>"']/g, s => ({
    '&':'&amp;',
    '<':'&lt;',
    '>':'&gt;',
    '"':'&quot;',
    "'":'&#039;'
  }[s]));
}

function setPresetUi(value) {
  const p = bitratePreset[Number(value)] || bitratePreset[500];

  $('dbEntry').value = String(p.bit);
  $('bitRate').value = p.bit;
  $('btr0').value = p.btr0;
  $('btr1').value = p.btr1;
  $('sampleCount').value = '0';
  $('prescaler').value = p.prescaler;
}

async function postApi(path, params) {
  const res = await fetch(path, {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: params
  });

  const j = await res.json();

  $('connect').textContent = j.ok ? j.msg : 'ERR: ' + j.msg;
  $('connect').className = 'status ' + (j.ok ? 'ok' : 'err');

  return j;
}

async function applyConfig() {
  const p = new URLSearchParams();
  p.set('bitrate', $('dbEntry').value);

  $('cfgMsg').textContent = 'Đang đổi tốc độ CAN...';

  const j = await postApi('/api/config', p);

  if (j.ok) {
    userEditingConfig = false;
    setPresetUi(j.bitrate || $('dbEntry').value);
    $('cfgMsg').textContent = 'Đang dùng ' + (j.bitrate || $('dbEntry').value) + ' kBit/s';
  } else {
    $('cfgMsg').textContent = j.msg;
  }
}

function txDraftParams() {
  const type = $('txNewType').value;

  const p = new URLSearchParams();
  p.set('id', $('txNewId').value.trim());
  p.set('data', $('txNewData').value.trim());
  p.set('cycle', $('txNewCycle').value.trim());

  p.set('ext', type.indexOf('EXT') >= 0 ? '1' : '0');
  p.set('rtr', type.indexOf('RTR') >= 0 ? '1' : '0');

  return p;
}

function updateTxDraftDataState() {
  const type = $('txNewType').value;
  const isRtr = type.indexOf('RTR') >= 0;

  $('txNewData').disabled = isRtr;
  $('txNewData').placeholder = isRtr ? 'RTR không cần Data' : '01 00 00 00 00 00 00 00';
}

async function addTxRow() {
  const j = await postApi('/api/tx/add', txDraftParams());

  if (j.ok) {
    selectedTx = j.idx;
    poll(true);
  }
}

function txRowParams(idx) {
  const type = $('tx_type_' + idx).value;

  const p = new URLSearchParams();
  p.set('idx', idx);
  p.set('id', $('tx_id_' + idx).value.trim());
  p.set('data', $('tx_data_' + idx).value.trim());
  p.set('cycle', $('tx_cycle_' + idx).value.trim());

  p.set('ext', type.indexOf('EXT') >= 0 ? '1' : '0');
  p.set('rtr', type.indexOf('RTR') >= 0 ? '1' : '0');

  return p;
}

function setTxRowLock(idx, locked) {
  const ids = [
    'tx_type_' + idx,
    'tx_id_' + idx,
    'tx_data_' + idx,
    'tx_cycle_' + idx
  ];

  ids.forEach(id => {
    const e = $(id);
    if (e) e.disabled = locked;
  });
}

async function saveTxRow(idx) {
  const autoBox = $('tx_auto_' + idx);

  if (autoBox && autoBox.checked) {
    return;
  }

  const j = await postApi('/api/tx/update', txRowParams(idx));

  if (j.ok) {
    poll(true);
  }
}

async function sendSelectedTx() {
  if (selectedTx < 0) {
    $('connect').textContent = 'Chưa chọn message TX';
    $('connect').className = 'status err';
    return;
  }

  const p = new URLSearchParams();
  p.set('idx', selectedTx);

  const j = await postApi('/api/tx/send', p);

  if (j.ok) {
    poll(true);
  }
}

async function deleteSelectedTx() {
  if (selectedTx < 0) {
    $('connect').textContent = 'Chưa chọn message TX';
    $('connect').className = 'status err';
    return;
  }

  const p = new URLSearchParams();
  p.set('idx', selectedTx);

  const j = await postApi('/api/tx/delete', p);

  if (j.ok) {
    selectedTx = -1;
    poll(true);
  }
}

async function toggleTxAuto(idx, checkbox) {
  const enable = checkbox.checked;

  const p = txRowParams(idx);
  p.set('enable', enable ? '1' : '0');

  setTxRowLock(idx, enable);

  const j = await postApi('/api/tx/auto', p);

  if (!j.ok) {
    checkbox.checked = !enable;
    setTxRowLock(idx, !enable);
    return;
  }

  poll(true);
}

async function clearTx() {
  const j = await postApi('/api/clear_tx', new URLSearchParams());
  if (j.ok) {
    selectedTx = -1;
    poll(true);
  }
}

async function clearRx() {
  const j = await postApi('/api/clear_rx', new URLSearchParams());
  if (j.ok) {
    poll(true);
  }
}

function selectTxRow(idx) {
  selectedTx = idx;

  document.querySelectorAll('#txBody tr').forEach(tr => {
    tr.classList.remove('selected');
  });

  const tr = $('tx_tr_' + idx);
  if (tr) tr.classList.add('selected');
}

function renderTxRows(rows) {
  const body = $('txBody');

  if (!rows || rows.length === 0) {
    body.innerHTML = '<tr><td colspan="8" class="empty">Chưa có message TX</td></tr>';
    return;
  }

  body.innerHTML = rows.map(r => {
    const idx = r.idx;
    const auto = !!r.auto;
    const typeText = String(r.type || 'STD');

    const isExt = typeText.indexOf('EXT') >= 0;
    const isRtr = typeText.indexOf('RTR') >= 0;

    let typeValue = 'STD';
    if (isExt && isRtr) typeValue = 'EXT_RTR';
    else if (isExt) typeValue = 'EXT';
    else if (isRtr) typeValue = 'STD_RTR';

    const idValue = String(r.canid || '').replace(/h$/i, '');
    const selectedClass = Number(idx) === Number(selectedTx) ? 'selected' : '';

    return `
      <tr id="tx_tr_${idx}" class="${selectedClass}" onclick="selectTxRow(${idx})">
        <td>
          <select id="tx_type_${idx}" class="rowSelect"
                  ${auto ? 'disabled' : ''}
                  onfocus="editingTx=true"
                  onblur="editingTx=false"
                  onchange="editingTx=false; saveTxRow(${idx})">
            <option value="STD" ${typeValue === 'STD' ? 'selected' : ''}>STD</option>
            <option value="EXT" ${typeValue === 'EXT' ? 'selected' : ''}>EXT</option>
            <option value="STD_RTR" ${typeValue === 'STD_RTR' ? 'selected' : ''}>STD RTR</option>
            <option value="EXT_RTR" ${typeValue === 'EXT_RTR' ? 'selected' : ''}>EXT RTR</option>
          </select>
        </td>

        <td>
          <input id="tx_id_${idx}" class="rowInput rowId"
                 value="${esc(idValue)}"
                 ${auto ? 'disabled' : ''}
                 onfocus="editingTx=true"
                 onblur="editingTx=false; saveTxRow(${idx})">
        </td>

        <td>
          <input class="rowInput rowLen"
                 value="${esc(r.len)}"
                 disabled>
        </td>

        <td>
          <input class="rowInput"
                 value="${esc(r.name)}"
                 disabled>
        </td>

        <td>
          <input id="tx_data_${idx}" class="rowInput rowData"
                 value="${esc(r.data)}"
                 ${(auto || isRtr) ? 'disabled' : ''}
                 onfocus="editingTx=true"
                 onblur="editingTx=false; saveTxRow(${idx})">
        </td>

        <td>
          <input id="tx_cycle_${idx}" class="rowInput rowCycle"
                 type="number"
                 min="10"
                 value="${esc(r.cycle || 1000)}"
                 ${auto ? 'disabled' : ''}
                 onfocus="editingTx=true"
                 onblur="editingTx=false; saveTxRow(${idx})">
        </td>

        <td class="centerCell">
          <input id="tx_auto_${idx}"
                 type="checkbox"
                 ${auto ? 'checked' : ''}
                 onchange="toggleTxAuto(${idx}, this)">
        </td>

        <td>${esc(r.count)}</td>
      </tr>
    `;
  }).join('');
}

function renderRxRows(rows) {
  const body = $('rxBody');

  if (!rows || rows.length === 0) {
    body.innerHTML = '<tr><td colspan="7" class="empty">Chưa nhận được CAN frame</td></tr>';
    return;
  }

  body.innerHTML = rows.map(r => `
    <tr>
      <td>${esc(r.type)}</td>
      <td>${esc(r.canid)}</td>
      <td>${esc(r.len)}</td>
      <td>${esc(r.name)}</td>
      <td class="bytes">${esc(r.data)}</td>
      <td>${esc(r.cycle)}</td>
      <td>${esc(r.count)}</td>
    </tr>
  `).join('');
}

async function poll(onceOnly = false) {
  try {
    const res = await fetch('/api/state?t=' + Date.now(), {cache: 'no-store'});
    const j = await res.json();

    $('connect').textContent = 'Connected';
    $('connect').className = 'status ok';

    $('canState').textContent = 'CAN: ' + j.can;
    $('canState').className = 'status ' + (j.can === 'RUNNING' ? 'ok' : 'err');

    $('rxInfo').textContent = 'RX: ' + j.rxTotal;
    $('txInfo').textContent = 'TX: ' + j.txTotal;
    $('errInfo').textContent = 'ERR: TX ' + j.txErr + ' / RX ' + j.rxErr + ' / BUS ' + j.busErr + ' / MISS ' + j.rxMiss;

    $('ip').textContent = j.ip + ' | FW: ' + (j.fw || 'cũ (chưa nạp bản mới)');

    if (!userEditingConfig) {
      setPresetUi(j.bitrate || 500);
      $('cfgMsg').textContent = 'Đang dùng ' + (j.bitrate || 500) + ' kBit/s';
    }

    renderRxRows(j.rx);

    // đang sửa = con trỏ thực sự nằm trong một ô của bảng TX;
    // không tin cờ editingTx để bảng không bị kẹt nếu cờ quên reset
    const ae = document.activeElement;
    const editingRow = !!(ae && ae.id && ae.id.indexOf('tx_') === 0);

    if (!editingRow) {
      editingTx = false;
      renderTxRows(j.tx);
    }
  } catch (e) {
    $('connect').textContent = 'Disconnected';
    $('connect').className = 'status err';
  }

  if (!onceOnly) {
    setTimeout(poll, 400);
  }
}

updateTxDraftDataState();
poll();
</script>
</body>
</html>
)HTML";

// ================== HTML: DOOR CONTROL ==================
static const char DOOR_HTML[] PROGMEM = R"DOOR(
<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0f14">
<title>Automatic Door Control</title>
<style>
:root{--bg:#0b0f14;--panel:#121923;--panel2:#18212d;--line:#243242;--txt:#e8eef5;--mut:#8497ab;
--ok:#2ecc71;--warn:#ffb020;--err:#ff4d4f;--info:#4aa3ff;}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;height:100%}
body{background:var(--bg);color:var(--txt);font-family:"Segoe UI",system-ui,Arial,sans-serif;font-size:14px;overflow:hidden}
.app{height:100vh;height:100dvh;max-width:560px;margin:0 auto;display:flex;flex-direction:column;gap:8px;
padding:8px 10px calc(8px + env(safe-area-inset-bottom))}
header{display:flex;align-items:center;gap:8px}
header .t{flex:1;min-width:0}
h1{margin:0;font-size:15px;letter-spacing:1.5px;font-weight:800;white-space:nowrap}
.sub{color:var(--mut);font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chip{padding:4px 10px;border-radius:999px;font-weight:800;font-size:11px;letter-spacing:1px;border:1px solid var(--line);background:var(--panel2);color:var(--mut);white-space:nowrap}
.chip.ok{color:var(--ok);border-color:#1f6b3f;background:#10261a}
.chip.err{color:var(--err);border-color:#7a2a2b;background:#2a1213}
.chip.warn{color:var(--warn);border-color:#7a5a12;background:#2a2010}
.chip.info{color:var(--info);border-color:#245080;background:#10202f}
a.mon{color:var(--mut);text-decoration:none;font-size:11px;border:1px solid var(--line);border-radius:8px;padding:4px 8px;white-space:nowrap}
#alerts{display:flex;flex-direction:column;gap:6px;flex:none}
#alerts:empty{display:none}
.alert{border-radius:10px;padding:7px 10px;border:1px solid;font-weight:700;font-size:13px}
.alert.err{background:#2a1213;border-color:#a33;color:#ffb3b3}
.alert.warn{background:#2a2010;border-color:#a77a1a;color:#ffd98a}
.alert.big{font-size:16px;text-align:center;letter-spacing:1px;animation:blink 1s infinite}
.alert.stale{opacity:.55;animation:none}
.alert small{display:block;font-weight:400;opacity:.85;font-size:11px;margin-top:1px}
.chips{display:flex;flex-wrap:wrap;gap:5px;margin-top:5px}
@keyframes blink{50%{box-shadow:0 0 0 3px rgba(255,77,79,.35)}}
.cards{display:flex;flex-direction:column;gap:6px;flex:none}
.card{background:var(--panel);border:1px solid var(--line);border-left:5px solid var(--mut);border-radius:10px;
padding:6px 12px;display:flex;align-items:center;justify-content:space-between;gap:8px;min-height:50px}
.card .l{min-width:0}
.card .lbl{color:var(--mut);font-size:10px;letter-spacing:1.5px;text-transform:uppercase}
.card .sub2{color:var(--mut);font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.card .val{font-size:19px;font-weight:800;letter-spacing:.5px;text-align:right;white-space:nowrap}
.card.ok{border-left-color:var(--ok)}.card.ok .val{color:var(--ok)}
.card.warn{border-left-color:var(--warn)}.card.warn .val{color:var(--warn)}
.card.err{border-left-color:var(--err)}.card.err .val{color:var(--err)}
.card.err:not(.stale){animation:blink 1s infinite}
.card.idle .val{color:var(--mut)}
.card.stale{opacity:.6}
.pwm{display:flex;align-items:center;gap:8px;font-size:11px;color:var(--mut);flex:none}
.bar{flex:1;height:8px;border-radius:4px;background:#0b1118;border:1px solid var(--line);overflow:hidden}
.fill{height:100%;width:0;background:var(--info);transition:width .25s}
.ctl{flex:1;min-height:0;display:grid;grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr 1.1fr 0.55fr;gap:8px}
button.btn{min-height:44px;border-radius:14px;border:1px solid var(--line);background:var(--panel2);color:var(--txt);
font-size:17px;font-weight:800;letter-spacing:2px;cursor:pointer;touch-action:manipulation;user-select:none;padding:0}
button.btn:active:not(:disabled){transform:scale(.97)}
button.btn:disabled{opacity:.4;cursor:not-allowed}
.b-open{border-color:#1f6b3f}.b-close{border-color:#245080}.b-lock{border-color:#6b5a1f}.b-unlock{border-color:#1f6b6b}
.b-stop{grid-column:1/3;background:#7a1517;border-color:#ff4d4f;font-size:24px;color:#fff}
.b-clear{grid-column:1/3;background:transparent;border-color:var(--warn);color:var(--warn);font-size:14px}
#cmdMsg{flex:none;font-size:11px;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-height:15px}
#cmdMsg.err{color:var(--err)}#cmdMsg.warn{color:var(--warn)}
</style>
</head>
<body>
<div class="app">
  <header>
    <div class="t">
      <h1>AUTOMATIC DOOR CONTROL</h1>
      <div class="sub" id="lastRx">Chưa nhận phản hồi</div>
    </div>
    <span id="conn" class="chip">WAITING</span>
    <a class="mon" href="/">Monitor</a>
  </header>

  <div id="alerts"></div>

  <div class="cards">
    <div class="card idle" id="cLock"><div class="l"><div class="lbl">Door Lock</div><div class="sub2"></div></div><div class="val">—</div></div>
    <div class="card idle" id="cDoor"><div class="l"><div class="lbl">Door Motion</div><div class="sub2"></div></div><div class="val">—</div></div>
    <div class="card idle" id="cObs"><div class="l"><div class="lbl">Obstacle</div><div class="sub2"></div></div><div class="val">—</div></div>
  </div>

  <div class="pwm"><span>PWM</span><div class="bar"><div class="fill" id="pwmFill"></div></div><span id="pwmTxt">—</span></div>

  <div class="ctl">
    <button class="btn b-lock"   data-cmd="lock">LOCK</button>
    <button class="btn b-unlock" data-cmd="unlock">UNLOCK</button>
    <button class="btn b-open"   data-cmd="open">OPEN</button>
    <button class="btn b-close"  data-cmd="close">CLOSE</button>
    <button class="btn b-stop"   data-cmd="stop">STOP</button>
    <button class="btn b-clear"  data-cmd="clear">CLEAR FAULT</button>
  </div>
  <div id="cmdMsg">Mỗi lần bấm gửi 1 lệnh; trạng thái chỉ đổi khi S32K144 phản hồi.</div>
</div>

<script>
const $ = id => document.getElementById(id);
const FRESH_MS = 1000;

const DOOR = {
  0: ['UNKNOWN', 'Chưa xác định vị trí', 'warn'],
  1: ['CLOSED',  'Đã đóng', 'ok'],
  2: ['OPENING', 'Đang mở', 'warn'],
  3: ['OPENED',  'Đã mở', 'ok'],
  4: ['CLOSING', 'Đang đóng', 'warn'],
  5: ['STOPPED', 'Đã dừng giữa hành trình', 'warn']
};

const FAULTS = [
  {m:0x01, n:'OBSTACLE', d:'Phát hiện vật cản', s:'err'},
  {m:0x02, n:'CAN COMM LOST', d:'Mất liên lạc CAN', s:'err'},
  {m:0x04, n:'CAN BUS-OFF', d:'Lỗi CAN Bus-Off', s:'err'},
  {m:0x08, n:'ENCODER/STALL', d:'Lỗi encoder hoặc kẹt lặp lại', s:'err'},
  {m:0x10, n:'TRAVEL TIMEOUT', d:'Quá thời gian hành trình', s:'err'},
  {m:0x20, n:'LIMIT SWITCH', d:'Lỗi công tắc hành trình', s:'err'},
  {m:0x40, n:'POSITION NOT SET', d:'Chưa xác lập vị trí cửa', s:'warn'},
  {m:0x80, n:'POWER-ON RESET', d:'Thông tin khởi động lại, không phải lỗi', s:'info'}
];

let d101 = null, lastRxWall = 0, netErr = false, lastJ = null, busy = false;

function hx(n){ return (n & 0xFF).toString(16).toUpperCase().padStart(2,'0'); }
function hhmmss(t){ return new Date(t).toLocaleTimeString(); }

function setCard(id, cls, val, sub, stale){
  const c = $(id);
  c.className = 'card ' + cls + (stale ? ' stale' : '');
  c.querySelector('.val').textContent = val;
  c.querySelector('.sub2').textContent = sub;
}

function render(j){
  if (j) {
    lastJ = j;
    if (j.f101.valid) { d101 = j.f101.data; lastRxWall = Date.now() - j.f101.age; }
  }
  const J = j || lastJ;

  let link = 'WAITING';
  if (netErr) link = 'DISCONNECTED';
  else if (J && J.f101.valid) link = (J.f101.age <= FRESH_MS) ? 'CONNECTED' : 'DISCONNECTED';
  const live = link === 'CONNECTED';

  const chip = $('conn');
  chip.textContent = link;
  chip.className = 'chip ' + (live ? 'ok' : link === 'DISCONNECTED' ? 'err' : 'warn');
  $('lastRx').textContent = lastRxWall
    ? 'Phản hồi cuối ' + hhmmss(lastRxWall) + ' (' + ((Date.now() - lastRxWall) / 1000).toFixed(1) + ' s trước)'
    : 'Chưa nhận phản hồi từ S32K144';

  const b0 = d101 ? d101[0] : 0, b1 = d101 ? d101[1] : 0,
        b2 = d101 ? d101[2] : 0, b3 = d101 ? d101[3] : 0;

  // Khóa
  if (!d101) setCard('cLock','idle','—','Chờ dữ liệu', false);
  else {
    const t = b1 === 0 ? 'UNLOCKED' : b1 === 1 ? 'LOCKED' : 'INVALID 0x' + hx(b1);
    if (live) setCard('cLock', (b1 === 0 || b1 === 1) ? 'ok' : 'err', t, b1 === 1 ? 'Đã khóa (phần mềm)' : b1 === 0 ? 'Đã mở khóa' : 'Ngoài đặc tả', false);
    else setCard('cLock','idle','NO DATA','Cũ, chưa xác nhận: ' + t, true);
  }

  // Cửa
  if (!d101) setCard('cDoor','idle','—','Chờ dữ liệu', false);
  else {
    const e = DOOR[b0];
    const t = e ? e[0] : 'INVALID 0x' + hx(b0);
    if (live) setCard('cDoor', e ? e[2] : 'err', t, e ? e[1] : 'Ngoài đặc tả', false);
    else setCard('cDoor','idle','NO DATA','Cũ, chưa xác nhận: ' + t, true);
  }

  // Vật cản (không tự xóa theo thời gian; chỉ đổi khi S32K144 báo)
  const obs = (b2 & 0x01) !== 0;
  if (!d101) setCard('cObs','idle','—','Chờ dữ liệu', false);
  else if (live) setCard('cObs', obs ? 'err' : 'ok', obs ? 'DETECTED' : 'NO EVENT',
      obs ? 'Chỉ xóa khi S32K144 báo đã xóa' : 'Không có cảnh báo vật cản', false);
  else if (obs) setCard('cObs','err','LAST: OBSTACLE','Chưa được xác nhận xóa', true);
  else setCard('cObs','idle','NO DATA','Cũ: không có vật cản', true);

  // Cảnh báo
  let h = '';
  if (netErr) h += '<div class="alert err">MẤT KẾT NỐI ESP32<small>Kiểm tra WiFi TCOM_CAN. Dữ liệu bên dưới là dữ liệu cũ.</small></div>';
  else if (link === 'DISCONNECTED') h += '<div class="alert err">MẤT KẾT NỐI S32K144<small>Không có phản hồi &gt; 1 s. Dữ liệu hiển thị là dữ liệu cũ.</small></div>';
  if (J && !netErr && J.can !== 'RUNNING') h += '<div class="alert warn">CAN ESP32: ' + J.can + '</div>';

  if (d101) {
    const st = live ? '' : ' stale';
    const tag = live ? '' : ' (CŨ)';
    if (b2 & 0x01) h += '<div class="alert err big' + st + '">⚠ OBSTACLE DETECTED' + tag + '</div>';
    const others = FAULTS.filter(f => (b2 & f.m) && f.m !== 0x01 && f.m !== 0x80);
    if (others.length) {
      const sev = others.some(f => f.s === 'err') ? 'err' : 'warn';
      h += '<div class="alert ' + sev + st + '">LỖI / CẢNH BÁO' + tag + '<div class="chips">' +
        others.map(f => '<span class="chip ' + f.s + '" title="' + f.d + '">' + f.n + '</span>').join('') + '</div></div>';
    }
    if (b2 & 0x80) h += '<div><span class="chip info">POWER-ON RESET (INFO)</span></div>';
  }
  $('alerts').innerHTML = h;

  // PWM
  const ok = live && d101 && b3 <= 100;
  $('pwmFill').style.width = ok ? b3 + '%' : '0%';
  $('pwmTxt').textContent = !d101 ? '—' : (b3 > 100 ? 'INVALID' : b3 + '%') + (live ? '' : ' (cũ)');
}

/* ---- Gửi lệnh: mỗi lần bấm đúng 1 frame, không tự gửi lại ---- */
const NAMES = {lock:'LOCK', unlock:'UNLOCK', open:'OPEN', close:'CLOSE', stop:'STOP', clear:'CLEAR FAULT'};

function setBusy(on){
  document.querySelectorAll('button.btn').forEach(b => { if (b.dataset.cmd !== 'stop') b.disabled = on; });
}
function msg(t, c){ const m = $('cmdMsg'); m.textContent = t; m.className = c || ''; }

async function sendCmd(cmd){
  if (busy && cmd !== 'stop') return;
  busy = true; setBusy(true);
  const name = NAMES[cmd], t = hhmmss(Date.now());
  const ctl = new AbortController();
  const to = setTimeout(() => ctl.abort(), 2500);
  try {
    const r = await fetch('/api/door/cmd', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'cmd=' + cmd, signal: ctl.signal
    });
    const j = await r.json();
    if (j.ok) msg(name + ' đã gửi lúc ' + t + ' — chờ S32K144 phản hồi', 'warn');
    else msg(name + ' THẤT BẠI: ' + j.msg, 'err');
  } catch (e) {
    msg(name + ': không rõ đã gửi chưa — kiểm tra trạng thái trước khi bấm lại', 'err');
  }
  clearTimeout(to);
  setTimeout(() => { busy = false; setBusy(false); }, 300);
}

document.querySelectorAll('button.btn').forEach(b => b.addEventListener('click', () => sendCmd(b.dataset.cmd)));
document.addEventListener('keydown', e => {
  if (e.repeat && e.target && e.target.tagName === 'BUTTON') e.preventDefault();
});

async function poll(){
  const ctl = new AbortController();
  const to = setTimeout(() => ctl.abort(), 1500);
  try {
    const r = await fetch('/api/door/state?t=' + Date.now(), {cache: 'no-store', signal: ctl.signal});
    const j = await r.json();
    netErr = false;
    render(j);
  } catch (e) {
    netErr = true;
    render(null);
  }
  clearTimeout(to);
  setTimeout(poll, 300);
}
render(null);
poll();
</script>
</body>
</html>
)DOOR";

// ================== HELPER ==================
bool argTrue(const String &s) {
  return s == "1" || s == "true" || s == "on" || s == "ON";
}

String frameName(uint32_t id, bool ext) {
  if (!ext && id == 0x336) return "";
  return "";
}

String hexId(uint32_t id, bool ext) {
  char buf[12];

  if (ext) {
    snprintf(buf, sizeof(buf), "%08lX", (unsigned long)id);
  } else {
    snprintf(buf, sizeof(buf), "%03lX", (unsigned long)id);
  }

  return String(buf);
}

String dataString(const uint8_t *data, uint8_t len, bool rtr) {
  if (rtr) return "RTR";

  String s;

  for (uint8_t i = 0; i < len; i++) {
    char b[4];
    snprintf(b, sizeof(b), "%02X", data[i]);

    if (i) s += " ";
    s += b;
  }

  return s;
}

const char *canStateName(twai_state_t s) {
  switch (s) {
    case TWAI_STATE_STOPPED:    return "STOPPED";
    case TWAI_STATE_RUNNING:    return "RUNNING";
    case TWAI_STATE_BUS_OFF:    return "BUS_OFF";
    case TWAI_STATE_RECOVERING: return "RECOVERING";
    default:                    return "UNKNOWN";
  }
}

void appendEscaped(String &out, const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];

    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
}

void jsonReply(bool ok, const String &msg) {
  String j;
  j.reserve(180 + msg.length());

  j += "{\"ok\":";
  j += ok ? "true" : "false";
  j += ",\"msg\":\"";
  appendEscaped(j, msg);
  j += "\"}";

  server.send(ok ? 200 : 400, "application/json; charset=utf-8", j);
}

void jsonReplyWithIdx(bool ok, const String &msg, int idx) {
  String j;
  j.reserve(220 + msg.length());

  j += "{\"ok\":";
  j += ok ? "true" : "false";
  j += ",\"msg\":\"";
  appendEscaped(j, msg);
  j += "\",\"idx\":";
  j += String(idx);
  j += "}";

  server.send(ok ? 200 : 400, "application/json; charset=utf-8", j);
}

void jsonReplyConfig(bool ok, const String &msg, uint32_t bitrate) {
  String j;
  j.reserve(240 + msg.length());

  j += "{\"ok\":";
  j += ok ? "true" : "false";
  j += ",\"msg\":\"";
  appendEscaped(j, msg);
  j += "\",\"bitrate\":";
  j += String(bitrate);
  j += "}";

  server.send(ok ? 200 : 400, "application/json; charset=utf-8", j);
}

int findSameRxRow(uint32_t id, bool ext, bool rtr) {
  for (int i = 0; i < MAX_RX_ROWS; i++) {
    if (rxRows[i].used &&
        rxRows[i].id == id &&
        rxRows[i].ext == ext &&
        rxRows[i].rtr == rtr) {
      return i;
    }
  }

  return -1;
}

int chooseRxRowSlot() {
  for (int i = 0; i < MAX_RX_ROWS; i++) {
    if (!rxRows[i].used) return i;
  }

  int oldest = 0;

  for (int i = 1; i < MAX_RX_ROWS; i++) {
    if (rxRows[i].lastMs < rxRows[oldest].lastMs) {
      oldest = i;
    }
  }

  return oldest;
}

// now = thời điểm frame được nhận (lấy ngay tại twai_receive, không phải lúc xử lý)
void updateRxRow(uint32_t id,
                 bool ext,
                 bool rtr,
                 uint8_t len,
                 const uint8_t *data,
                 uint32_t now) {
  xSemaphoreTake(rxMutex, portMAX_DELAY);

  int idx = findSameRxRow(id, ext, rtr);
  bool same = idx >= 0;

  if (!same) {
    idx = chooseRxRowSlot();
    memset(&rxRows[idx], 0, sizeof(CanRow));
  }

  CanRow &r = rxRows[idx];

  r.used = true;
  r.id = id;
  r.ext = ext;
  r.rtr = rtr;
  r.len = len;

  if (!rtr && data != nullptr) {
    memcpy(r.data, data, len);
  } else {
    memset(r.data, 0, sizeof(r.data));
  }

  if (same) {
    uint32_t delta = now - r.lastMs;

    // frame cùng ID đến trong cùng 1ms là burst: giữ nguyên chu kỳ
    // giữa các burst thay vì ghi đè bằng 0
    if (delta > 0) {
      r.cycleMs = delta;
    }

    r.count++;
  } else {
    r.cycleMs = 0;
    r.count = 1;
  }

  r.lastMs = now;
  rxTotal++;

  xSemaphoreGive(rxMutex);
}

bool isRowAuto(int idx) {
  for (int i = 0; i < MAX_AUTO_JOBS; i++) {
    if (txJobs[i].used && txJobs[i].rowIndex == idx) {
      return true;
    }
  }

  return false;
}

int findFreeTxRow() {
  for (int i = 0; i < MAX_ROWS; i++) {
    if (!txRows[i].used) return i;
  }

  return -1;
}

bool txIndexValid(int idx) {
  return idx >= 0 && idx < MAX_ROWS && txRows[idx].used;
}

int findAutoJobByRow(int idx) {
  for (int i = 0; i < MAX_AUTO_JOBS; i++) {
    if (txJobs[i].used && txJobs[i].rowIndex == idx) {
      return i;
    }
  }

  return -1;
}

int findFreeAutoJob() {
  for (int i = 0; i < MAX_AUTO_JOBS; i++) {
    if (!txJobs[i].used) return i;
  }

  return -1;
}

void stopAutoJobByRow(int idx) {
  for (int i = 0; i < MAX_AUTO_JOBS; i++) {
    if (txJobs[i].used && txJobs[i].rowIndex == idx) {
      txJobs[i].used = false;
    }
  }
}

// ================== PARSE ==================
bool parseHexUint(String raw, uint32_t &value) {
  raw.trim();

  if (raw.endsWith("h") || raw.endsWith("H")) {
    raw.remove(raw.length() - 1);
  }

  if (raw.startsWith("0x") || raw.startsWith("0X")) {
    raw = raw.substring(2);
  }

  if (raw.length() == 0) return false;

  char *endPtr = nullptr;
  value = strtoul(raw.c_str(), &endPtr, 16);

  return endPtr && *endPtr == '\0';
}

bool parseHexByte(String raw, uint8_t &value) {
  raw.trim();

  if (raw.startsWith("0x") || raw.startsWith("0X")) {
    raw = raw.substring(2);
  }

  if (raw.length() == 0 || raw.length() > 2) return false;

  char *endPtr = nullptr;
  uint32_t v = strtoul(raw.c_str(), &endPtr, 16);

  if (!endPtr || *endPtr != '\0' || v > 0xFF) return false;

  value = (uint8_t)v;
  return true;
}

bool parseDataBytes(String raw, uint8_t *data, uint8_t &len, String &err) {
  len = 0;
  raw.trim();

  if (raw.length() == 0) {
    return true;
  }

  raw.replace(",", " ");
  raw.replace(";", " ");
  raw.replace("\n", " ");
  raw.replace("\r", " ");

  bool hasSpace = raw.indexOf(' ') >= 0;

  if (hasSpace) {
    int pos = 0;

    while (pos < raw.length()) {
      int next = raw.indexOf(' ', pos);

      if (next < 0) {
        next = raw.length();
      }

      String token = raw.substring(pos, next);
      token.trim();

      if (token.length() > 0) {
        if (len >= 8) {
          err = "Data vượt quá 8 byte";
          return false;
        }

        uint8_t b;

        if (!parseHexByte(token, b)) {
          err = "Byte data không hợp lệ: " + token;
          return false;
        }

        data[len++] = b;
      }

      pos = next + 1;
    }

    return true;
  }

  raw.replace("0x", "");
  raw.replace("0X", "");

  if (raw.length() % 2) {
    raw = "0" + raw;
  }

  if (raw.length() > 16) {
    err = "Data vượt quá 8 byte";
    return false;
  }

  for (int i = 0; i < raw.length(); i += 2) {
    uint8_t b;
    String token = raw.substring(i, i + 2);

    if (!parseHexByte(token, b)) {
      err = "Data không hợp lệ";
      return false;
    }

    data[len++] = b;
  }

  return true;
}

bool readFrameArgs(uint32_t &id,
                   bool &ext,
                   bool &rtr,
                   uint8_t &len,
                   uint8_t *data,
                   String &err) {
  if (!server.hasArg("id")) {
    err = "Thiếu CAN-ID";
    return false;
  }

  if (!parseHexUint(server.arg("id"), id)) {
    err = "CAN-ID không hợp lệ";
    return false;
  }

  ext = argTrue(server.arg("ext"));
  rtr = argTrue(server.arg("rtr"));

  if (!ext && id > 0x7FF) {
    err = "Standard ID phải <= 7FFh";
    return false;
  }

  if (ext && id > 0x1FFFFFFF) {
    err = "Extended ID phải <= 1FFFFFFFh";
    return false;
  }

  memset(data, 0, 8);

  if (rtr) {
    len = 0;
    return true;
  }

  return parseDataBytes(server.arg("data"), data, len, err);
}

bool updateTxRowFromArgs(int idx, bool resetCount, String &err) {
  uint32_t id;
  bool ext;
  bool rtr;
  uint8_t len;
  uint8_t data[8];

  if (!readFrameArgs(id, ext, rtr, len, data, err)) {
    return false;
  }

  uint32_t cycle = (uint32_t)server.arg("cycle").toInt();

  if (cycle < 10) {
    cycle = 1000;
  }

  txRows[idx].used = true;
  txRows[idx].id = id;
  txRows[idx].ext = ext;
  txRows[idx].rtr = rtr;
  txRows[idx].len = len;
  txRows[idx].cycleMs = cycle;
  txRows[idx].lastMs = millis();

  if (!rtr) {
    memcpy(txRows[idx].data, data, len);
  } else {
    memset(txRows[idx].data, 0, sizeof(txRows[idx].data));
  }

  if (resetCount) {
    txRows[idx].count = 0;
  }

  return true;
}

// ================== CAN ==================
twai_timing_config_t getTimingConfig(uint32_t kbps) {
  switch (kbps) {
    case 125:
      return TWAI_TIMING_CONFIG_125KBITS();

    case 250:
      return TWAI_TIMING_CONFIG_250KBITS();

    case 1000:
      return TWAI_TIMING_CONFIG_1MBITS();

    case 500:
    default:
      return TWAI_TIMING_CONFIG_500KBITS();
  }
}

bool startCan(uint32_t bitrate) {
  twai_general_config_t g_config =
    TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);

  g_config.tx_queue_len = 20;
  g_config.rx_queue_len = 50;

  twai_timing_config_t t_config = getTimingConfig(bitrate);
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t result;

  result = twai_driver_install(&g_config, &t_config, &f_config);

  if (result != ESP_OK) {
    Serial.print("twai_driver_install failed: ");
    Serial.println((int)result);
    return false;
  }

  result = twai_start();

  if (result != ESP_OK) {
    Serial.print("twai_start failed: ");
    Serial.println((int)result);
    twai_driver_uninstall();
    return false;
  }

  currentBitrate = bitrate;
  return true;
}

void stopCan() {
  if (!canReady) return;

  canReady = false;

  // chờ task RX thoát khỏi twai_receive (tối đa ~50ms) trước khi gỡ driver
  while (rxTaskBusy || hbTaskBusy) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  twai_stop();
  twai_driver_uninstall();
}

bool restartCan(uint32_t bitrate) {
  stopCan();
  delay(20);

  canReady = startCan(bitrate);
  return canReady;
}

bool transmitFrame(const CanRow &r, String &err) {
  if (!canReady) {
    err = "CAN chưa khởi động";
    return false;
  }

  twai_message_t msg = {};
  msg.identifier = r.id;
  msg.data_length_code = r.len;
  msg.flags = 0;

  if (r.ext) {
    msg.flags |= TWAI_MSG_FLAG_EXTD;
  }

  if (r.rtr) {
    msg.flags |= TWAI_MSG_FLAG_RTR;
  }

  if (!r.rtr && r.len > 0) {
    memcpy(msg.data, r.data, r.len);
  }

  esp_err_t result = twai_transmit(&msg, pdMS_TO_TICKS(20));

  if (result != ESP_OK) {
    err = "twai_transmit lỗi: " + String((int)result);
    return false;
  }

  txTotal++;
  return true;
}

bool sendTxRow(int idx, uint32_t forcedCycleMs, String &err) {
  if (!txIndexValid(idx)) {
    err = "TX row không hợp lệ";
    return false;
  }

  if (!transmitFrame(txRows[idx], err)) {
    return false;
  }

  txRows[idx].count++;
  txRows[idx].lastMs = millis();

  if (forcedCycleMs > 0) {
    txRows[idx].cycleMs = forcedCycleMs;
  }

  return true;
}

// Lưu frame 0x101 / 0x103 của S32K144 (chỉ nhận frame chuẩn, đúng DLC)
void updateDoorFrame(uint32_t id, uint8_t len, const uint8_t *data, uint32_t now) {
  DoorFrame *f = nullptr;
  uint8_t expect = 0;

  if (id == DOOR_STATUS_ID)    { f = &door101; expect = 6; }
  else if (id == DOOR_TRIP_ID) { f = &door103; expect = 8; }
  else return;

  xSemaphoreTake(rxMutex, portMAX_DELAY);

  if (len != expect) {
    f->bad++;
  } else {
    memcpy(f->data, data, len);
    f->len = len;
    f->lastMs = now;
    f->count++;
    f->valid = true;
  }

  xSemaphoreGive(rxMutex);
}

// Task nhận CAN riêng: block trên twai_receive nên timestamp lấy được
// ngay khi frame đến, không phụ thuộc độ trễ của loop()/web server.
void canRxTask(void *arg) {
  twai_message_t msg;

  for (;;) {
    // rxTaskBusy phải bật TRƯỚC khi kiểm tra canReady để stopCan()
    // không uninstall driver trong lúc twai_receive đang chạy
    rxTaskBusy = true;

    if (!canReady) {
      rxTaskBusy = false;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    esp_err_t res = twai_receive(&msg, pdMS_TO_TICKS(50));

    rxTaskBusy = false;

    if (res != ESP_OK) continue;

    uint32_t now = millis();

    bool ext = msg.flags & TWAI_MSG_FLAG_EXTD;
    bool rtr = msg.flags & TWAI_MSG_FLAG_RTR;

    uint8_t len = msg.data_length_code;

    if (len > 8) {
      len = 8;
    }

    updateRxRow(msg.identifier, ext, rtr, len, msg.data, now);

    if (!ext && !rtr) {
      updateDoorFrame(msg.identifier, len, msg.data, now);
    }
  }
}

// Heartbeat 0x102 / DLC 1 / A5 mỗi 200 ms, chạy độc lập với web và nút bấm.
// Tự phục hồi khi Bus-Off (xảy ra khi bus chưa có node ACK).
void canHeartbeatTask(void *arg) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(HB_PERIOD_MS));

    hbTaskBusy = true;

    if (!canReady) {
      hbTaskBusy = false;
      continue;
    }

    twai_status_info_t st;

    if (twai_get_status_info(&st) == ESP_OK) {
      if (st.state == TWAI_STATE_BUS_OFF) {
        twai_initiate_recovery();
        hbRecover++;
      } else if (st.state == TWAI_STATE_STOPPED) {
        twai_start();
      } else if (st.state == TWAI_STATE_RUNNING) {
        twai_message_t m = {};
        m.identifier = DOOR_HB_ID;
        m.data_length_code = 1;
        m.data[0] = 0xA5;

        if (twai_transmit(&m, 0) == ESP_OK) hbSent++;
        else                                hbFail++;
      }
    }

    hbTaskBusy = false;
  }
}

void processAutoTx() {
  if (!canReady) return;

  uint32_t now = millis();

  for (int i = 0; i < MAX_AUTO_JOBS; i++) {
    if (!txJobs[i].used) continue;

    int idx = txJobs[i].rowIndex;

    if (!txIndexValid(idx)) {
      txJobs[i].used = false;
      continue;
    }

    if ((int32_t)(now - txJobs[i].nextMs) >= 0) {
      String err;
      sendTxRow(idx, txJobs[i].intervalMs, err);

      txJobs[i].nextMs += txJobs[i].intervalMs;

      if ((int32_t)(now - txJobs[i].nextMs) >= 0) {
        txJobs[i].nextMs = now + txJobs[i].intervalMs;
      }
    }
  }
}

// ================== JSON ==================
void appendRowJson(String &j, const CanRow &r, bool isTx, int idx) {
  j += "{";

  j += "\"idx\":";
  j += String(idx);
  j += ",";

  j += "\"type\":\"";
  if (r.ext) j += "EXT";
  else       j += "STD";
  if (r.rtr) j += " RTR";
  j += "\",";

  j += "\"canid\":\"";
  j += hexId(r.id, r.ext);
  j += "h\",";

  j += "\"len\":";
  j += String(r.len);
  j += ",";

  j += "\"name\":\"";
  appendEscaped(j, frameName(r.id, r.ext));
  j += "\",";

  j += "\"data\":\"";
  appendEscaped(j, dataString(r.data, r.len, r.rtr));
  j += "\",";

  j += "\"cycle\":";
  j += String(r.cycleMs);
  j += ",";

  if (isTx) {
    j += "\"auto\":";
    j += isRowAuto(idx) ? "true" : "false";
    j += ",";
  }

  j += "\"count\":";
  j += String(r.count);

  j += "}";
}

// ================== HTML: DC MOTOR DASHBOARD ==================
// Presentation only: Phase 4A is NOT implemented by the current door protocol.
static const char MOTOR_HTML[] PROGMEM = R"MOTOR(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b1017">
<title>Motor Control</title>
<style>
:root{color-scheme:dark;--bg:#0b1017;--panel:#121b26;--line:#293748;--text:#e8edf5;--muted:#9baabd;--accent:#6ae2c3;--amber:#f5bd68;--red:#ff8a91}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.5 "Segoe UI",system-ui,sans-serif}
button,input,select{font:inherit;border:1px solid var(--line);border-radius:7px;background:#192534;color:var(--text);padding:9px 12px}
button,input,select,summary{touch-action:manipulation}
button{cursor:pointer;font-weight:600}
button:hover:enabled{background:#26384b;border-color:#64819b}
button:disabled,input:disabled,select:disabled{cursor:not-allowed;color:#8c9bad;background:#161e29}
button:disabled{border-style:dashed}
button:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
input,select{min-width:0;width:100%}
label{display:grid;gap:6px;font-size:12px;color:var(--muted)}
h1,h2,p{margin:0}h2{font-size:16px;font-weight:600}
.muted,small{color:var(--muted)}small{font-size:12px}
td{font-family:Consolas,"SFMono-Regular",monospace}
.shell{max-width:1440px;margin:auto;padding:18px 28px 28px}
.header{display:flex;align-items:center;justify-content:flex-end;gap:12px;min-height:32px;margin-bottom:14px;flex-wrap:wrap}
.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip-path:inset(50%);white-space:nowrap;border:0}
.linknote{color:var(--amber);font-size:12px;margin-right:auto}
.chip{display:inline-flex;align-items:center;gap:7px;border:1px solid var(--line);border-radius:20px;padding:5px 10px;font-size:11px;letter-spacing:.5px;white-space:nowrap;color:var(--muted)}
.chip:before{content:"";width:6px;height:6px;border-radius:100%;background:currentColor}
.ok{color:var(--accent)}.warn{color:var(--amber)}.error{color:var(--red)}
.cards{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px;margin-bottom:18px}
.card,.panel,details{background:var(--panel);border:1px solid var(--line);border-radius:12px}
.card{padding:16px 20px}.card .label{color:var(--muted);font-size:12px}
.value{font-size:24px;font-weight:650;margin:6px 0 0;line-height:1.2;overflow-wrap:anywhere}.card small{display:block}
.grid{display:grid;grid-template-columns:minmax(0,1.4fr) minmax(0,1fr);gap:18px;margin-bottom:18px}
.panel{padding:20px;min-width:0}
.panelhead{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px;flex-wrap:wrap}
.controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:14px 0}
.stop{grid-column:1/-1;min-height:52px;font-size:18px;letter-spacing:1px}
.stop:disabled{background:#351f28;border:1px solid #8c4d5b;color:#ffb7be}.command:disabled{min-height:42px}
.duty{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:14px}.unit{display:flex;align-items:center;gap:8px}.unit input{width:100%}
.diag{display:grid;grid-template-columns:1fr 1fr;gap:0 20px;margin:0}
.diag div{padding:10px 0;border-bottom:1px solid #213040}.diag dt{color:var(--muted);font-size:11px}.diag dd{margin:3px 0 0;font-family:Consolas,monospace;font-size:16px}
.toolbar{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.toolbar label{display:flex;align-items:center;gap:8px}.toolbar input{width:130px}.toolbar select{width:88px}
.tablewrap{overflow:auto;max-height:370px;border:1px solid var(--line);border-radius:8px;margin-top:14px}
table{border-collapse:collapse;width:100%;white-space:nowrap;font-size:12px;text-align:left}
th{position:sticky;top:0;background:#1b2837;color:var(--muted);font:600 10px "Segoe UI",sans-serif;letter-spacing:.7px;text-transform:uppercase;z-index:1}
th,td{padding:11px 12px;border-bottom:1px solid #233143}tbody tr:hover{background:#182736}
.empty{text-align:center;color:var(--muted);padding:30px}.rx{color:var(--accent)}.tx{color:#8dbaff}
.footnote{margin-top:10px;font-size:11px;color:var(--muted)}
details{margin-top:14px;min-width:0}summary{padding:16px 20px;cursor:pointer;font-weight:600}
.detailbody{padding:0 20px 20px}
.formgrid{display:grid;grid-template-columns:120px 125px minmax(180px,1fr) 120px;gap:12px;margin:14px 0}
.actions{display:flex;gap:9px;flex-wrap:wrap;margin:12px 0}.primary{background:#225047;color:#b5fbe5;border-color:#387a69}.danger{color:var(--red)}
#actionMessage{min-height:24px;overflow-wrap:anywhere}.stale table{opacity:.55}
.settings{display:flex;align-items:end;gap:12px;flex-wrap:wrap}.settings label{width:180px}
@media(max-width:960px){.shell{padding:16px 20px}.cards{grid-template-columns:repeat(2,minmax(0,1fr))}.grid{grid-template-columns:1fr}.formgrid{grid-template-columns:1fr 1fr}}
@media(max-width:520px){.shell{padding:12px}.header{gap:8px}.card{padding:14px}.value{font-size:20px}.panel{padding:16px}.toolbar{align-items:flex-start}.toolbar label{font-size:11px}.duty{grid-template-columns:1fr}.formgrid{grid-template-columns:1fr}.detailbody{padding:0 16px 16px}.diag{gap:0 10px}}
@media(max-width:340px){.cards{grid-template-columns:1fr}.controls{grid-template-columns:1fr}}
@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important}}
</style>
</head>
<body>
<main class="shell">
  <header class="header">
    <h1 class="sr-only">Motor control</h1>
    <span id="connectionNotice" class="linknote" role="status">Connecting to device</span>
    <span id="browserLink" class="chip" role="status">CONNECTING</span>
  </header>
  <section class="cards" aria-label="System overview">
    <article class="card"><div class="label" title="Local CAN controller; not confirmation of motor connectivity">CAN</div><div id="canValue" class="value">Unknown</div><small id="canDetail">Waiting for device</small></article>
    <article class="card"><div class="label">Motor</div><div class="value muted" id="motorState">Unavailable</div></article>
    <article class="card"><div class="label">Duty</div><div class="value muted" id="requestedDuty">&mdash; <small style="display:inline">%</small></div></article>
    <article class="card"><div class="label">Faults</div><div class="value warn" id="motorFault">Unknown</div></article>
  </section>
  <div class="grid">
    <section class="panel" aria-labelledby="controlTitle">
      <div class="panelhead"><h2 id="controlTitle">Motor Control</h2><span class="chip warn">UNAVAILABLE</span></div>
      <p class="muted" id="motorHelp">Motor controls unavailable: protocol not integrated.</p>
      <div id="motorCommands" class="controls" aria-describedby="motorHelp"></div>
      <div class="duty"><label>Duty<div class="unit"><input type="number" min="0" max="100" placeholder="Unavailable" disabled aria-describedby="motorHelp"><span>%</span></div></label><label>Max duty<div class="unit"><input type="number" min="0" max="100" placeholder="Unavailable" disabled aria-describedby="motorHelp"><span>%</span></div></label></div>
    </section>
    <section class="panel" aria-labelledby="diagTitle">
      <div class="panelhead"><h2 id="diagTitle">Diagnostics</h2><span id="snapshotAge" class="chip">NO SNAPSHOT</span></div>
      <dl class="diag">
        <div><dt title="Received frames / transmit queue acceptances, excluding heartbeat">RX / TX queued</dt><dd id="totals">&mdash; / &mdash;</dd></div><div><dt>Cyclic jobs</dt><dd id="activeJobs">&mdash; / 8</dd></div>
        <div><dt>TX / RX errors</dt><dd id="errors">&mdash; / &mdash;</dd></div><div><dt>Bus errors / missed RX</dt><dd id="busErrors">&mdash; / &mdash;</dd></div>
        <div><dt>Heartbeat sent / failed</dt><dd id="heartbeat">&mdash; / &mdash;</dd></div><div><dt>Bus recoveries</dt><dd id="recoveries">&mdash;</dd></div>
        <div><dt>Uptime</dt><dd id="uptime">&mdash;</dd></div><div><dt>Bitrate</dt><dd id="bitrate">&mdash;</dd></div>
      </dl>
      
    </section>
  </div>
  <section class="panel" aria-labelledby="monitorTitle" id="monitorPanel">
    <div class="panelhead"><div><h2 id="monitorTitle">CAN Monitor</h2></div><div class="toolbar"><label><span class="sr-only">Direction</span><select id="direction"><option value="ALL">All</option><option>RX</option><option>TX</option></select></label><label><span class="sr-only">ID filter</span><input id="idFilter" placeholder="Filter ID" maxlength="10"></label><button id="pauseMonitor" type="button" aria-pressed="false">Pause view</button></div></div>
    <div class="tablewrap"><table><thead><tr><th scope="col">Timestamp (ms)</th><th scope="col">Dir</th><th scope="col">CAN ID</th><th scope="col">Type</th><th scope="col">DLC</th><th scope="col">Payload (hex)</th><th scope="col">Count</th><th scope="col">Cycle (ms)</th></tr></thead><tbody id="monitorBody"><tr><td colspan="8" class="empty">Waiting for CAN data</td></tr></tbody></table></div>
    <p id="monitorNote" class="footnote" title="Aggregated snapshots, not a lossless trace. Direct door TX and heartbeat are not retained as rows. RX time follows reception; TX time indicates queue acceptance.">Latest frames &middot; uptime in ms &middot; TX: manual/cyclic rows only</p>
  </section>
  <details id="debugPanel"><summary>CAN Tools</summary><div class="detailbody">
    <p class="muted">Add / Save stores a row. Send / Enable cyclic transmits it.</p>
    <form id="txForm"><div class="formgrid"><label>Frame type<select id="frameType"><option>STD</option><option>EXT</option><option value="STD_RTR">STD RTR</option><option value="EXT_RTR">EXT RTR</option></select></label><label>CAN ID (hex)<input id="frameId" value="336" maxlength="10" required autocomplete="off"></label><label>Payload (hex, 0&ndash;8 bytes)<input id="frameData" placeholder="01 02 03 04" maxlength="64" autocomplete="off"></label><label>Cycle (ms)<input id="frameCycle" type="number" min="10" max="2147483647" value="1000" required></label></div>
    <div class="actions"><button id="addRow" class="primary" type="submit">Add row</button><button id="saveRow" type="button">Save selected</button><button id="sendRow" type="button">Send selected</button><button id="autoRow" type="button">Enable cyclic</button><button id="deleteRow" class="danger" type="button">Delete selected</button><button id="newDraft" type="button">New draft</button></div></form>
    <div id="actionMessage" role="status" class="muted">Select a row or add a new one.</div>
    <div class="tablewrap"><table><thead><tr><th scope="col">Select</th><th scope="col">Type</th><th scope="col">ID</th><th scope="col">DLC</th><th scope="col">Payload</th><th scope="col">Cycle (ms)</th><th scope="col">Auto</th><th scope="col">Count</th></tr></thead><tbody id="debugBody"><tr><td colspan="8" class="empty">No TX rows</td></tr></tbody></table></div>
    <div class="actions"><button id="clearRx" type="button">Clear RX</button><button id="clearTx" type="button" class="danger">Clear TX / stop cyclic</button></div>
  </div></details>
  <details><summary>Settings</summary><div class="detailbody"><div class="settings"><label>Bitrate<select id="bitrateSelect"><option value="125">125 kbit/s</option><option value="250">250 kbit/s</option><option value="500" selected>500 kbit/s</option><option value="1000">1000 kbit/s</option></select></label><button id="applyBitrate" type="button">Apply bitrate</button></div><p class="footnote">Applying briefly interrupts CAN communication.</p><p id="deviceInfo" class="footnote"></p></div></details>
  
</main>
<script>
'use strict';
const $ = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const STALE_MS = 2500;
let snapshot = null, lastSuccess = 0, disconnected = false, busy = false, paused = false;
let selected = -1, frozenRows = null, pollPending = null, configEditing = false;

// Capability boundary. No opcodes, guessed response parsing or motor POST path.
// A future protocol adapter must keep these disabled until real integration exists.
const motorCapability = Object.freeze({available:false, reason:'Motor protocol not integrated.'});
const motorCommands = Object.freeze([
  ['STOP','STOP'],['START_FORWARD','Start forward'],['START_REVERSE','Start reverse'],
  ['SET_DUTY','Set duty'],['SET_MAX_DUTY','Set max duty'],['GET_STATUS','Get status'],['CLEAR_FAULT','Clear fault']
]);
function renderMotorCommands() {
  $('motorCommands').innerHTML = motorCommands.map(([cmd,label]) => `<button type="button" class="command ${cmd === 'STOP' ? 'stop' : ''}" data-motor="${cmd}" disabled aria-describedby="motorHelp" title="${motorCapability.reason}">${label}</button>`).join('');
}

// HTTP transport. POSTs are never retried: an interrupted reply can be ambiguous.
async function request(path, params) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 1800);
  try {
    const options = {cache:'no-store', signal:controller.signal};
    if (params) Object.assign(options, {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:new URLSearchParams(params)});
    const response = await fetch(path, options);
    const data = await response.json();
    if (!response.ok || data.ok !== true) throw new Error(data.msg || 'Device rejected request');
    return data;
  } finally { clearTimeout(timer); }
}
function fresh() { return !!snapshot && !disconnected && Date.now() - lastSuccess <= STALE_MS; }
function selectedRow() { return snapshot && snapshot.tx.find(row => Number(row.idx) === selected); }
function message(text, tone = 'muted') { $('actionMessage').textContent = text; $('actionMessage').className = tone; }
function updateLocks() {
  const row = selectedRow(), online = fresh(), locked = !!(row && row.auto);
  ['frameType','frameId','frameCycle'].forEach(id => $(id).disabled = busy || locked);
  $('frameData').disabled = busy || locked || $('frameType').value.includes('RTR');
  $('addRow').disabled = !online || busy || locked;
  $('saveRow').disabled = !online || busy || !row || locked;
  $('deleteRow').disabled = !online || busy || !row || locked;
  ['sendRow','autoRow'].forEach(id => $(id).disabled = !online || busy || !row);
  $('autoRow').textContent = locked ? 'Stop cyclic' : 'Enable cyclic';
  ['clearRx','clearTx','applyBitrate'].forEach(id => $(id).disabled = !online || busy);
  $('newDraft').disabled = busy;
  document.querySelectorAll('[data-select]').forEach(button => button.disabled = busy);
  // No browser or CAN status enables unsupported motor controls.
}
function renderConnection() {
  const age = snapshot ? Date.now() - lastSuccess : 0;
  const stale = !!snapshot && age > STALE_MS;
  const text = disconnected ? 'DISCONNECTED' : stale ? 'STALE' : snapshot ? 'CONNECTED' : 'CONNECTING';
  $('browserLink').textContent = text;
  $('browserLink').className = 'chip ' + (fresh() ? 'ok' : 'warn');
  $('snapshotAge').textContent = snapshot ? (stale || disconnected ? 'OLD / ' : '') + (age / 1000).toFixed(1) + ' s ago' : 'NO SNAPSHOT';
  $('snapshotAge').className = 'chip ' + (fresh() ? 'ok' : 'warn');
  $('canValue').textContent = snapshot ? snapshot.can : 'Unknown';
  $('canValue').className = 'value ' + (!fresh() ? 'muted' : snapshot.can === 'RUNNING' ? 'ok' : 'error');
  $('canDetail').textContent = !snapshot ? 'Waiting' : !fresh() ? 'Last known / stale' : snapshot.bitrate + ' kbit/s';
  $('connectionNotice').hidden = fresh();
  $('connectionNotice').textContent = disconnected ? 'Offline / data may be old' : stale ? 'Stale data' : 'Connecting to device';
  $('monitorPanel').classList.toggle('stale', !fresh());
  updateLocks();
}
function monitorRows(data) {
  return [...data.rx.map(row => ({...row, direction:'RX'})), ...data.tx.filter(row => row.count > 0).map(row => ({...row, direction:'TX'}))]
    .sort((a,b) => ((data.uptime - a.lastMs) >>> 0) - ((data.uptime - b.lastMs) >>> 0));
}
function renderMonitor() {
  const rows = paused ? frozenRows : snapshot ? monitorRows(snapshot) : [];
  const filter = $('idFilter').value.trim().replace(/^0x/i,'').replace(/h$/i,'').toUpperCase();
  const direction = $('direction').value;
  const visible = (rows || []).filter(row => (direction === 'ALL' || row.direction === direction) && String(row.canid).toUpperCase().includes(filter));
  $('monitorBody').innerHTML = visible.length ? visible.map(row => `<tr><td>${esc(row.lastMs)}</td><td class="${row.direction.toLowerCase()}">${row.direction}</td><td>${esc(row.canid)}</td><td>${esc(row.type)}</td><td>${esc(row.len)}</td><td>${esc(row.data || '\u2014')}</td><td>${esc(row.count)}</td><td>${esc(row.cycle)}</td></tr>`).join('') : '<tr><td colspan="8" class="empty">No frames</td></tr>';
  $('pauseMonitor').textContent = paused ? 'Resume view' : 'Pause view';
  $('pauseMonitor').setAttribute('aria-pressed', String(paused));
  $('monitorNote').textContent = (paused ? 'View paused / CAN remains active. ' : '') + 'Latest frames · uptime in ms · TX: manual/cyclic rows only';
}
function renderDebug() {
  const rows = snapshot ? snapshot.tx : [];
  $('debugBody').innerHTML = rows.length ? rows.map(row => `<tr><td><button type="button" data-select="${Number(row.idx)}" aria-pressed="${Number(row.idx) === selected}">${Number(row.idx) === selected ? 'Selected' : 'Edit'}</button></td><td>${esc(row.type)}</td><td>${esc(row.canid)}</td><td>${esc(row.len)}</td><td>${esc(row.data || '\u2014')}</td><td>${esc(row.cycle)}</td><td class="${row.auto ? 'warn' : 'muted'}">${row.auto ? 'ACTIVE' : 'Off'}</td><td>${esc(row.count)}</td></tr>`).join('') : '<tr><td colspan="8" class="empty">No TX rows</td></tr>';
  if (selected >= 0 && !selectedRow()) { selected = -1; message('Row removed. Draft retained.'); }
  updateLocks();
}
function renderSnapshot(data) {
  $('totals').textContent = data.rxTotal + ' / ' + data.txTotal;
  $('activeJobs').textContent = data.tx.filter(row => row.auto).length + ' / 8';
  $('errors').textContent = data.txErr + ' / ' + data.rxErr;
  $('busErrors').textContent = data.busErr + ' / ' + data.rxMiss;
  $('heartbeat').textContent = data.hbSent + ' / ' + data.hbFail;
  $('recoveries').textContent = data.hbRecover;
  $('uptime').textContent = (data.uptime / 1000).toFixed(1) + ' s';
  $('bitrate').textContent = data.bitrate + ' kbit/s';
  $('deviceInfo').textContent = data.ip + ' / FW ' + data.fw;
  if (!configEditing) $('bitrateSelect').value = String(data.bitrate);
  renderConnection(); renderMonitor(); renderDebug();
}
async function pollOnce() {
  if (pollPending) return pollPending;
  pollPending = (async () => {
    try {
      const data = await request('/api/dashboard/state');
      if (!Array.isArray(data.rx) || !Array.isArray(data.tx) || !Number.isFinite(data.uptime)) throw new Error('Invalid snapshot');
      snapshot = data; lastSuccess = Date.now(); disconnected = false; renderSnapshot(data);
    } catch (error) { disconnected = true; renderConnection(); }
  })();
  try { await pollPending; } finally { pollPending = null; }
}
async function pollLoop() { await pollOnce(); setTimeout(pollLoop, document.hidden ? 2000 : 600); }

// Raw CAN tools use the existing validated API. No motor actions enter this path.
function draftParams() {
  const type = $('frameType').value;
  return {id:$('frameId').value.trim(), data:$('frameData').value.trim(), cycle:$('frameCycle').value,
    ext:type.includes('EXT') ? '1' : '0', rtr:type.includes('RTR') ? '1' : '0'};
}
function selectRow(idx) {
  if (busy) return;
  const row = snapshot && snapshot.tx.find(item => Number(item.idx) === idx);
  if (!row) return;
  selected = idx;
  $('frameType').value = String(row.type).replace(' ','_');
  $('frameId').value = String(row.canid).replace(/h$/i,'');
  $('frameData').value = String(row.type).includes('RTR') ? '' : row.data;
  $('frameCycle').value = row.cycle;
  message('Row ' + idx + ' selected. Save edits before sending.');
  renderDebug();
}
async function mutate(path, params, onSuccess) {
  if (busy || !fresh()) return;
  busy = true; updateLocks(); message('Request in progress\u2026');
  try {
    const data = await request(path, params);
    if (onSuccess) onSuccess(data);
    message(data.msg || 'Request accepted', 'ok');
    // A GET already in flight can predate this mutation. Finish it, then obtain
    // a post-action snapshot before releasing the busy guard.
    if (pollPending) await pollPending;
    await pollOnce();
  } catch (error) {
    message('Request not confirmed: ' + error.message + '. Check device state before retrying.', 'error');
  } finally { busy = false; updateLocks(); }
}
$('txForm').addEventListener('submit', event => { event.preventDefault(); if (!$('addRow').disabled) mutate('/api/tx/add', draftParams(), data => { selected = Number(data.idx); }); });
$('saveRow').addEventListener('click', () => { if (!$('saveRow').disabled) mutate('/api/tx/update', {...draftParams(), idx:selected}); });
$('sendRow').addEventListener('click', () => { if (!$('sendRow').disabled) mutate('/api/tx/send', {idx:selected}); });
$('deleteRow').addEventListener('click', () => { if (!$('deleteRow').disabled) mutate('/api/tx/delete', {idx:selected}, () => { selected = -1; }); });
$('autoRow').addEventListener('click', () => {
  const row = selectedRow(); if ($('autoRow').disabled || !row) return;
  mutate('/api/tx/auto', {...draftParams(), idx:selected, enable:row.auto ? '0' : '1'});
});
$('newDraft').addEventListener('click', () => { if (busy) return; selected = -1; message('New draft'); renderDebug(); });
$('frameType').addEventListener('change', updateLocks);
$('debugBody').addEventListener('click', event => { const button = event.target.closest('[data-select]'); if (button) selectRow(Number(button.dataset.select)); });
$('clearRx').addEventListener('click', () => mutate('/api/clear_rx', {}));
$('clearTx').addEventListener('click', () => mutate('/api/clear_tx', {}, () => { selected = -1; }));
$('bitrateSelect').addEventListener('change', () => { configEditing = true; });
$('applyBitrate').addEventListener('click', () => mutate('/api/config', {bitrate:$('bitrateSelect').value}, () => { configEditing = false; }));
$('pauseMonitor').addEventListener('click', () => { paused = !paused; frozenRows = paused && snapshot ? monitorRows(snapshot) : null; renderMonitor(); });
$('direction').addEventListener('change', renderMonitor);
$('idFilter').addEventListener('input', renderMonitor);
document.addEventListener('keydown', event => { if (event.repeat && event.target.tagName === 'BUTTON') event.preventDefault(); });
renderMotorCommands(); renderConnection(); pollLoop();
setInterval(renderConnection, 250);
</script>
</body>
</html>
)MOTOR";


// ================== READ-ONLY DASHBOARD API ==================
void handleMotorPage() {
  server.send_P(200, "text/html; charset=utf-8", MOTOR_HTML);
}

void appendDashboardRowJson(String &j, const CanRow &row, bool isTx, int idx) {
  appendRowJson(j, row, isTx, idx);
  j.remove(j.length() - 1);  // Extend only the new endpoint; legacy JSON is unchanged.
  j += ",\"lastMs\":";
  j += String(row.lastMs);
  j += "}";
}

void handleDashboardState() {
  // Fixed storage, used only by the single WebServer loop. Never serialize with
  // rxMutex held: RX processing and the independent heartbeat retain their tasks.
  static CanRow snapshotRx[MAX_RX_ROWS];
  uint32_t now, snapshotRxTotal;
  xSemaphoreTake(rxMutex, portMAX_DELAY);
  memcpy(snapshotRx, rxRows, sizeof(snapshotRx));
  now = millis();
  snapshotRxTotal = rxTotal;
  xSemaphoreGive(rxMutex);

  twai_status_info_t status = {};
  const bool haveStatus = canReady && twai_get_status_info(&status) == ESP_OK;
  String j;
  j.reserve(32000);
  j += "{\"ok\":true,\"can\":\"";
  j += haveStatus ? canStateName(status.state) : "NOT_READY";
  j += "\",\"ip\":\"";
  j += WiFi.softAPIP().toString();
  j += "\",\"fw\":\"" __DATE__ " " __TIME__ "\",\"uptime\":";
  j += String(now);
  j += ",\"bitrate\":";
  j += String(currentBitrate);
  j += ",\"rxTotal\":";
  j += String(snapshotRxTotal);
  j += ",\"txTotal\":";
  j += String(txTotal);
  j += ",\"txErr\":";
  j += String(status.tx_error_counter);
  j += ",\"rxErr\":";
  j += String(status.rx_error_counter);
  j += ",\"busErr\":";
  j += String(status.bus_error_count);
  j += ",\"rxMiss\":";
  j += String(status.rx_missed_count);
  j += ",\"hbSent\":";
  j += String((uint32_t)hbSent);
  j += ",\"hbFail\":";
  j += String((uint32_t)hbFail);
  j += ",\"hbRecover\":";
  j += String((uint32_t)hbRecover);
  j += ",\"tx\":[";
  bool first = true;
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (!txRows[i].used) continue;
    if (!first) j += ",";
    appendDashboardRowJson(j, txRows[i], true, i);
    first = false;
  }
  j += "],\"rx\":[";
  first = true;
  for (int i = 0; i < MAX_RX_ROWS; ++i) {
    if (!snapshotRx[i].used) continue;
    if (!first) j += ",";
    appendDashboardRowJson(j, snapshotRx[i], false, i);
    first = false;
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", j);
}


// ================== API ==================
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleState() {
  String canState = "NOT_READY";

  uint32_t txErr = 0;
  uint32_t rxErr = 0;
  uint32_t busErr = 0;
  uint32_t rxMiss = 0;

  if (canReady) {
    twai_status_info_t st;

    if (twai_get_status_info(&st) == ESP_OK) {
      canState = canStateName(st.state);
      txErr = st.tx_error_counter;
      rxErr = st.rx_error_counter;
      busErr = st.bus_error_count;
      rxMiss = st.rx_missed_count;
    }
  }

  String j;
  j.reserve(32000);

  j += "{";

  j += "\"ok\":true,";

  j += "\"ip\":\"";
  j += WiFi.softAPIP().toString();
  j += "\",";

  j += "\"fw\":\"";
  j += __DATE__ " " __TIME__;
  j += "\",";

  j += "\"can\":\"";
  j += canState;
  j += "\",";

  j += "\"bitrate\":";
  j += String(currentBitrate);
  j += ",";

  j += "\"rxTotal\":";
  j += String(rxTotal);
  j += ",";

  j += "\"txTotal\":";
  j += String(txTotal);
  j += ",";

  j += "\"txErr\":";
  j += String(txErr);
  j += ",";

  j += "\"rxErr\":";
  j += String(rxErr);
  j += ",";

  j += "\"busErr\":";
  j += String(busErr);
  j += ",";

  j += "\"rxMiss\":";
  j += String(rxMiss);
  j += ",";

  j += "\"tx\":[";
  bool first = true;

  for (int i = 0; i < MAX_ROWS; i++) {
    if (!txRows[i].used) continue;

    if (!first) j += ",";
    appendRowJson(j, txRows[i], true, i);
    first = false;
  }

  j += "],";

  j += "\"rx\":[";
  first = true;

  xSemaphoreTake(rxMutex, portMAX_DELAY);

  for (int i = 0; i < MAX_RX_ROWS; i++) {
    if (!rxRows[i].used) continue;

    if (!first) j += ",";
    appendRowJson(j, rxRows[i], false, i);
    first = false;
  }

  xSemaphoreGive(rxMutex);

  j += "]";

  j += "}";

  server.send(200, "application/json; charset=utf-8", j);
}

void handleConfig() {
  uint32_t bitrate = (uint32_t)server.arg("bitrate").toInt();

  if (!(bitrate == 125 || bitrate == 250 || bitrate == 500 || bitrate == 1000)) {
    jsonReplyConfig(false, "Bitrate không hợp lệ", currentBitrate);
    return;
  }

  if (restartCan(bitrate)) {
    jsonReplyConfig(true, "Đã đổi tốc độ CAN", currentBitrate);
  } else {
    jsonReplyConfig(false, "Đổi tốc độ CAN thất bại", currentBitrate);
  }
}

void handleTxAdd() {
  int idx = findFreeTxRow();

  if (idx < 0) {
    jsonReply(false, "Bảng TX đã đầy");
    return;
  }

  memset(&txRows[idx], 0, sizeof(CanRow));

  String err;

  if (!updateTxRowFromArgs(idx, true, err)) {
    memset(&txRows[idx], 0, sizeof(CanRow));
    jsonReply(false, err);
    return;
  }

  jsonReplyWithIdx(true, "Đã thêm message TX", idx);
}

void handleTxUpdate() {
  if (!server.hasArg("idx")) {
    jsonReply(false, "Thiếu idx");
    return;
  }

  int idx = server.arg("idx").toInt();

  if (!txIndexValid(idx)) {
    jsonReply(false, "TX row không hợp lệ");
    return;
  }

  if (isRowAuto(idx)) {
    jsonReply(false, "Message đang Auto, bỏ tích Auto trước khi sửa");
    return;
  }

  String err;

  if (!updateTxRowFromArgs(idx, true, err)) {
    jsonReply(false, err);
    return;
  }

  jsonReply(true, "Đã lưu message TX");
}

void handleTxSend() {
  if (!server.hasArg("idx")) {
    jsonReply(false, "Thiếu idx");
    return;
  }

  int idx = server.arg("idx").toInt();
  String err;

  if (!sendTxRow(idx, 0, err)) {
    jsonReply(false, err);
    return;
  }

  jsonReply(true, "Đã gửi message TX");
}

void handleTxDelete() {
  if (!server.hasArg("idx")) {
    jsonReply(false, "Thiếu idx");
    return;
  }

  int idx = server.arg("idx").toInt();

  if (!txIndexValid(idx)) {
    jsonReply(false, "TX row không hợp lệ");
    return;
  }

  if (isRowAuto(idx)) {
    jsonReply(false, "Message đang Auto, bỏ tích Auto trước khi xóa");
    return;
  }

  memset(&txRows[idx], 0, sizeof(CanRow));

  jsonReply(true, "Đã xóa message TX");
}

void handleTxAuto() {
  if (!server.hasArg("idx")) {
    jsonReply(false, "Thiếu idx");
    return;
  }

  int idx = server.arg("idx").toInt();

  if (!txIndexValid(idx)) {
    jsonReply(false, "TX row không hợp lệ");
    return;
  }

  bool enable = argTrue(server.arg("enable"));

  if (enable) {
    if (isRowAuto(idx)) {
      jsonReply(true, "Auto TX đang bật");
      return;
    }

    String err;

    if (!updateTxRowFromArgs(idx, false, err)) {
      jsonReply(false, err);
      return;
    }

    if (txRows[idx].cycleMs < 10) {
      jsonReply(false, "Cycle tối thiểu 10 ms");
      return;
    }

    int slot = findFreeAutoJob();

    if (slot < 0) {
      jsonReply(false, "Đã đầy danh sách Auto TX");
      return;
    }

    txJobs[slot].used = true;
    txJobs[slot].rowIndex = idx;
    txJobs[slot].intervalMs = txRows[idx].cycleMs;
    txJobs[slot].nextMs = millis() + txRows[idx].cycleMs;

    if (!sendTxRow(idx, txRows[idx].cycleMs, err)) {
      txJobs[slot].used = false;
      jsonReply(false, err);
      return;
    }

    jsonReply(true, "Đã bật Auto TX");
  } else {
    stopAutoJobByRow(idx);
    jsonReply(true, "Đã tắt Auto TX");
  }
}

void handleClearTx() {
  memset(txRows, 0, sizeof(txRows));
  memset(txJobs, 0, sizeof(txJobs));

  txTotal = 0;

  jsonReply(true, "Đã clear TX");
}

void handleClearRx() {
  xSemaphoreTake(rxMutex, portMAX_DELAY);

  memset(rxRows, 0, sizeof(rxRows));
  rxTotal = 0;

  xSemaphoreGive(rxMutex);

  jsonReply(true, "Đã clear RX");
}

// ================== DOOR API ==================
void handleDoorPage() {
  server.send_P(200, "text/html; charset=utf-8", DOOR_HTML);
}

void appendDoorFrameJson(String &j, const DoorFrame &f, uint32_t now) {
  int32_t age = f.valid ? (int32_t)(now - f.lastMs) : 0;
  if (age < 0) age = 0;

  j += "{\"valid\":";
  j += f.valid ? "true" : "false";
  j += ",\"age\":";
  j += String((uint32_t)age);
  j += ",\"count\":";
  j += String(f.count);
  j += ",\"bad\":";
  j += String(f.bad);
  j += ",\"data\":[";

  for (uint8_t i = 0; i < f.len && f.valid; i++) {
    if (i) j += ",";
    j += String(f.data[i]);
  }

  j += "]}";
}

void handleDoorState() {
  String canState = "NOT_READY";
  uint32_t txErr = 0, rxErr = 0, busErr = 0, rxMiss = 0;

  if (canReady) {
    twai_status_info_t st;

    if (twai_get_status_info(&st) == ESP_OK) {
      canState = canStateName(st.state);
      txErr = st.tx_error_counter;
      rxErr = st.rx_error_counter;
      busErr = st.bus_error_count;
      rxMiss = st.rx_missed_count;
    }
  }

  DoorFrame f101, f103;
  uint32_t now;

  xSemaphoreTake(rxMutex, portMAX_DELAY);
  now = millis();               // lấy sau khi giữ mutex => now >= lastMs
  f101 = door101;
  f103 = door103;
  xSemaphoreGive(rxMutex);

  String j;
  j.reserve(900);

  j += "{\"ok\":true,\"can\":\"";
  j += canState;
  j += "\",\"bitrate\":";
  j += String(currentBitrate);
  j += ",\"uptime\":";
  j += String(now);
  j += ",\"hbSent\":";
  j += String((uint32_t)hbSent);
  j += ",\"hbFail\":";
  j += String((uint32_t)hbFail);
  j += ",\"hbRecover\":";
  j += String((uint32_t)hbRecover);
  j += ",\"cmdTotal\":";
  j += String((uint32_t)doorCmdTotal);
  j += ",\"txErr\":";
  j += String(txErr);
  j += ",\"rxErr\":";
  j += String(rxErr);
  j += ",\"busErr\":";
  j += String(busErr);
  j += ",\"rxMiss\":";
  j += String(rxMiss);
  j += ",\"f101\":";
  appendDoorFrameJson(j, f101, now);
  j += ",\"f103\":";
  appendDoorFrameJson(j, f103, now);
  j += "}";

  server.send(200, "application/json; charset=utf-8", j);
}

// Bảng lệnh cố định phía ESP32: trình duyệt chỉ gửi tên lệnh, không gửi byte tùy ý.
// Mỗi request = đúng 1 CAN frame, không lặp lại.
void handleDoorCmd() {
  String c = server.arg("cmd");
  uint8_t d0 = 0, d1 = 0;
  const char *label = nullptr;

  if      (c == "lock")   { d0 = 0x03; d1 = 0x00; label = "LOCK"; }
  else if (c == "unlock") { d0 = 0x04; d1 = 0x00; label = "UNLOCK"; }
  else if (c == "open")   { d0 = 0x01; d1 = 0x50; label = "OPEN"; }
  else if (c == "close")  { d0 = 0x02; d1 = 0x50; label = "CLOSE"; }
  else if (c == "stop")   { d0 = 0x00; d1 = 0x00; label = "STOP"; }
  else if (c == "clear")  { d0 = 0x05; d1 = 0x00; label = "CLEAR FAULT"; }
  else {
    jsonReply(false, "Lệnh không hợp lệ");
    return;
  }

  CanRow r = {};
  r.used = true;
  r.id = DOOR_CMD_ID;
  r.len = 2;
  r.data[0] = d0;
  r.data[1] = d1;

  String err;

  if (!transmitFrame(r, err)) {
    jsonReply(false, err);
    return;
  }

  doorCmdTotal++;
  jsonReply(true, String("Đã gửi ") + label + " lên CAN, chờ S32K144 phản hồi");
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP32-S3 CAN Webserver starting...");
  Serial.printf("Build: %s %s\n", __DATE__, __TIME__);
  Serial.println("======================================");

  Serial.printf("Flash size: %lu MB\n",
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));

  Serial.printf("PSRAM found: %s\n",
                psramFound() ? "YES" : "NO");

  Serial.printf("PSRAM size: %lu MB\n",
                (unsigned long)(ESP.getPsramSize() / (1024 * 1024)));

  Serial.printf("CAN RX GPIO: %d\n", CAN_RX_PIN);
  Serial.printf("CAN TX GPIO: %d\n", CAN_TX_PIN);

  rxMutex = xSemaphoreCreateMutex();

  canReady = startCan(currentBitrate);

  if (canReady) {
    Serial.println("CAN/TWAI started OK");
  } else {
    Serial.println("CAN/TWAI start FAILED");
  }

  // Task nhận CAN chạy trên core 0 (loop Arduino chạy core 1)
  xTaskCreatePinnedToCore(canRxTask, "canRx", 4096, nullptr, 10, nullptr, 0);

  // Heartbeat 0x102 mỗi 200 ms, chạy ngầm ngay từ khi khởi động
  xTaskCreatePinnedToCore(canHeartbeatTask, "canHb", 3072, nullptr, 8, nullptr, 0);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.print("WiFi AP SSID: ");
  Serial.println(AP_SSID);

  Serial.print("WiFi AP PASS: ");
  Serial.println(AP_PASS);

  Serial.print("Open browser: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/config", HTTP_POST, handleConfig);

  server.on("/api/tx/add", HTTP_POST, handleTxAdd);
  server.on("/api/tx/update", HTTP_POST, handleTxUpdate);
  server.on("/api/tx/send", HTTP_POST, handleTxSend);
  server.on("/api/tx/delete", HTTP_POST, handleTxDelete);
  server.on("/api/tx/auto", HTTP_POST, handleTxAuto);

  server.on("/api/clear_tx", HTTP_POST, handleClearTx);
  server.on("/api/clear_rx", HTTP_POST, handleClearRx);

  server.on("/door", HTTP_GET, handleDoorPage);
  server.on("/api/door/state", HTTP_GET, handleDoorState);
  server.on("/api/door/cmd", HTTP_POST, handleDoorCmd);

  server.on("/motor", HTTP_GET, handleMotorPage);
  server.on("/api/dashboard/state", HTTP_GET, handleDashboardState);

  server.begin();

  Serial.println("HTTP server started");
  Serial.println("======================================");
}

void loop() {
  server.handleClient();
  processAutoTx();

  delay(1);
}