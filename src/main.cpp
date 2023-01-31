#include <heltec.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#include "string.h"
#include "BluetoothSerial.h"

#include "config.h"

#define LORA_BAND 915E6 // 915 MHz

// Control variables
BluetoothSerial SerialBT;
Station_Config config;
HTTPClient http;

String bt_cmd;
String bt_cmd_value;
String lora_ph_value;
bool has_data_to_upload = false;

// Functions declarations
void setup_storage();
void setup_peripherals();
void setup_wifi();
void setup_rtc();
void setup_lora();

void bt_data_cb(const uint8_t *buff, size_t size);

// Tasks declarations
void task_bt_execute_cmd(void *params);
void task_lora_receive_data(void *params);
void task_wifi_send_data(void *params);
void task_wifi_scan_net(void *params);

void setup()
{
    Heltec.begin(true, true, true, true, LORA_BAND);
    delay(100);

    SerialBT.onData(bt_data_cb);
    SerialBT.begin("pH_Station");

    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);

    setup_storage();
    setup_peripherals();

    // Tasks
    xTaskCreatePinnedToCore(task_bt_execute_cmd, "task_bt_execute_cmd", 2048, NULL, 3, NULL, 1);
    xTaskCreate(task_lora_receive_data, "task_lora_receive_data", 2048, NULL, 2, NULL);
}

void loop()
{
    // Empty! Jobs are done in tasks.
}

void setup_storage()
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    config.load();
}

void setup_peripherals()
{
    Heltec.display->clear();

    if (config.loaded)
    {
        setup_wifi();
        setup_rtc();
        setup_lora();

        Heltec.display->drawString(0, 40, "phStation is ready!");
    }
    else
    {
        Heltec.display->drawString(0, 0, "Station not configured.");
        Heltec.display->drawString(0, 20, "Connect on mobile device");
        Heltec.display->drawString(0, 30, "to configure.");
    }

    Heltec.display->display();
}

void setup_wifi()
{
    WiFi.disconnect(true);
    delay(100);

    Heltec.display->drawString(0, 0, "Setting WiFi...     ");
    Heltec.display->display();

    WiFi.mode(WIFI_MODE_STA);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_pass.c_str());

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

void setup_rtc()
{
    Heltec.display->drawString(0, 10, "Setting RTC...      ");
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

void setup_lora()
{
    Heltec.display->drawString(0, 20, "Setting LoRa...     ");
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

void bt_data_cb(const uint8_t *buff, size_t size)
{
    if (buff != null && size > 0)
    {
        String str(buff, size);
        str.trim();

        if (!str.isEmpty())
        {
            bt_cmd = str.substring(0, 2);

            if (str.length() > 2)
                bt_cmd_value = str.substring(3);
        }
    }
}

// Tasks implementations
void task_bt_execute_cmd(void *params)
{
    bool clear = false;
    bool reconfigure = false;

    while (1)
    {
        if (!bt_cmd.isEmpty())
        {
            // Respond to device to inform that the request was received
            SerialBT.println("OK");

            // 01: Update station configuration
            if (bt_cmd == "01")
            {
                if (!bt_cmd_value.isEmpty())
                {
                    esp_err_t ret = config.from_json_string(bt_cmd_value.c_str());

                    if (ret == ESP_OK)
                    {
                        if ((ret = config.save()) == ESP_OK)
                        {
                            config.loaded = true;
                            setup_peripherals();
                        }
                    }
                }
            }
            // 02: Scan WiFi networks
            else if (bt_cmd == "02")
            {
                xTaskCreate(task_wifi_scan_net, "task_wifi_scan_net", 4096, NULL, 2, NULL);
            }
            // 08: Clear station configuration
            if (bt_cmd == "08")
            {
                esp_err_t ret = config.clear();

                if (ret == ESP_OK)
                    setup_peripherals();
            }
            else if (bt_cmd == "09")
            {
                Serial.println(config.to_json_string().c_str());
                SerialBT.println(config.to_json_string().c_str());
            }

            clear = true;
        }

        if (clear)
        {
            bt_cmd.clear();
            bt_cmd_value.clear();

            clear = false;
        }

        vTaskDelay(SerialBT.hasClient() ? 400 : 10000 / portTICK_PERIOD_MS);
    }
}

void task_lora_receive_data(void *params)
{
    while (1)
    {
        int packet_size = LoRa.parsePacket();

        if (packet_size > 0)
        {
            char packet[10] = "";

            while (LoRa.available())
            {
                char ch = (char)LoRa.read();
                strncat(packet, &ch, 1);
            }
            Serial.printf("LORA: Received %d bytes: %s\n", packet_size, packet);

            while (has_data_to_upload)
                vTaskDelay(2000 / portTICK_PERIOD_MS);

            lora_ph_value.clear();
            lora_ph_value = packet;

            has_data_to_upload = true;
            xTaskCreate(task_wifi_send_data, "task_wifi_send_data", 2048, NULL, 1, NULL);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void task_wifi_send_data(void *params)
{
    while (1)
    {
        if (has_data_to_upload && WiFi.status() == WL_CONNECTED)
        {
            time_t tt = time(NULL);

            char body[60];
            sprintf(body, "{\"reading\":%s,\"timestamps\":%d}", lora_ph_value.c_str(), int32_t(tt));

            http.begin(config.api_url.c_str());

            http.setAuthorization(config.user_email.c_str(), config.user_pass.c_str());
            http.addHeader("Content-Type", "application/json");

            int responseStatusCode = http.POST(body);
            http.end();

            has_data_to_upload = false;
            lora_ph_value.clear();

            Serial.printf("WiFi: Uploaded done with status %d\n", responseStatusCode);
            vTaskDelete(NULL);
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void task_wifi_scan_net(void *params)
{
    if (SerialBT.hasClient())
    {
        int n = WiFi.scanNetworks();

        if (n == 0)
            SerialBT.println("No networks found");
        else
        {
            JSONVar jsonArray;
            for (int i = 0; i < n; i++)
            {
                JSONVar item;

                item["ssid"] = WiFi.SSID(i).c_str();
                item["rssi"] = WiFi.RSSI(i);

                switch (WiFi.encryptionType(i))
                {
                case WIFI_AUTH_OPEN:
                    item["encryption"] = "open";
                    break;
                case WIFI_AUTH_WEP:
                    item["encryption"] = "WEP";
                    break;
                case WIFI_AUTH_WPA_PSK:
                    item["encryption"] = "WPA_PSK";
                    break;
                case WIFI_AUTH_WPA2_PSK:
                    item["encryption"] = "WPA2_PSK";
                    break;
                case WIFI_AUTH_WPA_WPA2_PSK:
                    item["encryption"] = "WPA_WPA2_PSK";
                    break;
                case WIFI_AUTH_WPA2_ENTERPRISE:
                    item["encryption"] = "WPA2_ENTERPRISE";
                    break;
                case WIFI_AUTH_WPA3_PSK:
                    item["encryption"] = "WPA3_PSK";
                    break;
                case WIFI_AUTH_WPA2_WPA3_PSK:
                    item["encryption"] = "WPA2_WPA3_PSK";
                    break;
                case WIFI_AUTH_WAPI_PSK:
                    item["encryption"] = "WAPI_PSK";
                    break;
                default:
                    item["encryption"] = "unknown";
                }

                jsonArray[i] = item;
            }

            JSONVar json;
            json["networks"] = jsonArray;

            String json_string = JSON.stringify(json);
            json_string.trim();
            json_string.replace(" ", "%20");

            SerialBT.println(json_string.c_str());
        }
    }

    vTaskDelete(NULL);
}
