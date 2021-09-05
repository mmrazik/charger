#include <Wire.h>
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_adc_cal.h"
#include <U8g2lib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include "WiFiConfig.h"



U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 22, /* data=*/ 21, /* reset=*/ U8X8_PIN_NONE);
OneWire oneWire(19);
DallasTemperature sensors(&oneWire);

static const adc_channel_t voltage_sensor = ADC_CHANNEL_0;
static const adc_channel_t current_sensor = ADC_CHANNEL_3;

static esp_adc_cal_characteristics_t *adc_chars;


static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

TaskHandle_t ChargerMonitorTask;
httpd_handle_t httpServer;


#define SAMPLES 128

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.println("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.println("Characterized using eFuse Vref\n");
    } else {
        Serial.println("Characterized using Default Vref\n");
    }
}

#define DEFAULT_VREF 1128

void ConnectToWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("charger");
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to "); Serial.println(ssid);

  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(500);

    if ((++i % 16) == 0)
    {
      Serial.println(F(" still trying to connect"));
      Serial.println(WiFi.status());
      Serial.println(WiFi.localIP());
    }
  }

  Serial.print(F("Connected. My IP address is: "));
  Serial.println(WiFi.localIP());
}



void ChargerMonitorMainLoop(void *parameter) {
    sensors.requestTemperatures();

    uint32_t adc_reading  = 0;
    for (int i=0; i < SAMPLES; i++) {
        adc_reading += adc1_get_raw((adc1_channel_t) voltage_sensor);
    }
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading/SAMPLES, adc_chars);
    //Serial.println(voltage);
    //3900/(3900+27000.0)*V = Vi
    double measured_voltage = voltage/(3900.0/(3900.0+27000.0))/1000.0;
    Serial.print(measured_voltage);
    Serial.print("V ");


    adc_reading = 0;
    for (int i=0; i < SAMPLES; i++) {
        adc_reading += adc1_get_raw((adc1_channel_t) current_sensor);
    }
    uint32_t current_voltage = esp_adc_cal_raw_to_voltage(adc_reading/SAMPLES, adc_chars);
    //int offset = 1634; //3.3V

    double current = (double) abs((long) (2500 - current_voltage)) / (double)100.0;


/*
    adc_reading = 0;
    for (int i=0; i < SAMPLES; i++) {
        adc_reading += adc1_get_raw((adc1_channel_t) current_sensor2);
    }
    uint32_t current_voltage2 = esp_adc_cal_raw_to_voltage(adc_reading/SAMPLES, adc_chars);
    double current2 = abs((double)(1637.0 - current_voltage2)) / 100.0;
*/
    //int current, current_voltage, measured_voltage;
    Serial.print(current);
    Serial.print("A (");
    Serial.print(current_voltage);
    Serial.print("mV) ");

/*
    Serial.print(current2);
    Serial.print("A2 (");
    Serial.print(current_voltage2);
    Serial.print("mV2) ");
*/
    Serial.print(current*measured_voltage);
    Serial.println("W");

/*
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvR08_te);
    u8g2.drawStr(10, 15, String(String("Voltage: ") + String(measured_voltage) + String("V")).c_str());
    u8g2.drawStr(10, 30, String(String("Current: ") + String(current) + String("A")).c_str());
    u8g2.drawStr(10, 45, String(String("Power: ") + String(current*measured_voltage) + String("W")).c_str());
    u8g2.drawStr(10, 60, String(String("Total: ") + String(current*measured_voltage) + String("Wh")).c_str());

    u8g2.drawStr(80, 40, String(String(sensors.getTempCByIndex(0)) + String(" ") + String((char) 176) + String("C")).c_str());
    u8g2.sendBuffer();
*/

    delay(100000);
}


esp_err_t get_prometheus_metrics_handler(httpd_req_t *req)
{
    httpd_resp_send(req, String("Hello world").c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_uri_t uri_metrics = {
    .uri      = "/metrics",
    .method   = HTTP_GET,
    .handler  = get_prometheus_metrics_handler,
    .user_ctx = NULL
};


httpd_handle_t startWebServer(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_metrics);
    }
    return server;
}

void setup() {
    u8g2.begin();
    sensors.begin();

    Serial.begin(115200);
    Serial.println("Hello");


    //adc2_vref_to_gpio((gpio_num_t)19);
    //delay(500000);


    adc1_config_width(width);
    adc1_config_channel_atten((adc1_channel_t)voltage_sensor, atten);
    adc1_config_channel_atten((adc1_channel_t)current_sensor, atten);

    adc_chars = (esp_adc_cal_characteristics_t *) calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

/*
    xTaskCreatePinnedToCore(
      ChargerMonitorMainLoop,
      "ChargerMonitorTask",
      10000,  // Stack size in words
      NULL,   // Task input parameter
      0,      // Priority of the task
      &ChargerMonitorTask,
      0); //Core where the task should run
*/
    ConnectToWiFi();
    httpServer = startWebServer();

}

void loop() {
    delay(1000);
}