#ifndef MOTOR_H
#define MOTOR_H

#include <math.h>
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

// Motor CAN IDs
#define MOTOR_LEFT_CAN_ID 104
#define MOTOR_RIGHT_CAN_ID 105
#define MOTOR_LEFT_RX_ID 0x2968
#define MOTOR_RIGHT_RX_ID 0x2969

// Motor status
typedef struct {
  float motor_pos;       // 电机位置
  float motor_spd;       // 电机速度
  float motor_cur;       // 电机电流
  int8_t temp;           // 电机温度
  int8_t error;          // 电机故障码
  uint32_t update_time;  // 最后更新时间 (ms)
} motor_status_t;

motor_status_t motor_left;
motor_status_t motor_right;

// Spinlock protecting motor status
static portMUX_TYPE motor_mux = portMUX_INITIALIZER_UNLOCKED;

// Forward declarations of CAN transmit functions
void comm_can_transmit_sid(uint32_t id, const uint8_t *data, uint8_t len);
void comm_can_transmit_eid(uint32_t id, const uint8_t *data, uint8_t len);

//int16数据位整理
void buffer_append_int16(uint8_t *buffer, int16_t number, int32_t *index) {
  buffer[(*index)++] = number >> 8;
  buffer[(*index)++] = number;
}

//int32数据位整理
void buffer_append_int32(uint8_t *buffer, int32_t number, int32_t *index) {
  buffer[(*index)++] = number >> 24;
  buffer[(*index)++] = number >> 16;
  buffer[(*index)++] = number >> 8;
  buffer[(*index)++] = number;
}

enum {
  CAN_PACKET_SET_DUTY = 0,
  CAN_PACKET_SET_CURRENT,
  CAN_PACKET_SET_CURRENT_BRAKE,
  CAN_PACKET_SET_RPM,
  CAN_PACKET_SET_POS,
  CAN_PACKET_SET_ORIGIN_HERE,
  CAN_PACKET_SET_POS_SPD
} CAN_PACKET_ID;

/*******************Servo*******************/
//占空比模式
void comm_can_set_duty(uint8_t controller_id, float duty) {
  int32_t send_index = 0;
  uint8_t buffer[4];
  buffer_append_int32(buffer, (int32_t)(duty * 100000.0), &send_index);
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_DUTY << 8), buffer, send_index);
}

//电流环模式
void comm_can_set_current(uint8_t controller_id, float current) {
  int32_t send_index = 0;
  uint8_t buffer[4];
  buffer_append_int32(buffer, (int32_t)(current * 1000.0), &send_index);
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_CURRENT << 8), buffer, send_index);
}

//电流刹车模式
void comm_can_set_cb(uint8_t controller_id, float current) {
  int32_t send_index = 0;
  uint8_t buffer[4];
  buffer_append_int32(buffer, (int32_t)(current * 1000.0), &send_index);
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_CURRENT_BRAKE << 8), buffer, send_index);
}

//速度环模式
void comm_can_set_rpm(uint8_t controller_id, float rpm) {
  int32_t send_index = 0;
  uint8_t buffer[4];
  buffer_append_int32(buffer, (int32_t)rpm, &send_index);
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_RPM << 8), buffer, send_index);
}

//位置环模式
void comm_can_set_pos(uint8_t controller_id, float pos) {
  int32_t send_index = 0;
  uint8_t buffer[4];
  buffer_append_int32(buffer, (int32_t)(pos * 10000.0), &send_index);
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_POS << 8), buffer, send_index);
}

//设置原点模式
void comm_can_set_origin(uint8_t controller_id, uint8_t set_origin_mode) {
  uint8_t buffer;
  buffer = set_origin_mode;
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_ORIGIN_HERE << 8), &buffer, 1);
}

//位置速度环模式
// spd range: -40000..40000, RPA range: 0..60000 (int32_t on the ESP32 side).
// On the CAN wire they are scaled by /10 into int16 fields (max 4000/6000),
// so the bus format is unchanged.
void comm_can_set_pos_spd(uint8_t controller_id, float pos, int32_t spd, int32_t RPA) {
  int32_t send_index = 0;
  int32_t send_index1 = 4;
  uint8_t buffer[8];
  buffer_append_int32(buffer, (int32_t)(pos * 10000.0), &send_index);
  buffer_append_int16(buffer, spd / 10.0, &send_index1);
  buffer_append_int16(buffer, RPA / 10.0, &send_index1);
  comm_can_transmit_eid(controller_id | ((uint32_t)CAN_PACKET_SET_POS_SPD << 8), buffer, send_index1);
}

// Parse a servo feedback frame into a motor_status_t
void motor_parse_servo(const twai_message_t *rx_message, motor_status_t *status) {
  int16_t pos_int = (rx_message)->data[0] << 8 | (rx_message)->data[1];
  int16_t spd_int = (rx_message)->data[2] << 8 | (rx_message)->data[3];
  int16_t cur_int = (rx_message)->data[4] << 8 | (rx_message)->data[5];
  status->motor_pos = (float)(pos_int * 0.1f);   //电机位置
  status->motor_spd = (float)(spd_int * 10.0f);  //电机速度
  status->motor_cur = (float)(cur_int * 0.01f);  //电机电流
  status->temp = (rx_message)->data[6];          //电机温度
  status->error = (rx_message)->data[7];         //电机故障码
  status->update_time = millis();
}

// Poll a received TWAI frame and update the matching motor status (task context)
void motor_poll(const twai_message_t *msg) {
  if (msg->identifier == MOTOR_LEFT_RX_ID) {
    portENTER_CRITICAL(&motor_mux);
    motor_parse_servo(msg, &motor_left);
    portEXIT_CRITICAL(&motor_mux);
  } else if (msg->identifier == MOTOR_RIGHT_RX_ID) {
    portENTER_CRITICAL(&motor_mux);
    motor_parse_servo(msg, &motor_right);
    portEXIT_CRITICAL(&motor_mux);
  }
}

// ============================================================================
// Shared control parameters (written by the web UI task, read by the control
// task). All access goes through the mutex below so values are always
// consistent.
// ============================================================================
typedef struct {
  float    pos;            // target position for the "to Pos" phases (deg)
  int32_t  spd;            // speed limit, range -40000..40000
  int32_t  rpa;            // acceleration limit, range 0..60000
  uint32_t approach_pos_ms;   // max time to wait while approaching target Pos
  uint32_t approach_zero_ms;  // max time to wait while returning to Pos = 0
  uint32_t cooldown_ms;       // idle wait before each "to Pos" phase
} motor_params_t;

static motor_params_t g_params = {
  .pos = 180.0f,
  .spd = 2000,
  .rpa = 2000,
  .approach_pos_ms = 3000,
  .approach_zero_ms = 3000,
  .cooldown_ms = 500,
};

// Mutex protecting g_params (web task writes / control task reads)
static SemaphoreHandle_t params_mux = NULL;

// Mutex serializing CAN transmission: the 100 Hz control task and the web
// handlers (origin buttons) both transmit, so they must not interleave.
SemaphoreHandle_t can_tx_mux = NULL;

// Read a consistent snapshot of the parameters (any task context).
static inline void motor_params_get(motor_params_t *out) {
  xSemaphoreTake(params_mux, portMAX_DELAY);
  *out = g_params;
  xSemaphoreGive(params_mux);
}

// Update one or more parameters from the web UI.
static inline void motor_params_set(const motor_params_t *in) {
  xSemaphoreTake(params_mux, portMAX_DELAY);
  g_params = *in;
  xSemaphoreGive(params_mux);
}

// ============================================================================
// Movement state machine (owned exclusively by the control task).
//
// Sequence per cycle (repeats until STOP):
//   R -> Pos  (wait until approached)
//   R -> 0    (wait until approached)
//   L -> Pos  (cooldown first, wait until approached)
//   L -> 0    (wait until approached)
// ============================================================================
typedef enum {
  PHASE_IDLE = 0,
  PHASE_R_TO_POS,
  PHASE_R_TO_ZERO,
  PHASE_L_TO_POS,
  PHASE_L_TO_ZERO,
} motor_phase_t;

static volatile bool     g_running = false;   // set by web START/STOP
static motor_phase_t     g_phase = PHASE_IDLE;
static uint32_t          g_phase_start_ms = 0; // when the current phase began

// Position tolerance (deg) used to decide that a motor has "approached" its
// target. A motor counts as arrived when |pos - target| <= this value OR the
// phase time limit elapses (safety timeout so we never hang).
#define APPROACH_TOL_DEG 10.0f

static const char *phase_name(motor_phase_t p) {
  switch (p) {
    case PHASE_R_TO_POS:  return "R->Pos";
    case PHASE_R_TO_ZERO: return "R->Zero";
    case PHASE_L_TO_POS:  return "L->Pos";
    case PHASE_L_TO_ZERO: return "L->Zero";
    default:              return "Idle";
  }
}

// True when the given motor is within tolerance of target, or the phase time
// limit has elapsed (whichever comes first).
static bool motor_approached(const motor_status_t *st, float target, uint32_t timeout_ms) {
  if (fabsf(st->motor_pos - target) <= APPROACH_TOL_DEG) return true;
  return (millis() - g_phase_start_ms) >= timeout_ms;
}

// Send a pos_spd command with CAN TX serialization.
static void motor_cmd_pos_spd(uint8_t id, float pos, int32_t spd, int32_t rpa) {
  xSemaphoreTake(can_tx_mux, portMAX_DELAY);
  comm_can_set_pos_spd(id, pos, spd, rpa);
  xSemaphoreGive(can_tx_mux);
}

// ============================================================================
// Control task: runs at 100 Hz (CAN RX + TX rate).
//   - drains all pending TWAI frames into the motor status structs
//   - advances the movement state machine
//   - re-sends the active motor's pos_spd command every cycle (100 Hz)
// ============================================================================
static void control_task(void *arg) {
  twai_message_t rx;
  for (;;) {
    // --- CAN RX: drain everything that arrived since last cycle ---
    while (twai_receive(&rx, pdMS_TO_TICKS(0)) == ESP_OK) {
      motor_poll(&rx);
    }

    if (g_running) {
      motor_params_t p;
      motor_params_get(&p);

      // If we just started (or re-started after STOP), begin the sequence at R->Pos.
      if (g_phase == PHASE_IDLE) {
        g_phase = PHASE_R_TO_POS;
        g_phase_start_ms = millis();
      }

      // Determine the active motor and its current target for this phase.
      uint8_t id = 0;
      float   target = 0.0f;
      uint32_t timeout = 0;
      bool    has_target = true;

      switch (g_phase) {
        case PHASE_R_TO_POS:
          id = MOTOR_RIGHT_CAN_ID; target = -p.pos; timeout = p.approach_pos_ms; break; // right uses negative pos
        case PHASE_R_TO_ZERO:
          id = MOTOR_RIGHT_CAN_ID; target = 0.0f;   timeout = p.approach_zero_ms; break;
        case PHASE_L_TO_POS:
          id = MOTOR_LEFT_CAN_ID;  target = p.pos;  timeout = p.approach_pos_ms; break; // left uses positive pos
        case PHASE_L_TO_ZERO:
          id = MOTOR_LEFT_CAN_ID;  target = 0.0f;   timeout = p.approach_zero_ms; break;
        default:
          has_target = false;
          break;
      }

      if (has_target) {
        const motor_status_t *st = (id == MOTOR_RIGHT_CAN_ID) ? &motor_right : &motor_left;

        // Phase complete? Advance to the next phase.
        if (motor_approached(st, target, timeout)) {
          switch (g_phase) {
            case PHASE_R_TO_POS:  g_phase = PHASE_R_TO_ZERO; break;
            case PHASE_R_TO_ZERO: g_phase = PHASE_L_TO_POS;  vTaskDelay(pdMS_TO_TICKS(p.cooldown_ms)); break; // cooldown before L->Pos
            case PHASE_L_TO_POS:  g_phase = PHASE_L_TO_ZERO; break;
            case PHASE_L_TO_ZERO: g_phase = PHASE_R_TO_POS;  vTaskDelay(pdMS_TO_TICKS(p.cooldown_ms)); break; // cooldown before R->Pos (next cycle)
            default: break;
          }
          g_phase_start_ms = millis();
        }

        // --- CAN TX: command the active motor at 100 Hz while its phase runs ---
        motor_cmd_pos_spd(id, target, p.spd, p.rpa);
      }
    } else {
      // Not running: park in idle.
      g_phase = PHASE_IDLE;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // 100 Hz control cycle
  }
}

static TaskHandle_t control_task_handle = NULL;

// Print both motors as CSV with a shared timestamp: Time,Side,pos,spd,cur,temp,err
void motor_print_csv() {
  uint32_t t = millis();
  motor_status_t l, r;
  portENTER_CRITICAL(&motor_mux);
  l = motor_left;
  r = motor_right;
  portEXIT_CRITICAL(&motor_mux);

  char line[64];
  snprintf(line, sizeof(line), "%lu,L,%.1f,%.1f,%.2f,%d,%d\n", (unsigned long)t, l.motor_pos, l.motor_spd, l.motor_cur, (int)l.temp, (int)l.error);
  Serial.print(line);
  snprintf(line, sizeof(line), "%lu,R,%.1f,%.1f,%.2f,%d,%d\n", (unsigned long)t, r.motor_pos, r.motor_spd, r.motor_cur, (int)r.temp, (int)r.error);
  Serial.print(line);
}

// Callback for the periodic CSV print timer.
static void status_timer_cb(TimerHandle_t t) {
  motor_print_csv();
}

// Start the RTOS control system: 100 Hz control task + 10 ms CSV print timer.
void motor_control_start() {
  if (params_mux == NULL) params_mux = xSemaphoreCreateMutex();
  if (can_tx_mux == NULL) can_tx_mux = xSemaphoreCreateMutex();

  // Dedicated control task at 100 Hz, higher priority than the Arduino loop
  // so CAN RX/TX and phase timing stay real-time even while serving HTTP.
  if (control_task_handle == NULL) {
    xTaskCreate(control_task, "mctrl", 4096, NULL, configMAX_PRIORITIES - 2, &control_task_handle);
  }

  // Periodic CSV status print (10 ms).
  static TimerHandle_t status_timer = NULL;
  if (status_timer == NULL) {
    status_timer = xTimerCreate("mstat", pdMS_TO_TICKS(10), true, NULL, status_timer_cb);
  }
  Serial.println("Time,Side,pos,spd,cur,temp,err");
  if (!xTimerIsTimerActive(status_timer)) xTimerStart(status_timer, 0);
}

// Web UI: start / stop the movement sequence.
void motor_control_run(bool run) { g_running = run; }

// Web UI status endpoint helper: current phase name.
const char *motor_phase_name() { return phase_name(g_phase); }

#endif
