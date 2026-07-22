#include <LittleFS.h>

struct MqttSettings
{
  bool enabled;
  char server[64];
  uint16_t port;
  char user[40];
  char password[64];
  char topicRoot[96];
  char promNamespace[32];
  uint16_t mqttRefreshSeconds;
  char ntpServer[64];
  bool lokiEnabled;
  char lokiUri[128];
  char lokiDataType[32];
  char lokiServiceName[48];
  bool promFreeHeap;
};

MqttSettings mqttSettings;

// Copy a setting safely.
static void copySetting(char *out, size_t size, const String &value)
{
  String clean = value;
  clean.trim();
  strlcpy(out, clean.c_str(), size);
}

// Restore defaults.
static void settingsDefaults()
{
  mqttSettings.enabled = MQTT_ENABLED_DEFAULT != 0;
  copySetting(mqttSettings.server, sizeof(mqttSettings.server), MQTT_SERVER_DEFAULT);
  mqttSettings.port = MQTT_PORT_DEFAULT;
  copySetting(mqttSettings.user, sizeof(mqttSettings.user), MQTT_USER_DEFAULT);
  copySetting(mqttSettings.password, sizeof(mqttSettings.password), MQTT_PASSWORD_DEFAULT);
  copySetting(mqttSettings.topicRoot, sizeof(mqttSettings.topicRoot), MQTT_TOPIC_ROOT_DEFAULT);
  copySetting(mqttSettings.promNamespace, sizeof(mqttSettings.promNamespace), "devicemon");
  mqttSettings.mqttRefreshSeconds = 10;
  copySetting(mqttSettings.ntpServer, sizeof(mqttSettings.ntpServer), NTP_SERVER_DEFAULT);
  mqttSettings.lokiEnabled = LOKI_ENABLED_DEFAULT != 0;
  copySetting(mqttSettings.lokiUri, sizeof(mqttSettings.lokiUri), LOKI_URI_DEFAULT);
  copySetting(mqttSettings.lokiDataType, sizeof(mqttSettings.lokiDataType), LOKI_DATA_TYPE_DEFAULT);
  copySetting(mqttSettings.lokiServiceName, sizeof(mqttSettings.lokiServiceName), LOKI_SERVICE_NAME_DEFAULT);
  mqttSettings.promFreeHeap = PROM_FREE_HEAP_DEFAULT != 0;
}

// Mount persistent storage.
static bool mountSettingsFs()
{
  return LittleFS.begin() || (LittleFS.format() && LittleFS.begin());
}

// Read settings from a file.
static bool readSettings(const char *path)
{
  File file = LittleFS.open(path, "r");
  if (!file)
    return false;
  mqttSettings.enabled = file.readStringUntil('\n').toInt() != 0;
  copySetting(mqttSettings.server, sizeof(mqttSettings.server), file.readStringUntil('\n'));
  mqttSettings.port = file.readStringUntil('\n').toInt();
  copySetting(mqttSettings.user, sizeof(mqttSettings.user), file.readStringUntil('\n'));
  copySetting(mqttSettings.password, sizeof(mqttSettings.password), file.readStringUntil('\n'));
  copySetting(mqttSettings.topicRoot, sizeof(mqttSettings.topicRoot), file.readStringUntil('\n'));
  String promNamespace = file.readStringUntil('\n');
  if (promNamespace.length())
    copySetting(mqttSettings.promNamespace, sizeof(mqttSettings.promNamespace), promNamespace);
  int mqttRefresh = file.readStringUntil('\n').toInt();
  if (mqttRefresh > 0) mqttSettings.mqttRefreshSeconds = mqttRefresh;
  String ntpServer = file.readStringUntil('\n');
  if (ntpServer.length()) copySetting(mqttSettings.ntpServer, sizeof(mqttSettings.ntpServer), ntpServer);
  String lokiEnabled = file.readStringUntil('\n');
  if (lokiEnabled.length()) mqttSettings.lokiEnabled = lokiEnabled.toInt() != 0;
  String lokiUri = file.readStringUntil('\n');
  if (lokiUri.length()) copySetting(mqttSettings.lokiUri, sizeof(mqttSettings.lokiUri), lokiUri);
  String lokiDataType = file.readStringUntil('\n');
  if (lokiDataType.length()) copySetting(mqttSettings.lokiDataType, sizeof(mqttSettings.lokiDataType), lokiDataType);
  String lokiServiceName = file.readStringUntil('\n');
  if (lokiServiceName.length()) copySetting(mqttSettings.lokiServiceName, sizeof(mqttSettings.lokiServiceName), lokiServiceName);
  String promFreeHeap = file.readStringUntil('\n');
  if (promFreeHeap.length()) mqttSettings.promFreeHeap = promFreeHeap.toInt() != 0;
  file.close();
  return true;
}

// Save settings to flash.
bool saveSettings()
{
  File file = LittleFS.open("/settings.cfg", "w");
  if (!file)
    return false;
  file.println(mqttSettings.enabled ? 1 : 0);
  file.println(mqttSettings.server);
  file.println(mqttSettings.port);
  file.println(mqttSettings.user);
  file.println(mqttSettings.password);
  file.println(mqttSettings.topicRoot);
  file.println(mqttSettings.promNamespace);
  file.println(mqttSettings.mqttRefreshSeconds);
  file.println(mqttSettings.ntpServer);
  file.println(mqttSettings.lokiEnabled ? 1 : 0);
  file.println(mqttSettings.lokiUri);
  file.println(mqttSettings.lokiDataType);
  file.println(mqttSettings.lokiServiceName);
  file.println(mqttSettings.promFreeHeap ? 1 : 0);
  file.close();
  return true;
}

// Read the newest forwarded log fingerprint.
bool loadLoggerFingerprint(uint32_t& fingerprint)
{
  File file = LittleFS.open("/logger.idx", "r");
  if(!file) return false;
  fingerprint = strtoul(file.readStringUntil('\n').c_str(), NULL, 16);
  file.close();
  return true;
}

// Save the newest forwarded log fingerprint.
bool saveLoggerFingerprint(uint32_t fingerprint)
{
  File file = LittleFS.open("/logger.idx", "w");
  if(!file) return false;
  file.printf("%08lx\n", (unsigned long)fingerprint);
  file.close();
  return true;
}

// Load settings
void loadSettings()
{
  settingsDefaults();
  if (!mountSettingsFs())
    return;
  if (readSettings("/settings.cfg"))
    return;
}
