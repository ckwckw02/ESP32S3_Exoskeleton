#ifndef MOTOR_WEB_H
#define MOTOR_WEB_H

#include <WiFi.h>
#include <WebServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "index_html.h"
#include "motor.h"

// ---- WiFi access point credentials (change as needed) ----
static const char *WIFI_SSID = "MotorControl";
static const char *WIFI_PASS = "12345678";

// ---- Web server (port 80) ----
static WebServer server(80);

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

  if (server.hasArg("pos"))            p.pos = server.arg("pos").toFloat();
  if (server.hasArg("spd"))            p.spd = constrain(server.arg("spd").toInt(), -40000, 40000); // Spd: -40000..40000
  if (server.hasArg("rpa"))            p.rpa = constrain(server.arg("rpa").toInt(), 0, 60000);       // RPA: 0..60000
  if (server.hasArg("approach_pos"))   p.approach_pos_ms = constrain(server.arg("approach_pos").toInt(), 50, 60000);
  if (server.hasArg("approach_zero"))  p.approach_zero_ms = constrain(server.arg("approach_zero").toInt(), 50, 60000);
  if (server.hasArg("cooldown"))       p.cooldown_ms = constrain(server.arg("cooldown").toInt(), 0, 60000);

  motor_params_set(&p);
  server.send(200, "text/plain", "params set");
}

static void handleStatus() {
  String s;
  motor_params_t p;
  motor_params_get(&p);

  s += "running: ";
  s += g_running ? "YES" : "no";
  s += ", phase: "; s += motor_phase_name();
  s += "\n";
  s += "pos="; s += p.pos;
  s += " spd="; s += (int)p.spd;
  s += " rpa="; s += (int)p.rpa;
  s += "\n";
  s += "approach_pos: "; s += p.approach_pos_ms; s += " ms, approach_zero: "; s += p.approach_zero_ms; s += " ms, cooldown: "; s += p.cooldown_ms; s += " ms\n";
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

// ---- Init: start the access point and the web server ----
static void webserver_init() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/start", HTTP_GET, handleStart);
  server.on("/stop", HTTP_GET, handleStop);
  server.on("/set_origin_left", HTTP_GET, handleSetOriginLeft);
  server.on("/set_origin_right", HTTP_GET, handleSetOriginRight);
  server.on("/set_para", HTTP_GET, handleSetPara);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
}

// ---- Per-loop: service the web server only. Movement is handled by the RTOS
// control task (motor.h), so nothing else to do here. ----
static void webserver_loop() {
  server.handleClient();
}

#endif
