#include <heltec.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "string.h"
#include "config.h"

#define LORA_BAND 915E6 // 915 MHz

#define BLE_SERVER_NAME "pH Station"
#define BLE_SERVICE_UUID "65426fae-f917-4c56-9026-2720358d212a"
#define BLE_CHARACTERISTIC_UUID "0951f60d-8f74-4612-9b85-d38cb6b36395"

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristc = NULL;

// Control variables
Station_Config config;
HTTPClient http;

String ble_cmd;
String ble_cmd_value;
String lora_ph_value;
String lora_temp_value;

bool has_data_to_upload = false;
bool ble_device_connected = false;
bool ble_restart_advertising = false;

// Functions declarations
void init_peripherals();
void setup_storage();
void setup_ble();
void setup_wifi();
void setup_rtc();
void setup_lora();

// Tasks declarations
void task_bt_execute_cmd(void *params);
void task_lora_receive_data(void *params);
void task_wifi_send_data(void *params);
void task_wifi_scan_net(void *params);
void task_turn_display_onoff(void *params);

class StationBLEServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        ble_device_connected = true;
        Serial.println("BLE: Device connected");
    }
    void onDisconnect(BLEServer *pServer)
    {
        ble_device_connected = false;
        ble_restart_advertising = true;
        Serial.println("BLE: Device disconnected");
    }
};

class StationBLECharacteristicCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristc)
    {
        std::string value = pCharacteristc->getValue();

        String str = String(value.c_str());
        str.trim();

        if (!str.isEmpty())
        {
            Serial.printf("BLE: Command received=%s\n", str.c_str());
            ble_cmd = str.substring(0, 2);

            if (str.length() > 2)
                ble_cmd_value = str.substring(3);
        }
    }
};

void setup()
{
    Heltec.begin(true, true, true, true, LORA_BAND);
    delay(100);

    Heltec.display->setFont(ArialMT_Plain_10);
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);

    // Setups
    init_peripherals();

    // Tasks
    xTaskCreatePinnedToCore(task_bt_execute_cmd, "task_bt_execute_cmd", 2048, NULL, 3, NULL, 1);
    xTaskCreate(task_lora_receive_data, "task_lora_receive_data", 2048, NULL, 2, NULL);

    // Turn off the display after 10 seconds
    xTaskCreate(task_turn_display_onoff, "task_turn_display_onoff", 1024, 0, 1, NULL);
}

void loop()
{
    if (ble_restart_advertising)
    {
        ble_restart_advertising = false;
        pServer->startAdvertising();
    }

    // Jobs are done in tasks.
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

void init_peripherals()
{
    Heltec.display->clear();
    setup_storage();

    if (config.loaded)
    {
        Serial.println(config.to_json_string());

        setup_ble();
        setup_wifi();
        setup_rtc();
        setup_lora();

        Heltec.display->drawString(0, 50, "phStation is ready!");
    }
    else
    {
        Heltec.display->drawString(0, 0, "Station not configured.");
        Heltec.display->drawString(0, 20, "Connect on mobile device");
        Heltec.display->drawString(0, 30, "to configure.");
    }

    Heltec.display->display();
}

void setup_ble()
{
    Heltec.display->drawString(0, 0, "Setting BLE...     ");
    Heltec.display->display();

    BLEDevice::init(BLE_SERVER_NAME);

    // BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new StationBLEServerCallbacks());

    // BLE Service
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // BLE Characteristics and Descriptors
    pCharacteristc = pService->createCharacteristic(
        BLE_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_INDICATE);

    pCharacteristc->setCallbacks(new StationBLECharacteristicCallbacks());
    pCharacteristc->addDescriptor(new BLE2902());

    // Start service
    pService->start();

    // Start advertisng
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x0);

    pServer->startAdvertising();
    // BLEDevice::startAdvertising();

    Heltec.display->drawString(90, 0, "OK");
    Heltec.display->display();
}

void setup_wifi()
{
    WiFi.disconnect(true);
    delay(100);

    Heltec.display->drawString(0, 10, "Setting WiFi...     ");
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
        Heltec.display->drawString(90, 10, "OK");
    else
        Heltec.display->drawString(90, 10, "Failed");

    Heltec.display->display();
    delay(100);
}

void setup_rtc()
{
    Heltec.display->drawString(0, 20, "Setting RTC...      ");
    Heltec.display->display();

    struct tm timeinfo;
    configTime(0, 0, "pool.ntp.org");

    if (!getLocalTime(&timeinfo))
    {
        Heltec.display->drawString(90, 20, "Failed");
        Heltec.display->display();
        return;
    }

    setenv("TZ", "<-03>3", 1);
    tzset();

    Heltec.display->drawString(90, 20, "OK");
    Heltec.display->display();
}

void setup_lora()
{
    Heltec.display->drawString(0, 30, "Setting LoRa...     ");
    Heltec.display->display();

    LoRa.setSpreadingFactor(12);    // Fator de espalhamento
    LoRa.setSignalBandwidth(250E3); // Largura de banda
    LoRa.setCodingRate4(5);         // Codding rate
    LoRa.setPreambleLength(6);      // Comprimento do preâmbulo
    LoRa.setSyncWord(0x12);         // Palavra de sincronização
    LoRa.crc();

    Heltec.display->drawString(90, 30, "OK");
    Heltec.display->display();
}

// Tasks implementations
void task_bt_execute_cmd(void *params)
{
    bool clear = false;
    bool reconfigure = false;

    while (1)
    {
        if (ble_device_connected && !ble_cmd.isEmpty())
        {
            pCharacteristc->setValue("OK");
            pCharacteristc->notify();
            delay(10);

            // 01: Update station configuration
            if (ble_cmd == "01")
            {
                if (!ble_cmd_value.isEmpty())
                {
                    esp_err_t ret = config.from_json_string(ble_cmd_value.c_str());

                    if (ret == ESP_OK)
                    {
                        if ((ret = config.save()) == ESP_OK)
                        {
                            config.loaded = true;
                            // restart wifi configuration
                        }
                    }
                }
            }
            // 02: Scan WiFi networks
            else if (ble_cmd == "02")
            {
                xTaskCreate(task_wifi_scan_net, "task_wifi_scan_net", 4096, NULL, 2, NULL);
            }
            // 08: Clear station configuration
            if (ble_cmd == "08")
            {
                esp_err_t ret = config.clear();

                if (ret == ESP_OK)
                {
                    // restart wifi configuration
                }
            }
            else if (ble_cmd == "09")
            {
                Serial.println(config.to_json_string().c_str());

                pCharacteristc->setValue(config.to_json_string().c_str());
                pCharacteristc->notify();
            }

            clear = true;
        }

        if (clear)
        {
            ble_cmd.clear();
            ble_cmd_value.clear();

            clear = false;
        }

        vTaskDelay(ble_device_connected ? 400 : 10000 / portTICK_PERIOD_MS);
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

            String strPacket = String(packet);

            lora_ph_value.clear();
            lora_ph_value = strPacket.substring(0, strPacket.indexOf(";"));

            lora_temp_value.clear();
            lora_temp_value = strPacket.substring(strPacket.indexOf(";") + 1);

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
            sprintf(body, "{\"reading\":%s,\"temp\":%s,\"timestamps\":%d}", lora_ph_value.c_str(), lora_temp_value.c_str(), int32_t(tt));
            Serial.println(body);

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
    if (ble_device_connected)
    {
        int n = WiFi.scanNetworks();

        if (n == 0)
        {
            pCharacteristc->setValue("[]");
            pCharacteristc->notify();
        }
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

            pCharacteristc->setValue(json_string.c_str());
            pCharacteristc->notify();
        }
    }

    vTaskDelete(NULL);
}

void task_turn_display_onoff(void *params)
{
    int param = (int)params;

    if (param == 0)
    {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        Heltec.display->displayOff();
    }
    else
        Heltec.display->displayOn();

    vTaskDelete(NULL);
}
