#include <WiFi.h>
#include "esp_wifi.h"

const char* ssid = "Once_Existed";
const char* password = "12345678";

// Increased buffer size to 384 for 40MHz channel
volatile bool new_csi_available = false;
int8_t csi_buffer[384];

void csi_rx_cb(void *ctx, wifi_csi_info_t *info) {
  // Now we look for exactly 384 bytes
  if (!info || !info->buf || info->len != 384) return;
  
  if (!new_csi_available) {
    memcpy((void*)csi_buffer, info->buf, 384);
    new_csi_available = true;
  }
}

void setup() {
  Serial.begin(921600);
  delay(1000);
  
  WiFi.setSleep(false);
  Serial.println("\nInitializing ESP32 CSI (40MHz Mode)...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  wifi_csi_config_t csi_config = {
      .lltf_en           = true,
      .htltf_en          = true,
      .stbc_htltf2_en    = true,
      .ltf_merge_en      = true,
      .channel_filter_en = true,
      .manu_scale        = false,
      .shift             = false,
  };

  esp_wifi_set_csi_config(&csi_config);
  esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL);
  esp_wifi_set_csi(true);
}

void loop() {
  if (new_csi_available) {
    Serial.print("CSI_DATA");
    // Loop through all 192 subcarriers
    for (int i = 0; i < 192; i++) {
      Serial.print(",");
      Serial.print(csi_buffer[i * 2]);
      Serial.print(",");
      Serial.print(csi_buffer[i * 2 + 1]);
    }
    Serial.println();
    new_csi_available = false; 
  }
}