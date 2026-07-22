// Build a full MQTT topic.
static String mqttTopic(const char* suffix)
{
  String root = mqttSettings.topicRoot;
  while(root.endsWith("/")) root.remove(root.length() - 1);
  return root + "/" + MQTT_VALUE_PREFIX + suffix;
}

// Initialize MQTT.
void mqttSetup()
{
  loadSettings();
  mqttClient.setServer(mqttSettings.server, mqttSettings.port);
  mqttClient.setSocketTimeout(1);
}

// Show device settings.
void handleMqttPage()
{
  String html;
  html.reserve(5200);
  html = "<!doctype html><html><meta name='viewport' content='width=device-width'><style>body{font:16px system-ui;background:#f2f4f7;color:#17202a;margin:0}.card{max-width:680px;margin:32px auto;background:white;padding:26px;border-radius:12px;box-shadow:0 3px 18px #0002}h2{margin-top:0}h3{margin:24px 0 8px;color:#345}table{width:100%;border-collapse:collapse}td{padding:9px 4px;border-bottom:1px solid #e5e8eb}td:first-child{width:34%;font-weight:600}input:not([type=checkbox]){box-sizing:border-box;width:100%;padding:9px;border:1px solid #b8c0c8;border-radius:6px;font:inherit}input:focus{outline:2px solid #69c}button{margin-top:22px;background:#1769aa;color:white;border:0;border-radius:7px;padding:11px 24px;font:600 16px system-ui;cursor:pointer}.actions{display:flex;gap:10px;flex-wrap:wrap}.back{float:right;color:#1769aa;text-decoration:none}@media(max-width:720px){.card{margin:0;border-radius:0;padding:20px}td{display:block;width:auto!important;border:0}td:first-child{padding-bottom:2px}}</style><body><main class='card'><a class='back' href='/'>Back</a><h2>Settings</h2><form method='post'><h3>MQTT</h3><table>";
  html += "<tr><td>Enabled</td><td><label><input type='checkbox' name='enabled' value='1'" + String(mqttSettings.enabled ? " checked" : "") + "> Publish battery data</label></td></tr>";
  html += "<tr><td>Server</td><td><input name='server' value='" + String(mqttSettings.server) + "'></td></tr>";
  html += "<tr><td>Port</td><td><input name='port' type='number' min='1' max='65535' value='" + String(mqttSettings.port) + "'></td></tr>";
  html += "<tr><td>User</td><td><input name='user' value='" + String(mqttSettings.user) + "'></td></tr>";
  html += "<tr><td>Password</td><td><input name='password' type='password' value='" + String(mqttSettings.password) + "'></td></tr>";
  html += "<tr><td>Topic root</td><td><input name='topic' value='" + String(mqttSettings.topicRoot) + "'></td></tr>";
  html += "<tr><td>Refresh interval</td><td><input name='mqttRefresh' type='number' min='1' max='3600' value='" + String(mqttSettings.mqttRefreshSeconds) + "'> seconds</td></tr></table>";
  html += "<h3>Prometheus</h3><table><tr><td>Namespace</td><td><input name='namespace' value='" + String(mqttSettings.promNamespace) + "'></td></tr>";
  html += "<tr><td>Free heap</td><td><label><input type='checkbox' name='promFreeHeap' value='1'" + String(mqttSettings.promFreeHeap ? " checked" : "") + "> Include ESP heap metric</label></td></tr></table>";
  html += "<h3>Time</h3><table><tr><td>NTP server</td><td><input name='ntpServer' value='" + String(mqttSettings.ntpServer) + "'></td></tr></table>";
  html += "<h3>Loki</h3><table>";
  html += "<tr><td>Enabled</td><td><label><input type='checkbox' name='lokiEnabled' value='1'" + String(mqttSettings.lokiEnabled ? " checked" : "") + "> Push battery logs</label></td></tr>";
  html += "<tr><td>Loki URI</td><td><input name='lokiUri' value='" + String(mqttSettings.lokiUri) + "'></td></tr>";
  html += "<tr><td>Data type</td><td><input name='lokiDataType' value='" + String(mqttSettings.lokiDataType) + "'></td></tr>";
  html += "<tr><td>Service name</td><td><input name='lokiServiceName' value='" + String(mqttSettings.lokiServiceName) + "'></td></tr>";
  html += "<tr><td>Device</td><td>Log entry ModID</td></tr></table>";
  html += F("<h3>Loki status</h3><table><tr><td>State</td><td id='lsState'>Loading...</td></tr><tr><td>Detail</td><td id='lsDetail'>-</td></tr><tr><td>Last attempt</td><td id='lsAttempt'>-</td></tr><tr><td>Next run</td><td id='lsNext'>-</td></tr><tr><td>Events</td><td id='lsEvents'>-</td></tr><tr><td>HTTP status</td><td id='lsHttp'>-</td></tr><tr><td>ESP network</td><td id='lsNetwork'>-</td></tr></table>");
  html += F("<div class='actions'><button type='button' id='loggerRun'>Run logger now</button><button type='submit'>Save settings</button></div></form></main>");
  html += F("<script>const q=id=>document.getElementById(id),run=q('loggerRun');async function loggerStatus(){try{let r=await fetch('/logger-status',{cache:'no-store'}),s=await r.json();q('lsState').textContent=s.state;q('lsDetail').textContent=s.detail;q('lsAttempt').textContent=s.last_attempt_ago_s<0?'Never':s.last_attempt_ago_s+'s ago';q('lsNext').textContent=s.next_run_s<0?'Disabled':s.next_run_s+'s';q('lsEvents').textContent=s.events_sent+' sent, '+s.events_skipped+' skipped / '+s.events_found;q('lsHttp').textContent=s.http_status||'-';q('lsNetwork').textContent=s.local_ip+' via '+s.gateway+' | '+s.free_heap+' B heap | '+s.rssi+' dBm'}catch(e){q('lsState').textContent='Status unavailable'}}run.onclick=async()=>{run.disabled=true;q('lsState').textContent='Running...';try{await fetch('/logger-run',{method:'POST'})}catch(e){}run.disabled=false;loggerStatus()};loggerStatus();setInterval(loggerStatus,3000)</script></body></html>");
  server.send(200, "text/html", html);
}

// Validate and save settings.
void handleMqttSave()
{
  int port = server.arg("port").toInt();
  int mqttRefresh = server.arg("mqttRefresh").toInt();
  bool enabled = server.hasArg("enabled");
  bool lokiEnabled = server.hasArg("lokiEnabled");
  bool promFreeHeap = server.hasArg("promFreeHeap");
  bool invalidLoki = lokiEnabled && (!server.arg("lokiUri").startsWith("http://") || server.arg("lokiDataType").isEmpty() || server.arg("lokiServiceName").isEmpty());
  if((enabled && server.arg("server").isEmpty()) || port < 1 || port > 65535 || server.arg("namespace").isEmpty() || server.arg("ntpServer").isEmpty() || mqttRefresh < 1 || mqttRefresh > 3600 || invalidLoki)
  {
    server.send(400, "text/plain", "Invalid server or port");
    return;
  }
  mqttSettings.enabled = enabled;
  copySetting(mqttSettings.server, sizeof(mqttSettings.server), server.arg("server"));
  mqttSettings.port = port;
  copySetting(mqttSettings.user, sizeof(mqttSettings.user), server.arg("user"));
  copySetting(mqttSettings.password, sizeof(mqttSettings.password), server.arg("password"));
  copySetting(mqttSettings.topicRoot, sizeof(mqttSettings.topicRoot), server.arg("topic"));
  copySetting(mqttSettings.promNamespace, sizeof(mqttSettings.promNamespace), server.arg("namespace"));
  copySetting(mqttSettings.ntpServer, sizeof(mqttSettings.ntpServer), server.arg("ntpServer"));
  mqttSettings.mqttRefreshSeconds = mqttRefresh;
  mqttSettings.lokiEnabled = lokiEnabled;
  mqttSettings.promFreeHeap = promFreeHeap;
  copySetting(mqttSettings.lokiUri, sizeof(mqttSettings.lokiUri), server.arg("lokiUri"));
  copySetting(mqttSettings.lokiDataType, sizeof(mqttSettings.lokiDataType), server.arg("lokiDataType"));
  copySetting(mqttSettings.lokiServiceName, sizeof(mqttSettings.lokiServiceName), server.arg("lokiServiceName"));
  if(!saveSettings())
  {
    server.send(500, "text/plain", "Save failed");
    return;
  }
  mqttClient.disconnect();
  mqttClient.setServer(mqttSettings.server, mqttSettings.port);
  syncTime();
  server.sendHeader("Location", "/settings");
  server.send(303);
}

// Publish a changed float.
static void mqttPublish(float value, float oldValue, float minDiff, const char* suffix, bool force)
{
  if(!force && ABS_DIFF(value, oldValue) <= minDiff) return;
  char text[16];
  snprintf(text, sizeof(text), "%.2f", value);
  mqttClient.publish(mqttTopic(suffix).c_str(), text, false);
}

// Publish a changed integer.
static void mqttPublish(long value, long oldValue, long minDiff, const char* suffix, bool force)
{
  if(!force && ABS_DIFF(value, oldValue) <= minDiff) return;
  char text[16];
  snprintf(text, sizeof(text), "%ld", value);
  mqttClient.publish(mqttTopic(suffix).c_str(), text, false);
}

// Publish a changed string.
static void mqttPublish(const char* value, const char* oldValue, const char* suffix, bool force)
{
  if(force || strcmp(value, oldValue) != 0) mqttClient.publish(mqttTopic(suffix).c_str(), value, false);
}

// Publish battery data.
static void pushBatteryDataToMqtt(const batteryStack& old, bool force)
{
  mqttPublish((float)g_stack.soc, (float)old.soc, 0, "soc", force);
  mqttPublish(g_stack.temp / 1000.0f, old.temp / 1000.0f, 0, "temp", force);
  mqttPublish(g_stack.getPowerDC(), old.getPowerDC(), 10, "powerDC", force);
  mqttPublish(g_stack.getEstPowerAc(), old.getEstPowerAc(), 10, "estPowerAC", force);
  mqttPublish((long)g_stack.batteryCount, (long)old.batteryCount, 0, "battery_count", force);
  mqttPublish(g_stack.baseState, old.baseState, "base_state", force);
  mqttPublish((long)g_stack.isNormal(), (long)old.isNormal(), 0, "is_normal", force);
}

static unsigned long mqttLastPoll = 0;

// Maintain MQTT and publish data.
void mqttLoop()
{
  if(!mqttSettings.enabled)
  {
    if(mqttClient.connected()) mqttClient.disconnect();
    return;
  }

  static bool connectAttempted = false;
  static unsigned long lastConnect = 0;
  if(mqttSettings.enabled && !mqttClient.connected() && (!connectAttempted || millis() - lastConnect > 60000))
  {
    IPAddress address;
    bool resolved = address.fromString(mqttSettings.server) || WiFi.hostByName(mqttSettings.server, address, 250) == 1;
    bool transport = resolved && espClient.connect(address, mqttSettings.port);
    String availability = mqttTopic("availability");
    if(transport && mqttClient.connect(WIFI_HOSTNAME, mqttSettings.user, mqttSettings.password, availability.c_str(), 1, true, "offline"))
    {
      Log("MQTT connected");
      mqttClient.publish(availability.c_str(), "online", true);
    }
    else Log("MQTT connection failed");
    connectAttempted = true;
    lastConnect = millis();
  }

  if(mqttClient.connected() && (!mqttLastPoll || millis() - mqttLastPoll >= mqttSettings.mqttRefreshSeconds * 1000UL))
  {
    mqttLastPoll = millis();
    if(sendCommandAndReadSerialResponse("pwr") && parsePwrResponse(g_szRecvBuff))
    {
      static batteryStack old;
      static unsigned int pollCount = 0;
      pushBatteryDataToMqtt(old, pollCount++ % 20 == 0);
      memcpy(&old, &g_stack, sizeof(old));
    }
  }
  if(mqttClient.connected()) mqttClient.loop();
}
