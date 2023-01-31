#include <string.h>
#include <Arduino_JSON.h>

#include "nvs.h"
#include "nvs_flash.h"

static const char *STATION_STORAGE_NAME = "storage";
static const char *STATION_CONFIG_KEY = "station_config";

class Station_Config
{
public:
    String wifi_ssid;
    String wifi_pass;
    String api_url;
    String user_email;
    String user_pass;
    bool loaded;

    Station_Config();

    esp_err_t load();
    esp_err_t save();
    esp_err_t clear();

    String to_json_string();
    esp_err_t from_json_string(const char *json_string);
};
