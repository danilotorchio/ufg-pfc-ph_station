#include <Arduino.h>
#include <heltec.h>

#include <WiFi.h>
#include <HTTPClient.h>

#include "tasks.h"
#include "helpers.h"

#define LORA_BAND 915E6 // 915 MHz

const char *WIFI_SSID = "LIVE TIM_7524_2G";
const char *WIFI_PASS = "kp4c4mct4k";

const char *server = "http://35.209.244.193/api/data";
const char *email = "danilotorchio@gmail.com";
const char *password = "123456";

TaskHandle_t TasksHandler = NULL;
WiFiClient client;
HTTPClient http;

char phValue[10];

bool sendingInProgress = false;
bool hasDataToUpload = false;

void setupWiFi()
{
  WiFi.disconnect(true);
  delay(100);

  Heltec.display->drawString(0, 0, "Setting WiFi...");
  Heltec.display->display();

  WiFi.mode(WIFI_MODE_STA);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  byte count = 0;

  while (WiFi.status() != WL_CONNECTED && count < 10)
  {
    count++;
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
    Heltec.display->drawString(90, 0, "OK");
  else
    Heltec.display->drawString(90, 0, "Failed");

  Heltec.display->display();
  delay(100);
}

void setupRTC()
{
  Heltec.display->drawString(0, 10, "Setting RTC...");
  Heltec.display->display();

  struct tm timeinfo;
  configTime(0, 0, "pool.ntp.org");

  if (!getLocalTime(&timeinfo))
  {
    Heltec.display->drawString(90, 10, "Failed");
    Heltec.display->display();
    return;
  }

  setenv("TZ", "<-03>3", 1);
  tzset();

  Heltec.display->drawString(90, 10, "OK");
  Heltec.display->display();
}

void setupLoRa()
{
  Heltec.display->drawString(0, 20, "Setting LoRa...");
  Heltec.display->display();

  LoRa.setSpreadingFactor(12);    // Fator de espalhamento
  LoRa.setSignalBandwidth(250E3); // Largura de banda
  LoRa.setCodingRate4(5);         // Codding rate
  LoRa.setPreambleLength(6);      // Comprimento do preâmbulo
  LoRa.setSyncWord(0x12);         // Palavra de sincronização
  LoRa.crc();

  Heltec.display->drawString(90, 20, "OK");
  Heltec.display->display();
}

void setup()
{
  // Peripherals initialization
  Heltec.begin(true, true, true, true, LORA_BAND);

  Heltec.display->clear();
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);

  setupWiFi();
  setupRTC();
  setupLoRa();

  Heltec.display->drawString(0, 40, "All settings done!");
  Heltec.display->display();

  // Tasks creation
  xTaskCreatePinnedToCore(TaskUploadData, "TaskUploadData", 2048, NULL, 3, &TasksHandler, 1);
  xTaskCreatePinnedToCore(TaskReceiveReading, "TaskReceiveReading", 2048, NULL, 2, &TasksHandler, 1);
}

void loop()
{
}

void TaskReceiveReading(void *pvParams)
{
  while (1)
  {
    int packetSize = LoRa.parsePacket();

    if (packetSize > 0)
    {
      char packet[10] = "";

      while (LoRa.available())
      {
        char ch = (char)LoRa.read();
        strncat(packet, &ch, 1);
      }
      Serial.printf("Received %d bytes: %s\n", packetSize, packet);

      while (sendingInProgress)
        vTaskDelay(2000 / portTICK_PERIOD_MS);

      sprintf(phValue, packet);
      hasDataToUpload = true;
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void TaskUploadData(void *pvParams)
{
  while (1)
  {
    if (hasDataToUpload)
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("Start upload...");

        sendingInProgress = true;
        time_t tt = time(NULL);

        char body[60];
        sprintf(body, "{\"reading\":%s,\"timestamps\":%d}", phValue, int32_t(tt));

        http.begin(server);

        http.setAuthorization(email, password);
        http.addHeader("Content-Type", "application/json");

        int responseStatusCode = http.POST(body);
        http.end();

        sendingInProgress = false;
        hasDataToUpload = false;

        sprintf(phValue, "");
        Serial.printf("Upload done with status %d\n", responseStatusCode);
      }
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}
