#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// --- OLED Configuration (ER-OLEDM0.91-1x-I2C) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C // Change to 0x3D if screen stays blank

#define I2C_SDA 38 // U1 Pin 31
#define I2C_SCL 39 // U1 Pin 32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- SPH0645LM4H I2S Microphone Configuration ---
#define I2S_WS   4  // U1 Pin 4 (LRCLK)
#define I2S_SCK  5  // U1 Pin 5 (BCLK)
#define I2S_SD   6  // U1 Pin 6 (DOUT)
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE 16000
#define BUFFER_LEN  256

int32_t i2s_read_buff[BUFFER_LEN];

void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // SPH0645 requires 32-bit frames
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing I2S driver: %d\n", err);
    while (true);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed setting I2S pins: %d\n", err);
    while (true);
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize I2C on custom PCB pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing Mic...");
  display.display();

  setupI2S();
  delay(500);
}

void loop() {
  size_t bytes_read = 0;
  
  // Read raw 32-bit samples from I2S DMA buffer
  esp_err_t result = i2s_read(I2S_PORT, &i2s_read_buff, sizeof(i2s_read_buff), &bytes_read, portMAX_DELAY);

  if (result == ESP_OK && bytes_read > 0) {
    int samples_read = bytes_read / sizeof(int32_t);
    
    // 1. Calculate DC offset (SPH0645 has a known constant DC bias)
    int64_t sum = 0;
    for (int i = 0; i < samples_read; i++) {
      // Shift right by 14 bits to extract the valid 18-bit/24-bit signed data
      int32_t sample = i2s_read_buff[i] >> 14;
      sum += sample;
    }
    int32_t dc_offset = sum / samples_read;

    // 2. Find peak amplitude after removing DC offset
    int32_t peak_amplitude = 0;
    for (int i = 0; i < samples_read; i++) {
      int32_t sample = (i2s_read_buff[i] >> 14) - dc_offset;
      if (abs(sample) > peak_amplitude) {
        peak_amplitude = abs(sample);
      }
    }

    // 3. Map amplitude to OLED bar width (adjust 8000 based on mic sensitivity)
    int bar_width = map(constrain(peak_amplitude, 0, 8000), 0, 8000, 0, SCREEN_WIDTH - 4);

    // 4. Update OLED Display
    display.clearDisplay();
    
    display.setCursor(0, 0);
    display.print("MIC LEVEL: ");
    display.println(peak_amplitude);

    // Draw outer frame and filled audio bar
    display.drawRect(0, 14, SCREEN_WIDTH, 16, SSD1306_WHITE);
    display.fillRect(2, 16, bar_width, 12, SSD1306_WHITE);
    
    display.display();
  }
}