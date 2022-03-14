
#include "esp_http_server.h"
#include <WiFi.h>
#include <Preferences.h>
#include "BluetoothSerial.h"
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_Sensor.h>
#include "timer.h"
#include <QMC5883LCompass.h>

#define uS_TO_S_FACTOR 1000000  
#define TIME_TO_SLEEP  30  

QMC5883LCompass compass;

Adafruit_BMP280 bmp; 

String ssids_array[50];
String network_string;
String connected_string;

const char* servername = "http://esp32myweatherstation.000webhostapp.com/post-esp-data.php";

String apiKeyValue = "tPmAT5Ab3j7F9";

WiFiServer server(80);

String header;

Timer timer;

const int LM393 = 27;
int counter = 0;

const char* pref_ssid = "";
const char* pref_pass = "";
String client_wifi_ssid;
String client_wifi_password;

const char* bluetooth_name = "ESP32";

long start_wifi_millis;
long wifi_timeout = 10000;
bool bluetooth_disconnect = false;

enum wifi_setup_stages { NONE, SCAN_START, SCAN_COMPLETE, SSID_ENTERED, WAIT_PASS, PASS_ENTERED, WAIT_CONNECT, LOGIN_FAILED };
enum wifi_setup_stages wifi_stage = NONE;



BluetoothSerial SerialBT;
Preferences preferences;

void setup()
{
  Serial.begin(115200);
  
  attachInterrupt(digitalPinToInterrupt(LM393), count, RISING);
  timer.setInterval(60000);
  timer.setCallback(KMH);
  timer.start();

  compass.init();

  
  if (!bmp.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
  
  Serial.println("Booting...");
  preferences.begin("wifi_access", false);

  if (!init_wifi()) { // Connect to Wi-Fi fails
    SerialBT.register_callback(callback);
  } else {
    SerialBT.register_callback(callback_show_ip);
  }

  SerialBT.begin(bluetooth_name);

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  
  esp_deep_sleep_start();  

}

void count() {
  counter++;
}

void KMH()
{
  int rpm = (counter/20);
  float p = 0.0048984; //uproszczony wynik ze wzoru
  float kmh = (p*rpm);
  
  Serial.println(rpm);
  counter = 0;
}

bool init_wifi()
{
  String temp_pref_ssid = preferences.getString("pref_ssid");
  String temp_pref_pass = preferences.getString("pref_pass");
  pref_ssid = temp_pref_ssid.c_str();
  pref_pass = temp_pref_pass.c_str();

  Serial.println(pref_ssid);
  Serial.println(pref_pass);

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);

  start_wifi_millis = millis();
  WiFi.begin(pref_ssid, pref_pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start_wifi_millis > wifi_timeout) {
      WiFi.disconnect(true, true);
      return false;
    }
  }
  return true;
}

void scan_wifi_networks()
{
  WiFi.mode(WIFI_STA);
  // WiFi.scanNetworks will return the number of networks found
  int n =  WiFi.scanNetworks();
  if (n == 0) {
    SerialBT.println("no networks found");
  } else {
    SerialBT.println();
    SerialBT.print(n);
    SerialBT.println(" networks found");
    delay(1000);
    for (int i = 0; i < n; ++i) {
      ssids_array[i + 1] = WiFi.SSID(i);
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(ssids_array[i + 1]);
      network_string = i + 1;
      network_string = network_string + ": " + WiFi.SSID(i) + " (Strength:" + WiFi.RSSI(i) + ")";
      SerialBT.println(network_string);
    }
    wifi_stage = SCAN_COMPLETE;
  }
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    wifi_stage = SCAN_START;
  }

  if (event == ESP_SPP_DATA_IND_EVT && wifi_stage == SCAN_COMPLETE) { // data from phone is SSID
    int client_wifi_ssid_id = SerialBT.readString().toInt();
    client_wifi_ssid = ssids_array[client_wifi_ssid_id];
    wifi_stage = SSID_ENTERED;
  }

  if (event == ESP_SPP_DATA_IND_EVT && wifi_stage == WAIT_PASS) { // data from phone is password
    client_wifi_password = SerialBT.readString();
    client_wifi_password.trim();
    wifi_stage = PASS_ENTERED;
  }

}

void callback_show_ip(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    SerialBT.print("ESP32 IP: ");
    SerialBT.println(WiFi.localIP());
    bluetooth_disconnect = true;
  }
}

static esp_err_t main_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  
}



void disconnect_bluetooth()
{
  delay(1000);
  Serial.println("BT stopping");
  SerialBT.println("Bluetooth disconnecting...");
  delay(1000);
  SerialBT.flush();
  SerialBT.disconnect();
  SerialBT.end();
  Serial.println("BT stopped");
  delay(1000);
  bluetooth_disconnect = false;
}

void loop()
{
  timer.update();

   int a;

  int rpm = (counter/20);
  float p = 0.0048984; //uproszczony wynik ze wzoru
  float kmh = (p*rpm);
  
  compass.read();

  
  a = compass.getAzimuth();
  
  String winddirection;
  
  if ((a > 340)  && (a < 20))
  {

    winddirection = "Północny";
  }


  if ((a > 19) && (a < 79))
  {
   
   winddirection = "Północno Zachodni";
  }

  if ((a > 80) && (a < 125))
  {
   
    winddirection = "Zachodni";
  }

  if ((a > 125) && (a < 159))
  {
    
    winddirection = "Południowo Wschodni";
  }

  if ((a > 160) && (a < 200))
  {
   
    winddirection = "Południowy";
  }

  if ((a > 199) && (a < 249))
  {
    
    winddirection = "Południowo Wschodni";
  }

  if ((a > 250) && (a < 290))
  {
 
    
    winddirection = "Wschodni";
  }

  if ((a > 289) && (a < 339))
  {


    winddirection = "Północno Wschodni";
  }

  
  
  if (bluetooth_disconnect)
  {
    disconnect_bluetooth();
  }

  switch (wifi_stage)
  {
    case SCAN_START:
      SerialBT.println("Scanning Wi-Fi networks");
      Serial.println("Scanning Wi-Fi networks");
      scan_wifi_networks();
      SerialBT.println("Please enter the number for your Wi-Fi");
      wifi_stage = SCAN_COMPLETE;
      break;

    case SSID_ENTERED:
      SerialBT.println("Please enter your Wi-Fi password");
      Serial.println("Please enter your Wi-Fi password");
      wifi_stage = WAIT_PASS;
      break;

    case PASS_ENTERED:
      SerialBT.println("Please wait for Wi-Fi connection...");
      Serial.println("Please wait for Wi_Fi connection...");
      wifi_stage = WAIT_CONNECT;
      preferences.putString("pref_ssid", client_wifi_ssid);
      preferences.putString("pref_pass", client_wifi_password);
      if (init_wifi()) { 
        connected_string = "ESP32 IP: ";
        connected_string = connected_string + WiFi.localIP().toString();
        SerialBT.println(connected_string);
        Serial.println(connected_string);
        bluetooth_disconnect = true;
      } else { 
        wifi_stage = LOGIN_FAILED;
      }
      break;

    case LOGIN_FAILED:
      SerialBT.println("Wi-Fi connection failed");
      Serial.println("Wi-Fi connection failed");
      delay(2000);
      wifi_stage = SCAN_START;
      break;
  }

WiFiClient client = server.available();   

  if(WiFi.status()== WL_CONNECTED){
    WiFiClient client;
    HTTPClient http;
    
    http.begin(client, servername);
    
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    delay(61000);  
    
    String httpRequestData = "api_key=" + apiKeyValue + "&value1=" + String(bmp.readTemperature())
                          + "&value2=" + String(bmp.readPressure()/100.0F) + "&value3=" + String(kmh) + "&value4=" + String(winddirection) + "";
    Serial.print("httpRequestData: ");
    Serial.println(httpRequestData);
    
    int httpResponseCode = http.POST(httpRequestData);
     

    if (httpResponseCode>0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
    }
    else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  }
   else {
    Serial.println("WiFi Disconnected");
  }
  
  delay(1000);  
}
