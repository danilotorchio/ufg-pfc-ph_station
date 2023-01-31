#include "config.h"

Station_Config::Station_Config()
{
    wifi_ssid = "";
    wifi_pass = "";
    api_url = "";
    user_email = "";
    user_pass = "";

    loaded = false;
}

esp_err_t Station_Config::load()
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(STATION_STORAGE_NAME, NVS_READWRITE, &handle);

    if (err == ESP_OK)
    {
        size_t required_size;
        char *value;

        err = nvs_get_str(handle, STATION_CONFIG_KEY, NULL, &required_size);

        if (err == ESP_OK)
        {
            value = (char *)malloc(required_size);
            err = nvs_get_str(handle, STATION_CONFIG_KEY, value, &required_size);

            if (err == ESP_OK)
            {
                err = this->from_json_string(value);

                if (err == ESP_OK)
                    this->loaded = true;
            }
        }
    }

    nvs_close(handle);
    return err;
}

esp_err_t Station_Config::save()
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(STATION_STORAGE_NAME, NVS_READWRITE, &handle);

    if (err == ESP_OK)
    {
        err = nvs_set_str(handle, STATION_CONFIG_KEY, this->to_json_string().c_str());

        if (err == ESP_OK)
            err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t Station_Config::clear()
{
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(STATION_STORAGE_NAME, NVS_READWRITE, &handle);

    if (err == ESP_OK)
    {
        err = nvs_erase_all(handle);

        if (err == ESP_OK)
            err = nvs_commit(handle);

        if (err == ESP_OK)
        {
            this->wifi_ssid.clear();
            this->wifi_pass.clear();
            this->user_email.clear();
            this->user_pass.clear();
            this->api_url.clear();

            this->loaded = false;
        }
    }

    nvs_close(handle);
    return err;
}

String Station_Config::to_json_string()
{
    JSONVar json;

    json["wifi_ssid"] = this->wifi_ssid.c_str();
    json["wifi_pass"] = this->wifi_pass.c_str();
    json["api_url"] = this->api_url.c_str();
    json["user_email"] = this->user_email.c_str();
    json["user_pass"] = this->user_pass.c_str();

    String json_string = JSON.stringify(json);
    json_string.replace(" ", "%20");

    return json_string;
}

esp_err_t Station_Config::from_json_string(const char *json_string)
{
    esp_err_t err = ESP_OK;

    String temp_str(json_string);
    temp_str.replace("%20", " ");

    JSONVar json = JSON.parse(temp_str);

    if (JSON.typeof(json) == "undefined")
        err = ESP_FAIL;
    else
    {
        if (json.hasOwnProperty("wifi_ssid"))
            this->wifi_ssid = (const char *)json["wifi_ssid"];
        if (json.hasOwnProperty("wifi_pass"))
            this->wifi_pass = (const char *)json["wifi_pass"];
        if (json.hasOwnProperty("api_url"))
            this->api_url = (const char *)json["api_url"];
        if (json.hasOwnProperty("user_email"))
            this->user_email = (const char *)json["user_email"];
        if (json.hasOwnProperty("user_pass"))
            this->user_pass = (const char *)json["user_pass"];
    }

    return err;
}
