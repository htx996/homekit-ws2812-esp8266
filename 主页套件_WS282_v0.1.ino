/*
  HomeKit WS2812 Controller (ESP8266 / ESP-12F)
  - Web 配网（全中文科技风 UI）
  - 扫描附近 Wi-Fi（异步扫描，不阻塞页面）
  - Web 可配置：LED 数量、LED GPIO（输入数字）
  - Web 按钮：重置Wi-Fi / 重启 / 重置HomeKit / 开始AP（全部生效）
  - 开灯渐亮、关灯渐灭（平滑）
  - HomeKit 配对码：11122333（显示 111-22-333） -> my_accessory.c
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>

#include <arduino_homekit_server.h>
#include "my_accessory.h"

// ======================= 可调整默认值 =======================
static const char*    kDeviceName           = "HomeKit RGB";
static const uint16_t kDefaultLedCount      = 30;
static const uint8_t  kDefaultLedGpio       = 2;     // 常用 GPIO2
static const uint16_t kFadeMsOn             = 1200;  // 渐亮总时长
static const uint16_t kFadeMsOff            = 900;   // 渐灭总时长
static const uint32_t kStaConnectTimeoutMs  = 12000;
// ============================================================

static const uint8_t DNS_PORT = 53;
static DNSServer dnsServer;
static ESP8266WebServer server(80);

static bool   g_apEnabled = false;        // 是否开启了 AP（可叠加在 STA 上）
static bool   g_homekitRunning = false;   // HomeKit server 是否已启动
static String g_apSsid;

static Adafruit_NeoPixel* g_pixels = nullptr;

// 亮度渐变
static float    g_currentBrightness = 0.0f; // 0..100 实际输出亮度（渐变用）
static uint32_t g_lastFadeMs = 0;

// WS2812 输出缓存（减少无意义 show）
static uint8_t  g_lastR = 0, g_lastG = 0, g_lastB = 0;
static uint16_t g_lastCount = 0;
static bool     g_lastOn = false;

// ---------- 配置存储 ----------
struct AppConfig {
  uint32_t magic;
  char ssid[33];
  char pass[65];
  uint16_t ledCount;
  uint8_t  ledGpio;
};
static const uint32_t CFG_MAGIC = 0xC0FFEE66;
static const char* CFG_PATH = "/cfg.bin";
static AppConfig g_cfg;

// ---------- WiFi 扫描缓存（异步） ----------
static bool     g_scanInProgress = false;
static uint32_t g_scanStartMs = 0;

// ======================= Web UI（科技风全中文） =======================
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>HomeKit 灯带控制器 - 配置</title>
<style>
  :root{--bg:#070A12;--panel:#0B1022;--line:#1BE7FF;--line2:#7C3AED;--txt:#E6F0FF;--mut:#9FB3C8;--ok:#22C55E;--bad:#EF4444;}
  *{box-sizing:border-box;font-family:ui-sans-serif,system-ui,-apple-system,"PingFang SC","Microsoft YaHei",sans-serif}
  html,body{height:100%}
  body{
    margin:0; min-height:100vh;
    background:
      radial-gradient(1200px 800px at 20% 10%, rgba(124,58,237,.25), transparent 55%),
      radial-gradient(1000px 700px at 90% 30%, rgba(27,231,255,.18), transparent 60%),
      var(--bg);
    background-repeat:no-repeat;
    background-attachment:fixed;
    background-size:cover;
    color:var(--txt);
  }
  .wrap{
    max-width:920px;margin:0 auto;padding:20px;
    padding-left: calc(20px + env(safe-area-inset-left));
    padding-right: calc(20px + env(safe-area-inset-right));
    padding-bottom: calc(20px + env(safe-area-inset-bottom));
  }
  .title{display:flex;align-items:center;gap:12px;margin:10px 0 18px}
  .logo{width:12px;height:12px;border-radius:50%;background:var(--line);box-shadow:0 0 18px var(--line)}
  h1{font-size:20px;margin:0}
  .grid{display:grid;grid-template-columns:1.2fr .8fr;gap:14px}
  @media(max-width:860px){.grid{grid-template-columns:1fr}}
  .card{background:linear-gradient(180deg, rgba(255,255,255,.04), rgba(255,255,255,.02));
        border:1px solid rgba(27,231,255,.18);
        border-radius:16px; padding:16px; position:relative; overflow:hidden}
  .card:before{content:""; position:absolute; inset:-2px; border-radius:18px;
     background:conic-gradient(from 180deg, transparent 0 70%, rgba(27,231,255,.25), rgba(124,58,237,.25), transparent);
     filter:blur(18px); opacity:.55; pointer-events:none}
  .card > *{position:relative}
  .sec{font-size:12px;color:var(--mut);letter-spacing:.14em;text-transform:uppercase}
  label{display:block;margin-top:12px;font-size:13px;color:var(--mut)}
  select,input{width:100%;padding:10px 12px;margin-top:6px;border-radius:12px;
               border:1px solid rgba(27,231,255,.25);
               background:rgba(11,16,34,.72);color:var(--txt);outline:none}
  input::placeholder{color:#6B7C93}
  .row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  @media(max-width:520px){.row{grid-template-columns:1fr}}
  .btns{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px}
  button{border:1px solid rgba(27,231,255,.35);background:rgba(11,16,34,.72);
         color:var(--txt); padding:10px 12px;border-radius:12px; cursor:pointer}
  button:hover{border-color:rgba(27,231,255,.75); box-shadow:0 0 0 3px rgba(27,231,255,.08)}
  .primary{border-color:rgba(34,197,94,.6)}
  .danger{border-color:rgba(239,68,68,.6)}
  .hint{margin-top:10px;font-size:12px;color:var(--mut);line-height:1.6}
  .status{margin-top:12px;font-size:13px;white-space:pre-wrap}
  .dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:8px;background:rgba(255,255,255,.2)}
  .ok{background:var(--ok)} .bad{background:var(--bad)}
  .mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace}

  @media(max-width:520px){
    .wrap{padding:14px}
    .card{padding:14px}
    button{width:100%}
    select,input{min-height:42px}
  }
</style></head>
<body><div class="wrap">
  <div class="title"><div class="logo"></div><h1>HomeKit 灯带控制器 · 配置中心</h1></div>

  <div class="grid">
    <div class="card">
      <div class="sec">Wi-Fi 配置</div>

      <label>扫描并选择附近 Wi-Fi</label>
      <div class="row">
        <select id="ssidList"></select>
        <button onclick="scanWifi()">扫描附近 Wi-Fi</button>
      </div>

      <label>Wi-Fi 名称（SSID）</label>
      <input id="ssid" placeholder="可从上方下拉选择，也可手动输入"/>

      <label>Wi-Fi 密码</label>
      <input id="pass" type="password" placeholder="留空=保留旧密码（仅当 SSID 不变）"/>

      <div class="sec" style="margin-top:16px">灯带参数</div>
      <div class="row">
        <div>
          <label>LED 数量（1~500）</label>
          <input id="ledCount" type="number" min="1" max="500"/>
        </div>
        <div>
          <label>LED 数据引脚 GPIO（0~16）</label>
          <input id="ledGpio" type="number" min="0" max="16"/>
        </div>
      </div>

      <div class="btns">
        <button class="primary" onclick="save()">保存并应用</button>
      </div>

      <div class="hint">
        HomeKit 配对码固定：<span class="mono">11122333</span>（Home App 中显示为 <span class="mono">111-22-333</span>）
      </div>
      <div class="status" id="status"><span class="dot"></span>等待操作…</div>
    </div>

    <div class="card">
      <div class="sec">设备控制</div>
      <div class="btns">
        <button onclick="action('start_ap')">开始 AP（配网模式）</button>
        <button class="danger" onclick="action('reset_wifi')">重置 Wi-Fi</button>
        <button class="danger" onclick="action('reset_homekit')">重置 HomeKit</button>
        <button onclick="action('reboot')">重启设备</button>
      </div>

      <div class="hint">
        <b>说明：</b><br/>
        (1) “重置 Wi-Fi” 会清空已保存的 Wi-Fi，并开启 AP 配网。<br/>
        (2) “重置 HomeKit” 会清空配对信息（需要重新在 Home App 添加）。<br/>
        (3) “开始 AP” 适用于已联网但想重新配网/改参数；不会影响当前联网（AP+STA）。<br/>
      </div>
    </div>
  </div>

<script>
async function jget(u){const r=await fetch(u,{cache:'no-store'}); return await r.json();}
function setStatus(ok, msg){
  const el=document.getElementById('status');
  el.innerHTML = `<span class="dot ${ok?'ok':'bad'}"></span>${msg}`;
}

async function loadCfg(){
  try{
    const c=await jget('/api/config');
    document.getElementById('ssid').value=c.ssid||'';
    document.getElementById('pass').value='';
    document.getElementById('ledCount').value=c.ledCount||30;
    document.getElementById('ledGpio').value=c.ledGpio||2;

    let msg = "已加载当前配置";
    if(c.apMode){
      msg += `\n📶 当前处于 AP 配网模式：请连接【${c.apSsid}】后访问 192.168.4.1`;
    }
    if(c.gpioNote){
      msg += "\n⚠️ 当前 GPIO 提示："+c.gpioNote;
    }
    setStatus(true, msg);
  }catch(e){
    setStatus(false,'无法加载配置：'+e);
  }
}

// 异步扫描：发起后轮询直到 done
async function scanWifi(){
  setStatus(true,'正在扫描附近 Wi-Fi…（预计 2~6 秒）');
  try{
    await jget('/api/scan?start=1');
    for(let i=0;i<20;i++){
      await new Promise(r=>setTimeout(r,350));
      const r=await jget('/api/scan');
      if(r.status==='done'){
        const list=document.getElementById('ssidList');
        list.innerHTML='';
        r.networks.forEach(n=>{
          const o=document.createElement('option');
          o.value=n.ssid;
          o.textContent=`${n.ssid}  (信号 ${n.rssi}dBm${n.sec?' · 加密':' · 开放'})`;
          list.appendChild(o);
        });
        list.onchange=()=>{document.getElementById('ssid').value=list.value;};
        if(r.networks.length>0){
          document.getElementById('ssid').value=r.networks[0].ssid;
          setStatus(true,`扫描完成：发现 ${r.networks.length} 个网络`);
        }else{
          setStatus(false,'未发现 Wi-Fi（或信号太弱）');
        }
        return;
      }else if(r.status==='scanning'){
        // continue
      }else{
        setStatus(false,'扫描失败：'+(r.error||'未知错误'));
        return;
      }
    }
    setStatus(false,'扫描超时：请重试');
  }catch(e){
    setStatus(false,'扫描失败：'+e);
  }
}

async function save(){
  const ssid=document.getElementById('ssid').value.trim();
  const pass=document.getElementById('pass').value;
  const ledCount=document.getElementById('ledCount').value;
  const ledGpio=document.getElementById('ledGpio').value;
  if(!ssid){ setStatus(false,'请填写 Wi-Fi 名称（SSID）'); return; }
  setStatus(true,'正在保存并应用…');
  const fd=new URLSearchParams();
  fd.set('ssid',ssid); fd.set('pass',pass);
  fd.set('ledCount',ledCount); fd.set('ledGpio',ledGpio);
  const r=await fetch('/api/save',{method:'POST',body:fd});
  const t=await r.text();
  setStatus(r.ok, t);
}

async function action(a){
  setStatus(true,'正在执行：'+a+' …');
  const fd=new URLSearchParams(); fd.set('action',a);
  const r=await fetch('/api/action',{method:'POST',body:fd});
  const t=await r.text();
  setStatus(r.ok, t);
}

loadCfg();
</script>
</div></body></html>
)HTML";

// ======================= GPIO 校验（按你的规则） =======================
static bool isGpioRejected(uint8_t gpio) {
  if (gpio == 1 || gpio == 3) return true;      // 串口 TX/RX
  if (gpio >= 6 && gpio <= 11) return true;     // SPI Flash
  return false;
}
static bool isGpioWarn(uint8_t gpio) {
  return (gpio == 0 || gpio == 2 || gpio == 15 || gpio == 16);
}
static String gpioWarnText(uint8_t gpio) {
  switch (gpio) {
    case 0:  return "GPIO0 属于启动/下载相关脚，外设可能影响上电启动或进入下载模式。";
    case 2:  return "GPIO2 属于启动相关脚，外部电路拉低可能导致无法启动；部分板子还连着板载LED。";
    case 15: return "GPIO15 属于启动相关脚（需保持低电平启动），外设接入可能导致无法启动。";
    case 16: return "GPIO16 特性与普通GPIO不同（深睡相关、功能限制），驱动 WS2812B 存在兼容性风险。";
    default: return "";
  }
}
static String jsonEscape(const String& s) {
  String o; o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\') o += "\\\\";
    else if (c == '"') o += "\\\"";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') {}
    else o += c;
  }
  return o;
}

// ======================= 工具函数 =======================
static String chipSuffix() {
  uint32_t id = ESP.getChipId();
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", (unsigned)(id & 0xFFFF));
  return String(buf);
}

static void setDefaultConfig() {
  memset(&g_cfg, 0, sizeof(g_cfg));
  g_cfg.magic = CFG_MAGIC;
  g_cfg.ledCount = kDefaultLedCount;
  g_cfg.ledGpio  = kDefaultLedGpio;
}

static bool loadConfig() {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists(CFG_PATH)) {
    setDefaultConfig();
    return false;
  }
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) { setDefaultConfig(); return false; }

  if (f.read((uint8_t*)&g_cfg, sizeof(g_cfg)) != sizeof(g_cfg)) {
    f.close();
    setDefaultConfig();
    return false;
  }
  f.close();

  if (g_cfg.magic != CFG_MAGIC) {
    setDefaultConfig();
    return false;
  }

  if (g_cfg.ledCount < 1) g_cfg.ledCount = 1;
  if (g_cfg.ledCount > 500) g_cfg.ledCount = 500;

  // 读取阶段兜底：非法 GPIO 回默认
  if (g_cfg.ledGpio > 16 || isGpioRejected(g_cfg.ledGpio)) g_cfg.ledGpio = kDefaultLedGpio;

  return true;
}

static bool saveConfig() {
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)&g_cfg, sizeof(g_cfg));
  f.close();
  return w == sizeof(g_cfg);
}

static void clearWifiInConfig() {
  g_cfg.ssid[0] = 0;
  g_cfg.pass[0] = 0;
  saveConfig();
}

static void delayedRestart(uint32_t ms = 600) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) { delay(10); yield(); }
  ESP.restart();
}

// ======================= LED：HSV -> RGB =======================
static void hsvToRgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
  float hh = fmodf(h, 360.0f);
  float ss = constrain(s, 0.0f, 100.0f) / 100.0f;
  float vv = constrain(v, 0.0f, 100.0f) / 100.0f;

  float c = vv * ss;
  float x = c * (1 - fabsf(fmodf(hh / 60.0f, 2) - 1));
  float m = vv - c;

  float rr=0, gg=0, bb=0;
  if      (hh < 60)  { rr=c; gg=x; bb=0; }
  else if (hh < 120) { rr=x; gg=c; bb=0; }
  else if (hh < 180) { rr=0; gg=c; bb=x; }
  else if (hh < 240) { rr=0; gg=x; bb=c; }
  else if (hh < 300) { rr=x; gg=0; bb=c; }
  else               { rr=c; gg=0; bb=x; }

  r = (uint8_t)((rr + m) * 255);
  g = (uint8_t)((gg + m) * 255);
  b = (uint8_t)((bb + m) * 255);
}

static void pixelsRecreate() {
  if (g_pixels) {
    delete g_pixels;
    g_pixels = nullptr;
  }
  g_pixels = new Adafruit_NeoPixel(g_cfg.ledCount, g_cfg.ledGpio, NEO_GRB + NEO_KHZ800);
  g_pixels->begin();
  g_pixels->show();

  // 重置缓存，确保下一次一定刷新
  g_lastR = g_lastG = g_lastB = 255;
  g_lastCount = 0;
  g_lastOn = !g_lastOn;
}

static void applyLedOutput() {
  if (!g_pixels) return;

  bool   on  = hk_targetOn;
  float  hue = hk_targetHue;
  float  sat = hk_targetSat;

  float effective = constrain(g_currentBrightness, 0.0f, 100.0f);
  uint8_t r0, g0, b0;
  hsvToRgb(hue, sat, 100.0f, r0, g0, b0);

  float scale = effective / 100.0f;
  uint8_t r = (uint8_t)(r0 * scale);
  uint8_t g = (uint8_t)(g0 * scale);
  uint8_t b = (uint8_t)(b0 * scale);

  if (r == g_lastR && g == g_lastG && b == g_lastB &&
      g_cfg.ledCount == g_lastCount && on == g_lastOn) {
    return;
  }
  g_lastR = r; g_lastG = g; g_lastB = b;
  g_lastCount = g_cfg.ledCount;
  g_lastOn = on;

  uint32_t c = g_pixels->Color(r, g, b);
  for (uint16_t i = 0; i < g_cfg.ledCount; i++) {
    g_pixels->setPixelColor(i, c);
  }
  g_pixels->show();
}

static void updateFade() {
  uint32_t now = millis();
  if (now - g_lastFadeMs < 15) return;
  g_lastFadeMs = now;

  float target = hk_targetOn ? hk_targetBrightness : 0.0f;
  target = constrain(target, 0.0f, 100.0f);

  if (fabsf(g_currentBrightness - target) < 0.2f) {
    g_currentBrightness = target;
    applyLedOutput();
    return;
  }

  uint16_t totalMs = hk_targetOn ? kFadeMsOn : kFadeMsOff;
  float step = 100.0f * (15.0f / (float)totalMs);

  if (g_currentBrightness < target) g_currentBrightness = min(target, g_currentBrightness + step);
  else                             g_currentBrightness = max(target, g_currentBrightness - step);

  applyLedOutput();
}

// ======================= AP / STA =======================
static void enableAPOverlay() {
  WiFiMode_t m = WiFi.getMode();
  if (m != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    delay(10);
  }

  // 让 AP 名字稳定：只在第一次生成
  if (g_apSsid.length() == 0) {
    g_apSsid = String("HomeKit-RGB-") + chipSuffix();
  }

  WiFi.softAP(g_apSsid.c_str());  // 无密码
  delay(30);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  g_apEnabled = true;
}

static bool connectSTA(uint32_t timeoutMs = kStaConnectTimeoutMs) {
  if (strlen(g_cfg.ssid) == 0) return false;

  if (g_apEnabled) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_STA);

  // 关键：切换目标 Wi-Fi 前不要把配置清空
  WiFi.begin(g_cfg.ssid, g_cfg.pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(150);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}

static void startHomeKitIfNeeded() {
  if (g_homekitRunning) return;
  if (WiFi.status() != WL_CONNECTED) return;

  // ✅ 如果你的 my_accessory.c/.h 提供 hk_init()（用于绑定 setter），这里必须先调用
  // 如果你没有 hk_init()，把下一行删掉即可。

  arduino_homekit_setup(&hk_config);
  g_homekitRunning = true;
}

// ======================= Web handlers =======================
static void handleRoot() {
  server.send(200, "text/html; charset=utf-8", FPSTR(INDEX_HTML));
}

static void handleConfig() {
  String note = "";
  if (isGpioWarn(g_cfg.ledGpio)) note = gpioWarnText(g_cfg.ledGpio);

  String json = "{";
  json += "\"ssid\":\"" + jsonEscape(String(g_cfg.ssid)) + "\",";
  json += "\"ledCount\":" + String(g_cfg.ledCount) + ",";
  json += "\"ledGpio\":" + String(g_cfg.ledGpio) + ",";
  json += "\"gpioNote\":\"" + jsonEscape(note) + "\",";
  json += "\"apMode\":" + String(g_apEnabled ? "true" : "false") + ",";
  json += "\"apSsid\":\"" + jsonEscape(g_apEnabled ? g_apSsid : String("")) + "\"";
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

static void handleScan() {
  bool start = server.hasArg("start") && server.arg("start") == "1";

  if (start) {
    if (!g_scanInProgress) {
      WiFiMode_t m = WiFi.getMode();
      if (m == WIFI_OFF) WiFi.mode(WIFI_STA);

      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
      g_scanInProgress = true;
      g_scanStartMs = millis();
    }
    server.send(200, "application/json; charset=utf-8", "{\"status\":\"scanning\"}");
    return;
  }

  if (!g_scanInProgress) {
    server.send(200, "application/json; charset=utf-8", "{\"status\":\"idle\",\"networks\":[]}");
    return;
  }

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - g_scanStartMs > 15000) {
      WiFi.scanDelete();
      g_scanInProgress = false;
      server.send(200, "application/json; charset=utf-8", "{\"status\":\"error\",\"error\":\"扫描超时\"}");
      return;
    }
    server.send(200, "application/json; charset=utf-8", "{\"status\":\"scanning\"}");
    return;
  }

  if (n < 0) {
    WiFi.scanDelete();
    g_scanInProgress = false;
    server.send(200, "application/json; charset=utf-8", "{\"status\":\"error\",\"error\":\"扫描失败\"}");
    return;
  }

  String json = "{\"status\":\"done\",\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    bool sec = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(rssi) + ",\"sec\":" + String(sec ? "true" : "false") + "}";
  }
  json += "]}";

  WiFi.scanDelete();
  g_scanInProgress = false;

  server.send(200, "application/json; charset=utf-8", json);
}

static void handleSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain; charset=utf-8", "缺少 ssid");
    return;
  }

  String ssid = server.arg("ssid"); ssid.trim();
  String pass = server.arg("pass"); // 可能为空

  int ledCountInt = server.arg("ledCount").toInt();
  int ledGpioInt  = server.arg("ledGpio").toInt();

  if (ssid.length() == 0) {
    server.send(400, "text/plain; charset=utf-8", "SSID 不能为空");
    return;
  }

  // ---------- ✅ 核心修复：密码留空但 SSID 不变 -> 保留旧密码 ----------
  String oldSsid = String(g_cfg.ssid);
  String oldPass = String(g_cfg.pass);

  if (pass.length() == 0 && ssid == oldSsid && oldPass.length() > 0) {
    pass = oldPass; // 保留
  }
  // -------------------------------------------------------------

  // LED 数量
  uint16_t ledCount = (uint16_t)ledCountInt;
  if (ledCount < 1) ledCount = 1;
  if (ledCount > 500) ledCount = 500;

  // GPIO 范围
  if (ledGpioInt < 0 || ledGpioInt > 16) {
    server.send(400, "text/plain; charset=utf-8", "GPIO 只能输入 0~16 之间的数字");
    return;
  }
  uint8_t ledGpio = (uint8_t)ledGpioInt;

  // 拒绝规则：6~11、1、3
  if (isGpioRejected(ledGpio)) {
    String msg = "该 GPIO 不允许使用：GPIO";
    msg += String(ledGpio);
    msg += "\n原因：";
    if (ledGpio == 1) msg += "GPIO1 为串口 TX，会影响下载/日志，上电还会输出脉冲导致灯带乱闪。";
    else if (ledGpio == 3) msg += "GPIO3 为串口 RX，会影响下载/串口通信。";
    else msg += "GPIO6~11 被 ESP-12F 的 SPI Flash 占用，使用会导致无法运行。";
    server.send(400, "text/plain; charset=utf-8", msg);
    return;
  }

  // 允许但警告：0/2/15/16
  String warn = "";
  if (isGpioWarn(ledGpio)) warn = gpioWarnText(ledGpio);

  bool ledChanged  = (g_cfg.ledCount != ledCount) || (g_cfg.ledGpio != ledGpio);
  bool wifiChanged = (oldSsid != ssid) || (oldPass != pass);

  // 写回配置
  g_cfg.magic = CFG_MAGIC;
  memset(g_cfg.ssid, 0, sizeof(g_cfg.ssid));
  memset(g_cfg.pass, 0, sizeof(g_cfg.pass));
  strncpy(g_cfg.ssid, ssid.c_str(), sizeof(g_cfg.ssid) - 1);
  strncpy(g_cfg.pass, pass.c_str(), sizeof(g_cfg.pass) - 1);
  g_cfg.ledCount = ledCount;
  g_cfg.ledGpio  = ledGpio;

  if (!saveConfig()) {
    server.send(500, "text/plain; charset=utf-8", "保存失败（LittleFS 写入错误）");
    return;
  }

  // LED 参数立即生效
  if (ledChanged) {
    pixelsRecreate();
    if (!hk_targetOn) g_currentBrightness = 0.0f;
    applyLedOutput();
  }

  // WiFi 参数变更：尝试立即连接；失败则自动开 AP 叠加救援
  String connMsg = "";
  if (wifiChanged) {
    connMsg = "\n📶 正在尝试连接 Wi-Fi…";
    bool ok = connectSTA(kStaConnectTimeoutMs);
    if (ok) {
      startHomeKitIfNeeded();
      connMsg += "\n✅ 已连接 Wi-Fi：";
      connMsg += WiFi.localIP().toString();
      connMsg += "\nHomeKit 已可用。";
    } else {
      enableAPOverlay();
      connMsg += "\n❌ 连接失败：请检查密码/信号。";
      connMsg += "\n📶 已自动开启 AP：请连接【";
      connMsg += g_apSsid;
      connMsg += "】后访问 192.168.4.1 重新配网。";
    }
  } else {
    connMsg = "\n✅ Wi-Fi 参数未变更（仅应用灯带参数），不会影响已配网状态。";
  }

  String okMsg = "保存成功，配置已应用。";
  okMsg += "\n提示：Wi-Fi 密码留空且 SSID 不变时，会自动保留之前保存的密码。";

  if (ssid != oldSsid && server.arg("pass").length() == 0) {
    okMsg += "\n⚠️ 你更换了 SSID 但未填写密码：仅适用于开放 Wi-Fi；若是加密网络将连接失败并自动进入 AP。";
  }

  if (warn.length()) {
    okMsg += "\n⚠️ 警告：你选择的 GPIO";
    okMsg += String(ledGpio);
    okMsg += "：";
    okMsg += warn;
  }
  okMsg += connMsg;

  server.send(200, "text/plain; charset=utf-8", okMsg);
}

static void handleAction() {
  String a = server.arg("action");
  a.trim();

  if (a == "reboot") {
    server.send(200, "text/plain; charset=utf-8", "已收到：重启设备…");
    delayedRestart(300);
    return;
  }

  if (a == "reset_wifi") {
    clearWifiInConfig();
    enableAPOverlay();
    server.send(200, "text/plain; charset=utf-8",
                ("已重置 Wi-Fi：请连接 AP【" + g_apSsid + "】后访问 192.168.4.1 重新配网").c_str());
    return;
  }

  if (a == "reset_homekit") {
    homekit_storage_reset();
    server.send(200, "text/plain; charset=utf-8", "已重置 HomeKit：配对信息已清空（设备将重启）…");
    delayedRestart(300);
    return;
  }

  if (a == "start_ap") {
    enableAPOverlay();
    server.send(200, "text/plain; charset=utf-8",
                ("已开启 AP 配网模式：请连接 AP【" + g_apSsid + "】后访问 192.168.4.1").c_str());
    return;
  }

  server.send(400, "text/plain; charset=utf-8", "未知 action");
}

static void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleConfig);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/action", HTTP_POST, handleAction);

  // captive portal 探测
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/connecttest.txt", HTTP_GET, handleRoot);
  server.onNotFound(handleRoot);

  server.begin();
}

// ======================= setup / loop =======================
void setup() {
  Serial.begin(115200);
  delay(50);

  if (!LittleFS.begin()) {
    setDefaultConfig();
  } else {
    loadConfig();
  }

  pixelsRecreate();
  g_currentBrightness = 0.0f;
  applyLedOutput();

  setupWebServer();

  bool ok = connectSTA(kStaConnectTimeoutMs);
  if (!ok) {
    enableAPOverlay();
    Serial.println();
    Serial.println("== AP 配网模式 ==");
    Serial.println("AP SSID: " + g_apSsid);
    Serial.println("AP IP  : " + WiFi.softAPIP().toString());
  } else {
    Serial.println();
    Serial.println("== WiFi 已连接 ==");
    Serial.println(WiFi.localIP());

    startHomeKitIfNeeded();
  }
}

void loop() {
  server.handleClient();
  if (g_apEnabled) dnsServer.processNextRequest();

  if (g_homekitRunning && WiFi.status() == WL_CONNECTED) {
    arduino_homekit_loop();
  }

  updateFade();
  yield();
}
