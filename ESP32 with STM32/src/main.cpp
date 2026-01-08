#include <WiFi.h>
#include <WebServer.h>
#include "SD_MMC.h"

// --- USER CONFIG (Kept from your working code) ---
const char* ssid = "iPhone";       
const char* password = "aassddff"; 

WebServer server(80);

// Enhanced system state
struct TelemetryData {
    int engaged;
    int angle;
    int pot_raw;
    int auto_mode;
    uint32_t stm32_ram;
    float distance;
    float current;   // Amperes (placeholder)
    float torque;    // Newton-meters (placeholder)
} data = {0, 0, 0, 1, 0, 0.0, 0.0, 0.0};

uint32_t session_start = 0;
uint32_t data_points = 0;
bool is_recording = false;
bool sd_card_ready = false;
String current_log_file = "";
String last_error = "";

// Rolling buffer for graph persistence (last 100 points)
struct DataPoint {
  int engaged;
  int angle;
  float distance;
} data_buffer[100];
int buffer_index = 0;
int buffer_count = 0;

// ================= PROFESSIONAL DASHBOARD HTML =================
// Contains custom JavaScript for Offline Graphing & UI Logic
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Musashi Telemetry</title>
  <style>
    :root { --bg: #717174ff; --card: #1e1e1e; --text: #ffffffff; --accent: #ffffffff; --green: #00ff88; --red: #ff4d4d; }
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; text-align: center; }
    
    /* LAYOUT GRID */
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; max-width: 1200px; margin: 0 auto; }
    .card { background-color: var(--card); border-radius: 12px; padding: 20px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); border: 1px solid #333; }
    
    h1 { color: var(--accent); letter-spacing: 2px; margin-bottom: 5px; text-transform: uppercase; font-size: 1.5rem; }
    h2 { font-size: 1rem; color: #ffe5e5ff; margin-top: 0; }
    h3 { margin: 0 0 10px 0; font-size: 0.9rem; color: #e9e9e9ff; text-align: left; border-bottom: 1px solid #333; padding-bottom: 5px; }

    /* BIG NUMBERS */
    .metric-value { font-size: 2.5rem; font-weight: bold; font-family: monospace; }
    .unit { font-size: 1rem; color: #adadadff; }
    
    /* DYNAMIC BUTTON */
    .btn-main {
      width: 100%; padding: 20px; font-size: 1.5rem; font-weight: bold; border: none; border-radius: 8px; cursor: pointer;
      transition: all 0.2s ease; text-transform: uppercase; box-shadow: 0 0 15px rgba(0,0,0,0.3);
    }
    .btn-eng { background: linear-gradient(135deg, var(--green), #00b359); color: #000; }
    .btn-dis { background: linear-gradient(135deg, var(--red), #b30000); color: #fff; }
    .btn-main:active { transform: scale(0.98); }

    /* GRAPHS */
    canvas { width: 100%; height: 150px; background: #181818; border-radius: 4px; border-left: 2px solid #444; border-bottom: 2px solid #444; }
    .status-badge { display: inline-block; padding: 5px 12px; border-radius: 15px; font-size: 0.8rem; font-weight: bold; margin-bottom: 15px; }
    .st-on { background: rgba(0,255,136,0.2); color: var(--green); border: 1px solid var(--green); }
    .st-off { background: rgba(255,77,77,0.2); color: var(--red); border: 1px solid var(--red); }

  </style>
</head>
<body>
<h1>Disconnect System Control & Telemetry System</h1>
  <h2>Musashi R&D- Material and Elemental Tech. Research Gr.</h2>
  

  <div class="grid">
    
    <div class="card" style="grid-column: 1 / -1;">
      <div id="statusBadge" class="status-badge st-off">SYSTEM STANDBY</div>
      <div style="display:flex;gap:10px">
        <button class="btn-main btn-eng" onclick="cmd('1')" style="flex:1">ENGAGE</button>
        <button class="btn-main btn-dis" onclick="cmd('0')" style="flex:1">DISENGAGE</button>
        <button class="btn-main" onclick="cmd('A')" style="flex:1;background:#ffd700;color:#000">AUTO</button>
      </div>
    </div>

    <div class="card">
      <h3>CURRENT ANGLE</h3>
      <div class="metric-value"><span id="valAngle">0</span><span class="unit">°</span></div>
    </div>
    
    <div class="card">
      <h3>LINEAR DISTANCE</h3>
      <div class="metric-value"><span id="valDist">0.0</span><span class="unit">cm</span></div>
    </div>

    <div class="card">
      <h3>POTENTIOMETER</h3>
      <div class="metric-value"><span id="valPot">0</span><span class="unit">/4095</span></div>
    </div>

    <div class="card">
      <h3>SYSTEM MEMORY</h3>
      <div style="font-size:0.8rem;text-align:left">
        <div style="margin:8px 0">STM32: <span id="valSTM" style="color:#00d2ff">-</span></div>
        <div style="margin:8px 0">ESP32: <span id="valESP" style="color:#00ff88">-</span></div>
        <div style="margin:8px 0">Points: <span id="valDP" style="color:#ffd700">0</span></div>
      </div>
    </div>
    
    <div class="card" style="grid-column: 1 / -1;">
      <h3>📊 DATA RECORDING</h3>
      <div style="display:flex;gap:10px;align-items:center;margin-bottom:10px">
        <button id="recBtn" class="btn-main" onclick="toggleRec()" style="flex:1;background:#00ff88;color:#000;font-size:1rem;padding:12px">▶ Start Save the data</button>
        <div id="recStatus" style="flex:1;text-align:center;font-size:0.9rem;color:#ff4d4d;font-weight:600">○ Not Saving</div>
      </div>
      <h3 style="margin-top:20px">📁 SAVED FILES</h3>
      <div id="fileList" style="max-height:200px;overflow-y:auto;text-align:left"></div>
      <button onclick="loadFiles()" style="width:100%;padding:8px;margin-top:10px;background:#333;color:#fff;border:none;border-radius:4px;cursor:pointer">🔄 Refresh Files</button>
    </div>

    <div class="card" style="grid-column: 1 / -1;">
      <h3>ENGAGEMENT STATE (Logic 0/1)</h3>
      <canvas id="chartState"></canvas>
    </div>

    <div class="card">
      <h3>ANGLE HISTORY (0-180°)</h3>
      <canvas id="chartAngle"></canvas>
    </div>

    <div class="card">
      <h3>DISTANCE TRAVEL (0-10cm)</h3>
      <canvas id="chartDist"></canvas>
    </div>

    <div class="card">
      <h3>CURRENT DRAW (0-5A)</h3>
      <canvas id="chartCurrent"></canvas>
    </div>

    <div class="card">
      <h3>TORQUE OUTPUT (0-50Nm)</h3>
      <canvas id="chartTorque"></canvas>
    </div>
  </div>

<script>
  // --- OFFLINE GRAPHING ENGINE ---
  const MAX_POINTS = 100;
  
  class Graph {
    constructor(canvasId, color, maxVal, label) {
      this.canvas = document.getElementById(canvasId);
      this.ctx = this.canvas.getContext('2d');
      this.data = new Array(MAX_POINTS).fill(0);
      this.color = color;
      this.maxVal = maxVal;
      this.label = label;
      this.canvas.width = this.canvas.offsetWidth;
      this.canvas.height = this.canvas.offsetHeight;
    }

    add(val) {
      this.data.push(val);
      this.data.shift();
      this.draw();
    }

    draw() {
      const w = this.canvas.width;
      const h = this.canvas.height;
      const ctx = this.ctx;
      
      ctx.clearRect(0, 0, w, h);
      
      // Title with current value
      ctx.fillStyle = this.color;
      ctx.font = 'bold 11px sans-serif';
      ctx.textAlign = 'left';
      ctx.fillText(this.label, 5, 12);
      ctx.textAlign = 'right';
      ctx.fillText(this.data[MAX_POINTS-1].toFixed(1), w - 5, 12);
      
      // Grid lines
      ctx.strokeStyle = '#2a2a2a';
      ctx.lineWidth = 1;
      for(let i=1; i<4; i++) {
        let y = (h * i) / 4;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
      }
      
      // Y-axis labels
      ctx.fillStyle = '#666';
      ctx.font = '9px monospace';
      ctx.textAlign = 'left';
      for(let i=0; i<=4; i++) {
        let val = this.maxVal * (1 - i/4);
        let y = (h * i) / 4 + 3;
        ctx.fillText(val.toFixed(0), 3, y);
      }

      // Plot Line
      ctx.strokeStyle = this.color;
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      
      const step = w / (MAX_POINTS - 1);
      
      for(let i=0; i<MAX_POINTS; i++) {
        let y = h - ((this.data[i] / this.maxVal) * h);
        if(i===0) ctx.moveTo(0, y);
        else ctx.lineTo(i * step, y);
      }
      ctx.stroke();
    }
  }

  // Init Graphs
  const gState = new Graph('chartState', '#00ff88', 1.2, 'Engaged'); 
  const gAngle = new Graph('chartAngle', '#00d2ff', 180, 'Angle (°)');
  const gDist  = new Graph('chartDist',  '#ffae00', 10, 'Distance (cm)');
  const gCurrent = new Graph('chartCurrent', '#ff00ff', 5, 'Current (A)');
  const gTorque = new Graph('chartTorque', '#00ffff', 50, 'Torque (Nm)');

  // --- LOGIC ---
  let currentState = 0;

  function updateDashboard() {
    fetch('/status').then(r => r.json()).then(d => {
      currentState = d.s;
      
      // 1. Update Numbers
      document.getElementById('valAngle').innerText = d.a;
      document.getElementById('valDist').innerText = d.d;
      document.getElementById('valPot').innerText = d.p;
      document.getElementById('valSTM').innerText = (d.sr/1024).toFixed(1) + 'KB';
      document.getElementById('valESP').innerText = (d.er/1024).toFixed(1) + 'KB';
      document.getElementById('valDP').innerText = d.dp;

      // 2. Update Graphs
      gState.add(d.s);
      gAngle.add(d.a);
      gDist.add(d.d);
      gCurrent.add(d.c || 0);
      gTorque.add(d.t || 0);

      // 3. Update Badge
      const badge = document.getElementById('statusBadge');
      if(d.m === 1) {
        badge.innerText = "🔄 AUTO MODE";
        badge.className = "status-badge" + (d.s ? " st-on" : " st-off");
      } else if(d.s === 1) {
        badge.innerText = "✓ SYSTEM ACTIVE";
        badge.className = "status-badge st-on";
      } else {
        badge.innerText = "○ STANDBY";
        badge.className = "status-badge st-off";
      }
      
      // 4. Update Recording Button State
      const btn = document.getElementById('recBtn');
      const status = document.getElementById('recStatus');
      if(d.rec) {
        btn.innerHTML = '⏹ Stop';
        btn.style.background = '#ff0066';
        btn.style.color = '#fff';
        status.innerHTML = '● Saving Data...';
        status.style.color = '#00ff88';
      } else {
        if(!d.sd) {
          btn.innerHTML = '❌ SD Card Error';
          btn.style.background = '#333';
          btn.style.color = '#ff4d4d';
          btn.disabled = true;
          status.innerHTML = '❌ ' + (d.err || 'SD Card Not Ready');
          status.style.color = '#ff4d4d';
        } else if(d.err && d.err !== '') {
          btn.innerHTML = '▶ Start Save the data';
          btn.style.background = '#00ff88';
          btn.style.color = '#000';
          btn.disabled = false;
          status.innerHTML = '⚠ ' + d.err;
          status.style.color = '#ffae00';
        } else {
          btn.innerHTML = '▶ Start Save the data';
          btn.style.background = '#00ff88';
          btn.style.color = '#000';
          btn.disabled = false;
          status.innerHTML = '○ Not Saving';
          status.style.color = '#ff4d4d';
        }
      }
    });
  }

  function cmd(c) {
    fetch('/cmd?v=' + c).then(() => setTimeout(updateDashboard, 100));
  }
  
  function toggleRec() {
    fetch('/record/toggle').then(r => r.json()).then(d => {
      const btn = document.getElementById('recBtn');
      const status = document.getElementById('recStatus');
      
      if(d.error && d.error !== '') {
        alert('Recording Error: ' + d.error);
        status.innerHTML = '❌ ' + d.error;
        status.style.color = '#ff4d4d';
        return;
      }
      
      if(d.recording) {
        btn.innerHTML = '⏹ Stop';
        btn.style.background = '#ff0066';
        btn.style.color = '#fff';
        status.innerHTML = '● Saving to: ' + (d.file || 'file');
        status.style.color = '#00ff88';
      } else {
        btn.innerHTML = '▶ Start Save the data';
        btn.style.background = '#00ff88';
        btn.style.color = '#000';
        status.innerHTML = d.file ? '✓ Saved: ' + d.file : '○ Not Saving';
        status.style.color = d.file ? '#00ff88' : '#ff4d4d';
        if(d.file) setTimeout(loadFiles, 500);
      }
    }).catch(e => {
      alert('Network error: ' + e);
    });
  }
  
  function loadFiles() {
    fetch('/files').then(r => r.json()).then(d => {
      const list = document.getElementById('fileList');
      
      if(d.error && d.error !== '') {
        list.innerHTML = '<div style=\"color:#ff4d4d;text-align:center;padding:20px;background:#1a1a1a;border-radius:4px\">❌ ' + d.error + '</div>';
        return;
      }
      
      if(d.files.length === 0) {
        list.innerHTML = '<div style=\"color:#666;text-align:center;padding:20px\">No saved files yet. Click START to create your first recording.</div>';
      } else {
        list.innerHTML = d.files.map(f => 
          `<div style=\"display:flex;justify-content:space-between;align-items:center;padding:8px;background:#1a1a1a;margin:4px 0;border-radius:4px;gap:10px\">
            <span style=\"flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap\">📄 ${f.name}</span>
            <span style=\"color:#888;font-size:0.75rem;min-width:60px;text-align:right\">${f.size}</span>
            <a href=\"/download/${f.name}\" download=\"${f.name}\" style=\"background:#0066ff;color:#fff;padding:6px 12px;border-radius:4px;text-decoration:none;font-size:0.8rem;white-space:nowrap\">⬇ Download</a>
          </div>`
        ).join('');
      }
    }).catch(e => {
      const list = document.getElementById('fileList');
      list.innerHTML = '<div style=\"color:#ff4d4d;text-align:center;padding:20px\">⚠ Network Error: ' + e.message + '</div>';
    });
  }
  
  function loadRecentData() {
    fetch('/recent').then(r => r.json()).then(d => {
      if(d.data && d.data.length > 0) {
        // Clear and fill graphs with server data
        gState.data = new Array(MAX_POINTS).fill(0);
        gAngle.data = new Array(MAX_POINTS).fill(0);
        gDist.data = new Array(MAX_POINTS).fill(0);
        
        const offset = MAX_POINTS - d.data.length;
        d.data.forEach((pt, i) => {
          gState.data[offset + i] = pt.e;
          gAngle.data[offset + i] = pt.a;
          gDist.data[offset + i] = pt.d;
        });
        
        gState.draw();
        gAngle.draw();
        gDist.draw();
      }
    }).catch(e => console.log('No recent data yet'));
  }

  // Loop
  setInterval(updateDashboard, 200); // 5Hz update rate
  loadFiles(); // Load files on start
  loadRecentData(); // Load graph history on start

  // Resize handler for canvas
  window.onresize = function() {
    [gState, gAngle, gDist, gCurrent, gTorque].forEach(g => {
        g.canvas.width = g.canvas.offsetWidth;
        g.canvas.height = g.canvas.offsetHeight;
    });
  };
</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // --- ESP32 CHIP INFO ---
  Serial.println("\n\n========================================");
  Serial.println("      FREENOVE ESP32 WROVER INFO");
  Serial.println("========================================");
  
  // Chip Model
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  Serial.printf("Chip Model: ESP32 (Rev %d)\n", chip_info.revision);
  Serial.printf("CPU Cores: %d\n", chip_info.cores);
  Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
  
  // Flash Memory
  Serial.printf("\n--- FLASH MEMORY ---\n");
  Serial.printf("Flash Size: %d MB (%s)\n", 
                ESP.getFlashChipSize() / (1024 * 1024),
                (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");
  Serial.printf("Flash Speed: %d MHz\n", ESP.getFlashChipSpeed() / 1000000);
  Serial.printf("Flash Mode: ");
  switch(ESP.getFlashChipMode()) {
    case FM_QIO:  Serial.println("QIO"); break;
    case FM_QOUT: Serial.println("QOUT"); break;
    case FM_DIO:  Serial.println("DIO"); break;
    case FM_DOUT: Serial.println("DOUT"); break;
    default:      Serial.println("UNKNOWN"); break;
  }
  
  // SRAM Memory
  Serial.printf("\n--- SRAM (HEAP) MEMORY ---\n");
  Serial.printf("Total Heap: %d KB\n", ESP.getHeapSize() / 1024);
  Serial.printf("Free Heap: %d KB\n", ESP.getFreeHeap() / 1024);
  Serial.printf("Min Free Heap: %d KB\n", ESP.getMinFreeHeap() / 1024);
  Serial.printf("Max Alloc Heap: %d KB\n", ESP.getMaxAllocHeap() / 1024);
  
  // PSRAM (if available on WROVER)
  if(psramFound()) {
    Serial.printf("\n--- PSRAM (External RAM) ---\n");
    Serial.printf("PSRAM Size: %d KB\n", ESP.getPsramSize() / 1024);
    Serial.printf("Free PSRAM: %d KB\n", ESP.getFreePsram() / 1024);
  } else {
    Serial.println("\n--- PSRAM: NOT DETECTED ---");
  }
  
  // Features
  Serial.printf("\n--- CHIP FEATURES ---\n");
  Serial.printf("WiFi: %s\n", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "YES (2.4GHz)" : "NO");
  Serial.printf("Bluetooth: %s\n", (chip_info.features & CHIP_FEATURE_BT) ? "YES" : "NO");
  Serial.printf("BLE: %s\n", (chip_info.features & CHIP_FEATURE_BLE) ? "YES" : "NO");
  
  Serial.println("========================================\n");
  
  // --- CONNECT TO STM32 ---
  // RX=26 (from STM32 TX PA9), TX=27 (to STM32 RX PA10)
  Serial2.begin(115200, SERIAL_8N1, 26, 27); 

  // --- SD CARD ---
  session_start = millis();
  Serial.println("Initializing SD Card...");
  
  // Try 1-bit mode first (more compatible with most boards)
  if(!SD_MMC.begin("/sdcard", true)) { 
    Serial.println("⚠ 1-bit mode failed, trying 4-bit mode..."); 
    // Try 4-bit mode as fallback
    if(!SD_MMC.begin("/sdcard", false)) {
      Serial.println("❌ SD Card Mount Failed - Check card insertion"); 
      sd_card_ready = false;
      last_error = "SD Card Failed";
    } else {
      Serial.println("✓ SD Card Ready (4-bit mode)"); 
      sd_card_ready = true;
      uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
      Serial.printf("SD Card Size: %lluMB\n", cardSize);
      Serial.printf("SD Card Type: ");
      uint8_t cardType = SD_MMC.cardType();
      if(cardType == CARD_MMC) Serial.println("MMC");
      else if(cardType == CARD_SD) Serial.println("SDSC");
      else if(cardType == CARD_SDHC) Serial.println("SDHC");
      else Serial.println("UNKNOWN");
    }
  } else { 
    Serial.println("✓ SD Card Ready (1-bit mode)"); 
    sd_card_ready = true;
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    Serial.printf("SD Card Type: ");
    uint8_t cardType = SD_MMC.cardType();
    if(cardType == CARD_MMC) Serial.println("MMC");
    else if(cardType == CARD_SD) Serial.println("SDSC");
    else if(cardType == CARD_SDHC) Serial.println("SDHC");
    else Serial.println("UNKNOWN");
  }

  // --- WIFI ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to iPhone");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("DASHBOARD IP: "); Serial.println(WiFi.localIP());

  // --- ROUTES ---
  server.on("/", []() { server.send(200, "text/html", index_html); });
  
  // New Toggle Route for Single Button
  server.on("/cmd", []() {
    if (server.hasArg("v")) {
      char cmd = server.arg("v").charAt(0);
      Serial2.print(cmd);
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/status", []() {
    char json[350];
    data.distance = ((float)data.angle / 180.0) * 10.0;
    // Placeholder values for current and torque (update when sensors available)
    data.current = 0.0;  // TODO: Read from current sensor
    data.torque = 0.0;   // TODO: Calculate from load cell or current
    
    snprintf(json, sizeof(json), 
             "{\"s\":%d,\"a\":%d,\"d\":%.1f,\"p\":%d,\"m\":%d,\"sr\":%u,\"er\":%u,\"dp\":%u,\"rec\":%s,\"sd\":%s,\"err\":\"%s\",\"c\":%.2f,\"t\":%.2f}",
             data.engaged, data.angle, data.distance, data.pot_raw, data.auto_mode,
             data.stm32_ram, ESP.getFreeHeap(), data_points, 
             is_recording ? "true" : "false",
             sd_card_ready ? "true" : "false",
             last_error.c_str(),
             data.current, data.torque);
    server.send(200, "application/json", json);
  });
  
  server.on("/recent", []() {
    String json;
    json.reserve(3000); // Pre-allocate memory
    json = "{\"data\":[";
    int count = buffer_count < 100 ? buffer_count : 100;
    for(int i = 0; i < count; i++) {
      int idx = (buffer_index - count + i + 100) % 100;
      if(i > 0) json += ",";
      char buf[50];
      snprintf(buf, sizeof(buf), "{\"e\":%d,\"a\":%d,\"d\":%.1f}",
               data_buffer[idx].engaged, data_buffer[idx].angle, data_buffer[idx].distance);
      json += buf;
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.on("/record/toggle", []() {
    if(!is_recording) {
      // START RECORDING
      if(!sd_card_ready) {
        last_error = "SD Card Not Ready";
        server.send(200, "application/json", "{\"recording\":false,\"error\":\"SD Card Not Ready\"}");
        return;
      }
      
      // Create timestamp-based filename
      time_t now = millis() / 1000;
      struct tm timeinfo;
      timeinfo.tm_year = 126; // 2026
      timeinfo.tm_mon = 0;    // January
      timeinfo.tm_mday = (now / 86400) % 31 + 1;
      timeinfo.tm_hour = (now / 3600) % 24;
      timeinfo.tm_min = (now / 60) % 60;
      timeinfo.tm_sec = now % 60;
      
      char filename[50];
      snprintf(filename, sizeof(filename), "/Datalog_%04d-%02d-%02d_%02d-%02d-%02d.csv",
               timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      current_log_file = String(filename);
      
      File f = SD_MMC.open(current_log_file.c_str(), FILE_WRITE);
      if(!f) {
        last_error = "Failed to create file";
        is_recording = false;
        current_log_file = "";
        server.send(200, "application/json", "{\"recording\":false,\"error\":\"Failed to create file\"}");
        return;
      }
      
      f.println("Time_ms,Session_s,Engaged,Angle,PotRaw,AutoMode,Distance_cm,STM32_RAM,ESP32_Free");
      f.close();
      
      data_points = 0;
      session_start = millis();
      is_recording = true;
      last_error = "";
      
      char json[150];
      snprintf(json, sizeof(json), "{\"recording\":true,\"file\":\"%s\",\"error\":\"\"}", 
               current_log_file.c_str());
      server.send(200, "application/json", json);
    } else {
      // STOP RECORDING
      is_recording = false;
      String stopped_file = current_log_file;
      current_log_file = "";
      last_error = "";
      
      char json[150];
      snprintf(json, sizeof(json), "{\"recording\":false,\"file\":\"%s\",\"error\":\"\"}",
               stopped_file.c_str());
      server.send(200, "application/json", json);
    }
  });
  
  server.on("/files", []() {
    if(!sd_card_ready) {
      server.send(200, "application/json", "{\"files\":[],\"error\":\"SD Card Not Ready\"}");
      return;
    }
    
    String json;
    json.reserve(2000);
    json = "{\"files\":[";
    File root = SD_MMC.open("/");
    bool first = true;
    if(root && root.isDirectory()) {
      File file = root.openNextFile();
      while(file) {
        if(!file.isDirectory()) {
          String fname = String(file.name());
          if(fname.startsWith("/")) fname = fname.substring(1);
          if(fname.endsWith(".csv")) {
            if(!first) json += ",";
            char buf[150];
            float sizeKB = file.size() / 1024.0;
            snprintf(buf, sizeof(buf), "{\"name\":\"%s\",\"size\":\"%.2fKB\"}",
                     fname.c_str(), sizeKB);
            json += buf;
            first = false;
          }
        }
        file.close();
        file = root.openNextFile();
      }
      root.close();
    }
    json += "],\"error\": \"\"}";
    server.send(200, "application/json", json);
  });
  
  server.onNotFound([]() {
    String path = server.uri();
    if(path.startsWith("/download/")) {
      if(!sd_card_ready) {
        server.send(503, "text/plain", "SD Card Not Available");
        return;
      }
      
      String filename = "/" + path.substring(10);
      File file = SD_MMC.open(filename.c_str());
      if(file) {
        String fname = filename;
        if(fname.startsWith("/")) fname = fname.substring(1);
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
        server.streamFile(file, "text/csv");
        file.close();
      } else {
        server.send(404, "text/plain", "File not found on SD card");
      }
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });

  server.begin();
}

void loop() {
  server.handleClient();

  // Enhanced STM32 Data Reception with 100ms logging rate
  static String buffer = "";
  static uint32_t last_log_time = 0;
  
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      // Parse: engaged,angle,pot_raw,auto_mode,ram
      int idx[4];
      idx[0] = buffer.indexOf(',');
      idx[1] = buffer.indexOf(',', idx[0] + 1);
      idx[2] = buffer.indexOf(',', idx[1] + 1);
      idx[3] = buffer.indexOf(',', idx[2] + 1);
      
      if (idx[3] > 0) {
        data.engaged = buffer.substring(0, idx[0]).toInt();
        data.angle = buffer.substring(idx[0] + 1, idx[1]).toInt();
        data.pot_raw = buffer.substring(idx[1] + 1, idx[2]).toInt();
        data.auto_mode = buffer.substring(idx[2] + 1, idx[3]).toInt();
        data.stm32_ram = buffer.substring(idx[3] + 1).toInt();
        
        // Store in rolling buffer for graph persistence
        data_buffer[buffer_index].engaged = data.engaged;
        data_buffer[buffer_index].angle = data.angle;
        data_buffer[buffer_index].distance = ((float)data.angle / 180.0) * 10.0;
        buffer_index = (buffer_index + 1) % 100;
        if(buffer_count < 100) buffer_count++;
        
        // Log to SD if recording and 100ms elapsed
        if(is_recording && sd_card_ready && current_log_file != "" && (millis() - last_log_time >= 100)) {
          File f = SD_MMC.open(current_log_file.c_str(), FILE_APPEND);
          if(f) {
            float session_time = (millis() - session_start) / 1000.0;
            float d = ((float)data.angle / 180.0) * 10.0;
            f.print(millis()); f.print(",");
            f.print(session_time, 2); f.print(",");
            f.print(data.engaged); f.print(",");
            f.print(data.angle); f.print(",");
            f.print(data.pot_raw); f.print(",");
            f.print(data.auto_mode); f.print(",");
            f.print(d, 2); f.print(",");
            f.print(data.stm32_ram); f.print(",");
            f.println(ESP.getFreeHeap());
            f.close();
            data_points++;
            last_log_time = millis();
          } else {
            // File write failed, stop recording
            is_recording = false;
            last_error = "Write failed";
            Serial.println("⚠ SD Write Failed - Stopping recording");
          }
        }
      }
      buffer = "";
    } else if (c != '\r' && buffer.length() < 128) {
      buffer += c;
    }
  }
  
  // Memory monitoring
  static uint32_t last_check = 0;
  if (millis() - last_check > 10000) {
    Serial.printf("💾 ESP32: %dKB free (min:%dKB) | STM32: %dB | Data: %d\n",
                  ESP.getFreeHeap()/1024, ESP.getMinFreeHeap()/1024,
                  data.stm32_ram, data_points);
    last_check = millis();
  }
}