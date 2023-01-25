// Helpers functions declarations

#include <WiFi.h>
#include <HTTPClient.h>
#include <WString.h>

String AuthenticateUser(WiFiClient *client, HTTPClient *http, String email, String password);