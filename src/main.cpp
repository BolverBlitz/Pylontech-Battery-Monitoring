#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <Updater.h>
#include <SimpleTimer.h>
#include <TimeLib.h> //https://github.com/PaulStoffregen/Time
#include <ntp_time.h>
#include <circular_log.h>
#include <PubSubClient.h>

#define MAX_PYLON_BATTERIES 8
#define MAX_BATTERY_METRIC_ROWS (MAX_PYLON_BATTERIES * 16)
#define MAX_STAT_RANGE_METRICS (MAX_PYLON_BATTERIES * 16)
#define METRICS_COMMAND_WAIT_LOOPS 400
#define METRICS_SEND_BUFFER_SIZE 1024
#define STAT_REFRESH_INTERVAL_MS 3600000UL
#define NTP_RESYNC_INTERVAL_MS 21600000UL
#define LOKI_REFRESH_INTERVAL_MS 300000UL
#define PROM_FREE_HEAP_DEFAULT 1
#ifndef NTP_SERVER_DEFAULT
#define NTP_SERVER_DEFAULT "pool.ntp.org"
#endif
#ifndef LOKI_ENABLED_DEFAULT
#define LOKI_ENABLED_DEFAULT 0
#endif
#ifndef LOKI_URI_DEFAULT
#define LOKI_URI_DEFAULT "http://192.168.1.13:3100/loki/api/v1/push"
#endif
#ifndef LOKI_DATA_TYPE_DEFAULT
#define LOKI_DATA_TYPE_DEFAULT "iot"
#endif
#ifndef LOKI_SERVICE_NAME_DEFAULT
#define LOKI_SERVICE_NAME_DEFAULT "pylontech-battery"
#endif
#define ABS_DIFF(a, b) (a > b ? a-b : b-a)
#define DECL_FIND_OFFSET_OR_FAIL(var, str, key) int var = findOffset((str), (key)); if ((var) == -1) { Log(key " not found"); return false; }

WiFiClient espClient;
PubSubClient mqttClient(espClient);

char g_szRecvBuff[7000];

ESP8266WebServer server(80);
SimpleTimer timer;
circular_log<7000> g_log;
bool ntpTimeReceived = false;
int g_baudRate = 0;

void handleLog();
void handleReq();
void handleJsonOut();
void handleRoot();
void handleSyncTime();
void handleMetrics();
void handleMqttPage();
void handleMqttSave();
void handleUpdateFinished();
void handleUpdateUpload();
void syncTime();
void wakeUpConsole();
unsigned long os_getCurrentTimeSec();
bool parsePwrResponse(const char* pStr);
bool sendCommandAndReadSerialResponse(const char* command, bool wakeRetry = true, int waitLoops = 150);
void prepareJsonOutput(char* pBuff, int buffSize);
void mqttLoop();
void mqttSetup();
void loggerLoop();
void handleLoggerStatus();
void handleLoggerRun();

void Log(const char* msg)
{
  g_log.Log(msg);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT); 
  digitalWrite(LED_BUILTIN, HIGH);//high is off
  
  // put your setup code here, to run once:
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.hostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  for(int ix=0; ix<10; ix++)
  {
    if(WiFi.status() == WL_CONNECTED)
    {
      break;
    }

    delay(1000);
  }

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();
  server.on("/", handleRoot);
  server.on("/log", handleLog);
  server.on("/req", handleReq);
  server.on("/jsonOut", handleJsonOut);
  server.on("/metrics", HTTP_GET, handleMetrics);
  server.on("/sync-time", HTTP_GET, handleSyncTime);
  server.on("/settings", HTTP_GET, handleMqttPage);
  server.on("/settings", HTTP_POST, handleMqttSave);
  server.on("/logger-status", HTTP_GET, handleLoggerStatus);
  server.on("/logger-run", HTTP_POST, handleLoggerRun);
  server.on("/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload);
  server.on("/reboot", [](){
    ESP.restart();
  });
  
  server.begin(); 
  
  mqttSetup();
  syncTime();
  wakeUpConsole();

  Log("Boot event");
}

void handleLog()
{
  server.send(200, "text/html", g_log.c_str());
}

#include "settings.cpp"
#include "communication.cpp"
#include "ota.cpp"
#include "web.cpp"
#include "mqtt.cpp"
#include "logger.cpp"

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  timer.run();
  mqttLoop();
  loggerLoop();

  //if there are bytes availbe on serial here - it's unexpected
  //when we send a command to battery, we read whole response
  //if we get anything here anyways - we will log it
  int bytesAv = Serial.available();
  if(bytesAv > 0)
  {
    if(bytesAv > 63)
    {
      bytesAv = 63;
    }
    
    char buff[64+4] = "RCV:";
    if(Serial.readBytes(buff+4, bytesAv) > 0)
    {
      digitalWrite(LED_BUILTIN, LOW);
      delay(5);
      digitalWrite(LED_BUILTIN, HIGH);//high is off

      Log(buff);
    }
  }
}
