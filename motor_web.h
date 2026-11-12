#ifndef MOTOR_WEB_H
#define MOTOR_WEB_H

#include <WiFi.h>
#include "esp_wifi.h"
#include <WebServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "index_html.h"
#include "motor.h"

// ---- WiFi access point credentials (change as needed) ----
static const char *WIFI_SSID = "MotorControl";
static const char *WIFI_PASS = "12345678";

// ---- Web server (port 80) ----
// HTTP/1.1 keep-alive web server.
//
// The stock WebServer always answers with "Connection: close" and releases the
// TCP connection right after each response, so every one of the page's /status
// polls (every 100 ms) pays a fresh connect + teardown. This subclass answers
// "Connection: keep-alive" instead and keeps the connection open, which removes
// that per-request latency.
//
// How it hooks the library without duplicating its connection state machine:
//   * _currentClientWrite() is the single choke point every response passes
//     through, and a response's whole header block arrives there as one
//     contiguous buffer starting with the status line — so the token can be
//     swapped in a private copy (content writes never match).
//   * The base handleClient() releases _currentClient at the end of the very
//     call that sent the response. Holding our own NetworkClient copy across
//     that call keeps the socket's shared handle alive, so "releasing" is a
//     no-op and the same connection can be handed back and re-armed in
//     HC_WAIT_READ for the next pipelined request.
//   * The idle hold is deliberately short (KEEP_ALIVE_IDLE_MS) and always
//     shorter than a browser's own keep-alive timeout, so the server closes
//     first and the client never writes into a half-closed socket.
// A client that asks for "Connection: close" gets exactly that, and anything
// that cannot be rewritten is sent untouched (still closing) — always safe.
//
// Trade-off: like the stock server this handles one connection at a time, so a
// held connection blocks another client for up to KEEP_ALIVE_IDLE_MS after the
// last request. The page polls every 100 ms, so the timeout never fires while
// the page is open; it only reclaims the slot once the browser goes away.
#define KEEP_ALIVE_IDLE_MS 1500

class KeepAliveWebServer : public WebServer {
public:
  explicit KeepAliveWebServer(int port) : WebServer(port) {}

  void handleClient() override {
    // Keep a reference to the socket the response was written to: the base
    // class drops _currentClient at the end of this same call.
    _held = NetworkClient();
    WebServer::handleClient();
    const bool responded = (bool)_held; // a keep-alive response just went out

    if (_held) {
      _currentClient = _held;
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
      _held = NetworkClient(); // _currentClient owns the socket again
    }

    if (_currentStatus != HC_WAIT_READ || !_currentClient) {
      _idle_since_ms = millis(); // not holding a connection open
      return;
    }

    // Activity (a response just went out, or a new request is pending) restarts
    // the idle timer; otherwise close once it expires so another client can take
    // the single connection slot, and so the browser never reuses a connection
    // the network stack has already dropped.
    if (responded || _currentClient.available()) {
      _idle_since_ms = millis();
    } else if ((millis() - _idle_since_ms) > KEEP_ALIVE_IDLE_MS) {
      _currentClient.stop();
      _currentClient = NetworkClient();
      _currentStatus = HC_NONE;
      _idle_since_ms = millis();
    }
  }

protected:
  size_t _currentClientWrite(const char *b, size_t l) override {
    // Only response header blocks start with the status line, so content
    // writes (which could legitimately contain the token) are never touched.
    if (l > 8 && l < sizeof(_patched) - 8 && memcmp(b, "HTTP/1.", 7) == 0) {
      // The request is already parsed at this point, so the client's own
      // "Connection" preference is readable (webserver_init collects it).
      // Keep-alive is promised only to HTTP/1.1+ clients that did not ask to
      // close; everything else keeps the stock "Connection: close" behaviour.
      const bool want_keep_alive =
        _currentVersion >= 1 && !header(F("Connection")).equalsIgnoreCase(F("close"));
      size_t patched = want_keep_alive ? patch_keep_alive(b, l) : 0;
      if (patched) {
        _held = _currentClient; // keep the socket alive past this response
        return WebServer::_currentClientWrite(_patched, patched);
      }
    }
    return WebServer::_currentClientWrite(b, l);
  }

private:
  // Replace "Connection: close" with "Connection: keep-alive" in a copy of the
  // header block. Returns the new length, or 0 when the token is absent or the
  // copy would not fit.
  size_t patch_keep_alive(const char *b, size_t l) {
    static const char kClose[] = "Connection: close";
    static const char kKeep[] = "Connection: keep-alive";
    const size_t kCloseLen = sizeof(kClose) - 1;   // 17
    const size_t kGrow = sizeof(kKeep) - sizeof(kClose); // +5 bytes

    const char *hit = nullptr;
    for (size_t i = 0; i + kCloseLen <= l; i++) {
      if (b[i] == 'C' && memcmp(b + i, kClose, kCloseLen) == 0) {
        hit = b + i;
        break;
      }
    }
    if (!hit || l + kGrow > sizeof(_patched)) {
      return 0;
    }

    size_t head = (size_t)(hit - b);
    memcpy(_patched, b, head);
    memcpy(_patched + head, kKeep, sizeof(kKeep) - 1);
    memcpy(_patched + head + (sizeof(kKeep) - 1), b + head + kCloseLen, l - head - kCloseLen);
    return l + kGrow;
  }

  char _patched[512]; // response header blocks are ~100 bytes
  NetworkClient _held;      // set while a response promised keep-alive
  unsigned long _idle_since_ms = 0;
};

static KeepAliveWebServer server(80);

// NOTE: All movement control now lives in the RTOS control task defined in
// motor.h. The web UI only sets shared parameters and start/stop flags; it no
// longer drives CAN directly (except the one-shot origin commands below).

// ---- Request handlers ----
static void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

static void handleStart() {
  // The control task picks this up on its next cycle and begins the sequence.
  motor_control_run(true);
  server.send(200, "text/plain", "started");
}

static void handleStop() {
  motor_control_run(false);
  server.send(200, "text/plain", "stopped");
}

// One-shot trigger: a single pull->release cycle on one side only. The control
// task starts it on its next cycle if idle; it is ignored while running,
// tight, or a one-shot is already in progress. While the one-shot safety switch
// is off (the default) the request is refused with 403 — see /set_oneshot.
static void handleOneshotLeft() {
  if (!motor_oneshot(ONESHOT_LEFT)) {
    server.send(403, "text/plain", "left one-shot disabled (safety switch off)");
    return;
  }
  server.send(200, "text/plain", "left one-shot queued");
}

static void handleOneshotRight() {
  if (!motor_oneshot(ONESHOT_RIGHT)) {
    server.send(403, "text/plain", "right one-shot disabled (safety switch off)");
    return;
  }
  server.send(200, "text/plain", "right one-shot queued");
}

// One-shot safety switch: ?on=1 enables the one-shot buttons, ?on=0 (the
// default) disables them entirely — pending requests are dropped and an
// in-progress cycle is aborted with the interrupt-style stop.
static void handleSetOneshot() {
  bool on = server.hasArg("on") && server.arg("on").toInt() != 0;
  motor_oneshot_enable(on);
  server.send(200, "text/plain", on ? "one-shot enabled" : "one-shot disabled");
}

// Tight mode: while active the control task streams the configured duty
// (params "tight", duty loop) to both motors via comm_can_set_duty(). Stop
// sends a one-shot duty 0 so the controllers don't keep driving after tight
// mode ends.
static void handleTightStart() {
  g_tight = true;
  server.send(200, "text/plain", "tight start");
}

static void handleTightStop() {
  g_tight = false;
  xSemaphoreTake(can_tx_mux, portMAX_DELAY);
  comm_can_set_duty(MOTOR_LEFT_CAN_ID, 0.0f);
  comm_can_set_duty(MOTOR_RIGHT_CAN_ID, 0.0f);
  xSemaphoreGive(can_tx_mux);
  server.send(200, "text/plain", "tight stop");
}

// One-shot origin commands: serialize with the control task's CAN TX.
static void handleSetOriginLeft() {
  xSemaphoreTake(can_tx_mux, portMAX_DELAY);
  comm_can_set_origin(MOTOR_LEFT_CAN_ID, 0);
  xSemaphoreGive(can_tx_mux);
  server.send(200, "text/plain", "left origin set");
}

static void handleSetOriginRight() {
  xSemaphoreTake(can_tx_mux, portMAX_DELAY);
  comm_can_set_origin(MOTOR_RIGHT_CAN_ID, 0);
  xSemaphoreGive(can_tx_mux);
  server.send(200, "text/plain", "right origin set");
}

static void handleSetPara() {
  // Build a full parameter snapshot (unspecified fields keep current values).
  motor_params_t p;
  motor_params_get(&p);

  // "To Pos" group
  if (server.hasArg("pos"))            p.pos = server.arg("pos").toFloat();
  if (server.hasArg("spd_pos"))        p.spd_pos  = constrain(server.arg("spd_pos").toInt(), -40000, 40000); // to-Pos Spd: -40000..40000
  if (server.hasArg("rpa_pos"))        p.rpa_pos  = constrain(server.arg("rpa_pos").toInt(), 0, 60000);       // to-Pos RPA: 0..60000
  if (server.hasArg("approach_pos"))   p.approach_pos_ms = constrain(server.arg("approach_pos").toInt(), 50, 60000);
  // "To Zero" group
  if (server.hasArg("spd_zero"))       p.spd_zero = constrain(server.arg("spd_zero").toInt(), -40000, 40000); // to-Zero Spd: -40000..40000
  if (server.hasArg("rpa_zero"))       p.rpa_zero = constrain(server.arg("rpa_zero").toInt(), 0, 60000);       // to-Zero RPA: 0..60000
  if (server.hasArg("approach_zero"))  p.approach_zero_ms = constrain(server.arg("approach_zero").toInt(), 50, 60000);
  // shared
  if (server.hasArg("cooldown"))       p.cooldown_ms = constrain(server.arg("cooldown").toInt(), 0, 60000);
  if (server.hasArg("overlap"))        p.overlap_ms = constrain(server.arg("overlap").toInt(), 0, 5000);
  // tight mode duty cycle: 0..1.0
  if (server.hasArg("tight"))          p.tight_duty = constrain(server.arg("tight").toFloat(), 0.0f, TIGHT_DUTY_MAX);
  // equation mode inputs
  if (server.hasArg("eq_T"))           p.T    = server.arg("eq_T").toFloat();
  if (server.hasArg("eq_SL"))          p.SL   = server.arg("eq_SL").toFloat();
  if (server.hasArg("eq_L"))           p.L    = server.arg("eq_L").toFloat();
  if (server.hasArg("eq_Lp"))          p.Lp   = server.arg("eq_Lp").toFloat();
  if (server.hasArg("eq_LD"))          p.LD   = server.arg("eq_LD").toFloat();
  if (server.hasArg("eq_H"))           p.H    = server.arg("eq_H").toFloat();
  if (server.hasArg("eq_pd"))          p.pd   = (server.arg("eq_pd").toInt() != 0) ? 1 : 0;

  // Recompute the equation outputs so edits apply live while in equation mode.
  motor_equation_compute(&p);

  motor_params_set(&p);
  server.send(200, "text/plain", "params set");
}

// Set the control mode: ?mode=0 -> pos+spd loop (comm_can_set_pos_spd),
// ?mode=1 -> speed-only loop (comm_can_set_rpm), ?mode=2 -> equation mode,
// ?mode=3 -> position loop (comm_can_set_pos, no time constant).
// Takes effect immediately; exactly one mode is active at a time.
static void handleSetMode() {
  motor_params_t p;
  motor_params_get(&p);

  if (server.hasArg("mode")) p.mode = (motor_mode_t)constrain(server.arg("mode").toInt(), 0, 3);

  // Refresh the equation outputs when switching into equation mode.
  if (p.mode == MODE_EQUATION) motor_equation_compute(&p);

  motor_params_set(&p);
  const char *name = (p.mode == MODE_POS_SPD) ? "mode: pos_spd" :
                     (p.mode == MODE_RPM)     ? "mode: rpm"     :
                     (p.mode == MODE_POS)     ? "mode: pos"     : "mode: equation";
  server.send(200, "text/plain", name);
}

static void handleStatus() {
  String s;
  motor_params_t p;
  motor_params_get(&p);

  s += "running: ";
  s += g_running ? "YES" : "no";
  s += ", phase: "; s += motor_phase_name();
  s += ", mode: ";
  if (p.mode == MODE_POS_SPD)      s += "POS_SPD (pos+spd)";
  else if (p.mode == MODE_RPM)     s += "RPM (speed-only)";
  else if (p.mode == MODE_POS)     s += "POS (position loop)";
  else                             s += "EQUATION (equation)";
  s += ", tight: "; s += g_tight ? "YES" : "no";
  s += "\n";
  // Machine-readable mode id so the page can sync its selector.
  s += "mode_id="; s += (int)p.mode;
  s += "\n";
  // CAN watchdog: which motors have no feedback (bit0=left, bit1=right).
  // The page shows its floating banner from this; the status text itself
  // stays clean.
  s += "can_err="; s += (int)motor_can_error_mask();
  s += "\n";
  // One field per line so the web page can parse and auto-populate the form.
  s += "pos="; s += p.pos;
  s += "\nspd_pos="; s += (int)p.spd_pos;
  s += "\nrpa_pos="; s += (int)p.rpa_pos;
  s += "\napproach_pos="; s += p.approach_pos_ms;
  s += "\nspd_zero="; s += (int)p.spd_zero;
  s += "\nrpa_zero="; s += (int)p.rpa_zero;
  s += "\napproach_zero="; s += p.approach_zero_ms;
  s += "\ncooldown="; s += p.cooldown_ms;
  s += "\noverlap="; s += p.overlap_ms;
  s += " ms\n";
  // Tight mode duty cycle (0..1) — parsed by the page to auto-populate its
  // slider. Fixed decimals so the value regex always matches.
  char dutyline[32];
  snprintf(dutyline, sizeof(dutyline), "tight=%.3f", p.tight_duty);
  s += dutyline;
  s += "\n";
  // One-shot safety switch state (0 = disabled, the default). The page greys
  // out the one-shot buttons from this.
  s += "oneshot_enabled="; s += motor_oneshot_enabled() ? 1 : 0;
  s += "\n";
  // Equation mode inputs (always shown so the form can auto-populate).
  char eqline[48];
  snprintf(eqline, sizeof(eqline), "eq_T=%.3f",   p.T);    s += "\n"; s += eqline;
  snprintf(eqline, sizeof(eqline), "eq_SL=%.2f",  p.SL);   s += "\n"; s += eqline;
  snprintf(eqline, sizeof(eqline), "eq_L=%.2f",   p.L);    s += "\n"; s += eqline;
  snprintf(eqline, sizeof(eqline), "eq_Lp=%.2f",  p.Lp);   s += "\n"; s += eqline;
  snprintf(eqline, sizeof(eqline), "eq_LD=%.2f",  p.LD);   s += "\n"; s += eqline;
  snprintf(eqline, sizeof(eqline), "eq_H=%.2f",   p.H);    s += "\n"; s += eqline;
  s += "\neq_pd="; s += p.pd;
  s += "\n";
  // Equation mode debug: computed outputs (shown while in equation mode).
  if (p.mode == MODE_EQUATION) {
    s += "\n--- equation ---\n";
    snprintf(eqline, sizeof(eqline), "dL=%.3f cm", p.eq_dL);
    s += eqline;
    s += "\nlift: ";
    s += p.eq_lift_rpm;
    s += " ERPM ";
    s += p.eq_lift_ms;
    s += " ms | release: ";
    s += p.eq_release_rpm;
    s += " ERPM ";
    s += p.eq_release_ms;
    s += " ms";
    if (!p.eq_valid) {
      s += "\nEQ INVALID: check T>=0.1s, 0<SL<=2L and geometry (rpm held at 0)";
    }
    s += "\n";
  }
  s += "L pos="; s += motor_left.motor_pos;
  s += " spd="; s += motor_left.motor_spd;
  s += " cur="; s += motor_left.motor_cur;
  s += " temp="; s += (int)motor_left.temp;
  s += " err="; s += (int)motor_left.error;
  s += "\n";
  s += "R pos="; s += motor_right.motor_pos;
  s += " spd="; s += motor_right.motor_spd;
  s += " cur="; s += motor_right.motor_cur;
  s += " temp="; s += (int)motor_right.temp;
  s += " err="; s += (int)motor_right.error;
  server.send(200, "text/plain", s);
}

// Debug endpoint: list AP-connected stations with RSSI. Polled by the page
// once per second. Per-station throughput is not exposed by the ESP32 WiFi
// API, so only MAC + RSSI are available.
static void handleWifi() {
  String s;
  int n = WiFi.softAPgetStationNum();
  s += "AP: "; s += WIFI_SSID;
  s += ", clients: "; s += n;
  s += "\n";

  wifi_sta_list_t sta_list;
  esp_wifi_ap_get_sta_list(&sta_list);

  for (int i = 0; i < sta_list.num && i < 4; i++) {
    wifi_sta_info_t sta = sta_list.sta[i];
    char line[64];
    snprintf(line, sizeof(line), "sta%d: %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d dBm\n",
             i + 1,
             sta.mac[0], sta.mac[1], sta.mac[2],
             sta.mac[3], sta.mac[4], sta.mac[5],
             sta.rssi);
    s += line;
  }
  server.send(200, "text/plain", s);
}

// ---- Init: start the access point and the web server ----
static void webserver_init() {
  WiFi.mode(WIFI_AP);
  // Maximize AP transmit power for range (XIAO ESP32-S3).
  WiFi.setTxPower(WIFI_POWER_21dBm);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/start", HTTP_GET, handleStart);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/oneshot_left", HTTP_GET, handleOneshotLeft);
  server.on("/oneshot_right", HTTP_GET, handleOneshotRight);
  server.on("/set_oneshot", HTTP_GET, handleSetOneshot);
  server.on("/tight_start", HTTP_GET, handleTightStart);
  server.on("/tight_stop", HTTP_GET, handleTightStop);
  server.on("/set_origin_left", HTTP_GET, handleSetOriginLeft);
  server.on("/set_origin_right", HTTP_GET, handleSetOriginRight);
  server.on("/set_para", HTTP_GET, handleSetPara);
  server.on("/set_mode", HTTP_GET, handleSetMode);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/wifi", HTTP_GET, handleWifi);

  // KeepAliveWebServer reads the client's own "Connection" header to decide
  // whether a response may promise keep-alive, so it has to be collected.
  static const char *kCollectedHeaders[] = {"Connection"};
  server.collectHeaders(kCollectedHeaders, 1);

  server.begin();
}

// ---- Per-loop: service the web server only. Movement is handled by the RTOS
// control task (motor.h), so nothing else to do here. ----
static void webserver_loop() {
  server.handleClient();
}

#endif
