////------------Remove ArduinoBLE library-------------------------///
#include "stdio.h"            // Library STDIO
#include "driver/ledc.h"      // Library ESP32 LEDC
#include "driver/pcnt.h"      // Library ESP32 PCNT
#include "soc/pcnt_struct.h"  // Library ESP32 PCNT
//#include "esp32/rom/gpio.h"   // Library ESP32 GPIO
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>

#define I2C_SDA 6
#define I2C_SCL 7
//--------------------RDA5807 -------------------------------------------------
#define RDA5807M_RANDOM_ACCESS_ADDRESS 0x11
// Registers
#define RDA5807M_REG_CONFIG 0x02
#define RDA5807M_REG_TUNING 0x03
#define RDA5807M_REG_VOLUME 0x05
#define RDA5807M_REG_RSSI   0x0B
// FLAGS
#define RDA5807M_FLG_DHIZ 0x8000 
#define RDA5807M_FLG_DMUTE 0x4000  
#define RDA5807M_FLG_BASS 0x1000
#define RDA5807M_FLG_ENABLE word(0x0001) 
#define RDA5807M_FLG_TUNE word(0x0010)
#define RDA5807M_FLG_MONO 0x2000 
#define RDA5807M_BIT_MUTE 14
// MASKS
#define RDA5807M_CHAN_MASK 0xFFC0
#define RDA5807M_CHAN_SHIFT 6
#define RDA5807M_VOLUME_MASK word(0x000F)
#define RDA5807M_VOLUME_SHIFT 0
#define RDA5807M_RSSI_MASK 0xFE00
#define RDA5807M_RSSI_SHIFT 9

#ifndef LEDC_HIGH_SPEED_MODE            
#define LEDC_HIGH_SPEED_MODE LEDC_LOW_SPEED_MODE
#endif

#define PCNT_COUNT_UNIT PCNT_UNIT_0        // Set Pulse Counter Unit - 0
#define PCNT_COUNT_CHANNEL PCNT_CHANNEL_0  // Set Pulse Counter channel - 0

#define PCNT_INPUT_SIG_IO GPIO_NUM_10    // Set Pulse Counter input - Freq Meter Input GPIO 34
#define LEDC_HS_CH0_GPIO GPIO_NUM_9     // LEDC output - pulse generator - GPIO_33
#define PCNT_INPUT_CTRL_IO GPIO_NUM_11   // Set Pulse Counter Control GPIO pin - HIGH = count up, LOW = count down
#define OUTPUT_CONTROL_GPIO GPIO_NUM_8  // Timer output control port - GPIO_32
#define PCNT_H_LIM_VAL overflow          // Overflow of Pulse Counter

#define BLE_SERVICE_UUID     "12345678-1234-1234-1234-1234567890ab"
#define CHAR_FREQ_UUID       "abcd1111-1234-5678-1234-56789abcdef0"
#define CHAR_SET_FREQ_UUID   "abcd2222-1234-5678-1234-56789abcdef0"

bool flag = true;                // Flag to enable print frequency reading
uint32_t overflow = 20000;       // Max Pulse Counter value 20000
int16_t pulses = 0;              // Pulse Counter value
uint32_t multPulses = 0;         // Number of PCNT counter overflows
uint32_t sample_time = 100000;  // Sample time of 1 second to count pulses (change the value to calibrate frequency meter)
uint32_t osc_freq = 465000;       // Oscillator frequency - initial 16000 Hz (may be 1 Hz to 40 MHz)
uint32_t mDuty = 0;              // Duty value
uint32_t resolution = 0;         // Resolution value of Oscillator
float frequency = 0;             // frequency value

esp_timer_create_args_t create_args;  // Create an esp_timer instance
esp_timer_handle_t timer_handle;      // Create an single timer

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;  // portMUX_TYPE to do synchronism

BLECharacteristic* freqChar;

void init_osc_freq()  // Initialize Oscillator to test Freq Meter
{
  resolution = (log(80000000 / osc_freq) / log(2)) / 2;
  if (resolution < 1) resolution = 1;
  mDuty = (pow(2, resolution)) / 2;

  ledc_timer_config_t ledc_timer = {};
  ledc_timer.duty_resolution = ledc_timer_bit_t(resolution);
  ledc_timer.freq_hz = osc_freq;
  ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_timer.timer_num = LEDC_TIMER_0;
  ledc_timer_config(&ledc_timer);

  ledc_channel_config_t ledc_channel = {};
  ledc_channel.channel = LEDC_CHANNEL_0;
  ledc_channel.duty = mDuty;
  ledc_channel.gpio_num = LEDC_HS_CH0_GPIO;
  ledc_channel.intr_type = LEDC_INTR_DISABLE;
  ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_channel.timer_sel = LEDC_TIMER_0;
  ledc_channel_config(&ledc_channel);
}

static void IRAM_ATTR pcnt_intr_handler(void *arg)
{
  portENTER_CRITICAL_ISR(&timerMux);
  multPulses++;
  PCNT.int_clr.val = BIT(PCNT_COUNT_UNIT);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void init_PCNT(void)
{
  pcnt_config_t pcnt_config = {};
  pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO;
  pcnt_config.ctrl_gpio_num = PCNT_INPUT_CTRL_IO;
  pcnt_config.unit = PCNT_COUNT_UNIT;
  pcnt_config.channel = PCNT_COUNT_CHANNEL;
  pcnt_config.counter_h_lim = PCNT_H_LIM_VAL;
  pcnt_config.pos_mode = PCNT_COUNT_INC;
  pcnt_config.neg_mode = PCNT_COUNT_INC;
  pcnt_config.lctrl_mode = PCNT_MODE_DISABLE;
  pcnt_config.hctrl_mode = PCNT_MODE_KEEP;
  pcnt_unit_config(&pcnt_config);

  pcnt_counter_pause(PCNT_COUNT_UNIT);
  pcnt_counter_clear(PCNT_COUNT_UNIT);

  pcnt_event_enable(PCNT_COUNT_UNIT, PCNT_EVT_H_LIM);
  pcnt_isr_register(pcnt_intr_handler, NULL, 0, NULL);
  pcnt_intr_enable(PCNT_COUNT_UNIT);
  pcnt_counter_resume(PCNT_COUNT_UNIT);
}

void read_PCNT(void *p)
{
  gpio_set_level(OUTPUT_CONTROL_GPIO, 0);
  pcnt_get_counter_value(PCNT_COUNT_UNIT, &pulses);
  flag = true;
}

void init_frequencyMeter() {
  init_osc_freq();
  init_PCNT();
  

#if CONFIG_IDF_TARGET_ESP32 // класична ESP32		
gpio_pad_select_gpio(OUTPUT_CONTROL_GPIO);		
#else // S3, C3, C6 …		
gpio_reset_pin((gpio_num_t)OUTPUT_CONTROL_GPIO);		
#endif


  gpio_set_direction(OUTPUT_CONTROL_GPIO, GPIO_MODE_OUTPUT);
  create_args.callback = read_PCNT;
  esp_timer_create(&create_args, &timer_handle);
}

void setGeneratorFreq(uint32_t hz) {
  Serial.print("🎛️ Generator set to: ");
  Serial.print(hz);
  Serial.println(" Hz");
  osc_freq = hz;
  init_osc_freq();
}

class SetFreqCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    int freq = atoi(pCharacteristic->getValue().c_str());
    setGeneratorFreq(freq*1000);
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("🚀 Starting frequency monitor...");

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  setupRDA5807();
  setRDA5807frequency(1015);
  setRDA5807volume(15); 

  BLEDevice::init("MagicTube2");
  BLEServer* server = BLEDevice::createServer();
  BLEService* service = server->createService(BLE_SERVICE_UUID);

  freqChar = service->createCharacteristic(
    CHAR_FREQ_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  freqChar->addDescriptor(new BLE2902());

  BLECharacteristic* setFreqChar = service->createCharacteristic(
    CHAR_SET_FREQ_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  setFreqChar->setCallbacks(new SetFreqCallback());

  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->start();

  Serial.println("📡 BLE server running...");
  init_frequencyMeter();  
}

void loop() {
  static unsigned long lastNotify = 0;
  unsigned long now = millis();

  if (flag == true)
  {
    flag = false;
    frequency = (pulses + (multPulses * overflow)) / 2  * (1000000./sample_time);
    Serial.print("Frequency: ");
    Serial.print(frequency);
    Serial.println(" Hz");

    multPulses = 0;
    pcnt_counter_clear(PCNT_COUNT_UNIT);
    esp_timer_start_once(timer_handle, sample_time);
    gpio_set_level(OUTPUT_CONTROL_GPIO, 1);
  }
  char freqStr[16];
  dtostrf(frequency, 0, 2, freqStr); 
  if (now - lastNotify >= 100) {
    freqChar->setValue(freqStr);
    freqChar->notify();
    lastNotify = now;
  }
}




void setRegister(uint8_t reg, const uint16_t value) {
  Wire.beginTransmission(0x11);
  Wire.write(reg);
  Wire.write(highByte(value));
  Wire.write(lowByte(value));
  Wire.endTransmission(true);
}
 
uint16_t getRegister(uint8_t reg) {
  uint16_t result;
  Wire.beginTransmission(RDA5807M_RANDOM_ACCESS_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(0x11, 2, true);
  result = (uint16_t)Wire.read() << 8;
  result |= Wire.read();
  return result;
}

void setupRDA5807()
{
uint16_t reg02h, reg03h, reg05h, reg0Bh;
  reg02h = RDA5807M_FLG_ENABLE | RDA5807M_FLG_DHIZ | RDA5807M_FLG_DMUTE;
  setRegister(RDA5807M_REG_CONFIG, reg02h);
  reg02h |= RDA5807M_FLG_BASS; 
  setRegister(RDA5807M_REG_CONFIG, reg02h);
  reg02h |= RDA5807M_FLG_MONO; 
  setRegister(RDA5807M_REG_CONFIG, reg02h);
}

void setRDA5807frequency(uint16_t freq)
{
uint16_t reg02h, reg03h, reg05h, reg0Bh;
  reg03h = (freq - 870) << RDA5807M_CHAN_SHIFT; 
  setRegister(RDA5807M_REG_TUNING, reg03h | RDA5807M_FLG_TUNE);
}


void setRDA5807volume(uint8_t v)
{
  uint16_t reg02h, reg03h, reg05h, reg0Bh;
  reg05h = getRegister(RDA5807M_REG_VOLUME); 
  reg05h &= ~RDA5807M_VOLUME_MASK; 
  reg05h |= v << RDA5807M_VOLUME_SHIFT; 
  setRegister(RDA5807M_REG_VOLUME, reg05h);
}