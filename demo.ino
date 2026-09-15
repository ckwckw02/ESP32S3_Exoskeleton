#include "driver/twai.h"
#include "motor.h"
#include "motor_web.h"

// ESP32-S3 TWAI (CAN) pins - change to match your wiring
#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

// TX buffer shared by the CAN transmit helpers (serialized via can_tx_mux).
twai_message_t canMsg1;

void comm_can_transmit_sid(uint32_t id, const uint8_t *data, uint8_t len) {
  canMsg1.extd = false;
  canMsg1.rtr = false;
  canMsg1.identifier = id;
  canMsg1.data_length_code = len;
  memcpy(canMsg1.data, data, len);
  twai_transmit(&canMsg1, pdMS_TO_TICKS(10));
}

void comm_can_transmit_eid(uint32_t id, const uint8_t *data, uint8_t len) {
  canMsg1.extd = true;
  canMsg1.rtr = false;
  canMsg1.identifier = id;
  canMsg1.data_length_code = len;
  memcpy(canMsg1.data, data, len);
  twai_transmit(&canMsg1, pdMS_TO_TICKS(10));
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(921600);
  Serial.print("\r\Serial init ok\r\n");

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.print("CAN init ok\r\n");
    twai_start();
  } else
    Serial.print("CAN init fault\r\n");

  // Activate the motor
  comm_can_transmit_eid(0x00, (uint8_t[]){0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA}, 8);

  // Start the RTOS control system: 100 Hz CAN RX/TX control task + movement
  // state machine, plus the 10 ms CSV status print timer.
  motor_control_start();

  // Start the WiFi access point and web control server
  webserver_init();
}

void loop() {
  // put your main code here, to run repeatedly:

  // Service the web server only. CAN RX/TX and the movement state machine are
  // handled by the dedicated RTOS control task (motor_control_start), and CSV
  // status printing runs on a 10 ms FreeRTOS timer.
  webserver_loop();
}
