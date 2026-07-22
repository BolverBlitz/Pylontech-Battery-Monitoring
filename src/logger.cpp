struct LokiEvent
{
  uint32_t index;
  char time[20];
  char modId[16];
  char code[16];
  String info;
  time_t utc;
};

static unsigned long loggerLastPoll = 0;
static uint32_t loggerLastFingerprint = 0;
static bool loggerFingerprintLoaded = false;
static unsigned long loggerLastAttempt = 0;
static unsigned long loggerLastDuration = 0;
static int loggerEventsFound = 0;
static int loggerEventsSent = 0;
static int loggerEventsSkipped = 0;
static int loggerHttpStatus = 0;
static char loggerState[20] = "waiting";
static String loggerDetail = "Not run yet";
static bool loggerLastPushSkippable = false;

// Update the visible logger state.
static void setLoggerStatus(const char* state, const char* detail)
{
  strlcpy(loggerState, state, sizeof(loggerState));
  loggerDetail = detail;
}

// Copy one field from a log block.
static bool readLogField(const char* start, const char* end, const char* name, char* out, size_t size)
{
  const char* pos = strstr(start, name);
  if(!pos || pos >= end) return false;
  pos = strchr(pos, ':');
  if(!pos || pos >= end) return false;
  do pos++; while(pos < end && (*pos == ' ' || *pos == '\t'));

  const char* lineEnd = pos;
  while(lineEnd < end && *lineEnd != '\r' && *lineEnd != '\n') lineEnd++;
  while(lineEnd > pos && isspace((unsigned char)lineEnd[-1])) lineEnd--;
  size_t length = lineEnd - pos;
  if(length >= size) return false;
  memcpy(out, pos, length);
  out[length] = '\0';
  return length > 0;
}

// Copy a variable-length log field.
static bool readLogField(const char* start, const char* end, const char* name, String& out)
{
  const char* pos = strstr(start, name);
  if(!pos || pos >= end) return false;
  pos = strchr(pos, ':');
  if(!pos || pos >= end) return false;
  do pos++; while(pos < end && (*pos == ' ' || *pos == '\t'));

  const char* lineEnd = pos;
  while(lineEnd < end && *lineEnd != '\r' && *lineEnd != '\n') lineEnd++;
  while(lineEnd > pos && isspace((unsigned char)lineEnd[-1])) lineEnd--;
  if(lineEnd == pos) return false;
  out = "";
  return out.concat(pos, lineEnd - pos);
}

// Return the last Sunday of a month.
static int lastSunday(int yearValue, int monthValue)
{
  tmElements_t parts;
  parts.Second = 0;
  parts.Minute = 0;
  parts.Hour = 12;
  parts.Day = 31;
  parts.Month = monthValue;
  parts.Year = CalendarYrToTm(yearValue);
  return 31 - (weekday(makeTime(parts)) - 1);
}

// Convert a Berlin wall-clock time to UTC.
static time_t batteryTimeToUtc(const char* value)
{
  int yy, monthValue, dayValue, hourValue, minuteValue, secondValue;
  if(sscanf(value, "%d-%d-%d %d:%d:%d", &yy, &monthValue, &dayValue, &hourValue, &minuteValue, &secondValue) != 6) return 0;
  int yearValue = yy < 100 ? 2000 + yy : yy;
  if(monthValue < 1 || monthValue > 12 || dayValue < 1 || dayValue > 31 || hourValue > 23 || minuteValue > 59 || secondValue > 59) return 0;

  bool summer = monthValue > 3 && monthValue < 10;
  if(monthValue == 3)
  {
    int changeDay = lastSunday(yearValue, 3);
    summer = dayValue > changeDay || (dayValue == changeDay && hourValue >= 2);
  }
  else if(monthValue == 10)
  {
    int changeDay = lastSunday(yearValue, 10);
    summer = dayValue < changeDay || (dayValue == changeDay && hourValue < 3);
  }

  tmElements_t parts;
  parts.Second = secondValue;
  parts.Minute = minuteValue;
  parts.Hour = hourValue;
  parts.Day = dayValue;
  parts.Month = monthValue;
  parts.Year = CalendarYrToTm(yearValue);
  return makeTime(parts) - (summer ? 7200 : 3600);
}

// Parse an event by its newest-first position.
static bool logEventAt(const char* response, int wanted, LokiEvent& event)
{
  const char* cursor = response;
  for(int position = 0; (cursor = strstr(cursor, "Index")) != NULL; position++)
  {
    const char* colon = strchr(cursor, ':');
    if(!colon) break;
    const char* next = strstr(colon + 1, "Index");
    const char* end = next ? next : response + strlen(response);
    if(position == wanted)
    {
      event.index = 0;
      event.time[0] = '\0';
      event.modId[0] = '\0';
      event.code[0] = '\0';
      event.info = "";
      event.utc = 0;
      event.index = strtoul(colon + 1, NULL, 10);
      if(!readLogField(cursor, end, "Time", event.time, sizeof(event.time)) ||
         !readLogField(cursor, end, "ModID", event.modId, sizeof(event.modId)) ||
         !readLogField(cursor, end, "Code", event.code, sizeof(event.code)) ||
         !readLogField(cursor, end, "Info", event.info)) return false;
      event.utc = batteryTimeToUtc(event.time);
      return event.utc != 0;
    }
    cursor = colon + 1;
  }
  return false;
}

// Identify an event across shifting display indexes.
static uint32_t logFingerprint(const LokiEvent& event)
{
  uint32_t hash = 2166136261UL;
  const char* fields[] = {event.time, event.modId, event.code, event.info.c_str()};
  for(unsigned int field = 0; field < sizeof(fields) / sizeof(fields[0]); field++)
  {
    for(const char* p = fields[field]; *p; p++)
    {
      hash ^= (uint8_t)*p;
      hash *= 16777619UL;
    }
    hash ^= 0xff;
    hash *= 16777619UL;
  }
  return hash;
}

// Escape text for JSON.
static String jsonEscape(const char* value)
{
  String escaped;
  escaped.reserve(strlen(value) + 8);
  for(const char* p = value; *p; p++)
  {
    switch(*p)
    {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if((unsigned char)*p >= 0x20) escaped += *p;
    }
  }
  return escaped;
}

// Parse and resolve the Loki target.
static bool getLokiTarget(String& host, uint16_t& port, String& path, IPAddress& address)
{
  String uri = mqttSettings.lokiUri;
  int start = uri.indexOf("://");
  start = start < 0 ? 0 : start + 3;
  int end = uri.indexOf('/', start);
  if(end < 0) end = uri.length();
  String authority = uri.substring(start, end);
  int colon = authority.lastIndexOf(':');
  host = colon > 0 ? authority.substring(0, colon) : authority;
  port = colon > 0 ? authority.substring(colon + 1).toInt() : 80;
  path = end < (int)uri.length() ? uri.substring(end) : "/";
  if(host.isEmpty() || !port || path.isEmpty()) return false;

  if(!address.fromString(host) && WiFi.hostByName(host.c_str(), address, 500) != 1) return false;
  return true;
}

// Push one event to Loki.
static bool pushLokiEvent(const LokiEvent& event)
{
  loggerLastPushSkippable = false;
  if(WiFi.status() != WL_CONNECTED)
  {
    loggerHttpStatus = 0;
    setLoggerStatus("error", "WiFi disconnected");
    return false;
  }

  String line;
  line.reserve(300);
  line = "{\"index\":" + String(event.index) + ",\"time\":\"" + jsonEscape(event.time) + "\",\"mod_id\":\"" + jsonEscape(event.modId) + "\",\"code\":\"" + jsonEscape(event.code) + "\",\"info\":\"" + jsonEscape(event.info.c_str()) + "\"}";

  String payload;
  payload.reserve(line.length() + 300);
  payload = "{\"streams\":[{\"stream\":{\"data_type\":\"" + jsonEscape(mqttSettings.lokiDataType) + "\",\"service_name\":\"" + jsonEscape(mqttSettings.lokiServiceName) + "\",\"device\":\"" + jsonEscape(event.modId) + "\"},\"values\":[[\"" + String((uint32_t)event.utc) + "000000000\",\"" + jsonEscape(line.c_str()) + "\"]]}]}";

  String host, path;
  uint16_t port;
  IPAddress address;
  if(!getLokiTarget(host, port, path, address))
  {
    loggerHttpStatus = 0;
    setLoggerStatus("error", "Invalid Loki URI or DNS failed");
    return false;
  }

  WiFiClient client;
  client.setTimeout(4000);
  if(!client.connect(address, port))
  {
    loggerHttpStatus = -1;
    setLoggerStatus("error", "TCP connection failed");
    return false;
  }

  client.print(F("POST "));
  client.print(path);
  client.print(F(" HTTP/1.1\r\nHost: "));
  client.print(host);
  client.print(':');
  client.print(port);
  client.print(F("\r\nContent-Type: application/json\r\nContent-Length: "));
  client.print(payload.length());
  client.print(F("\r\nConnection: close\r\n\r\n"));
  if(client.print(payload) != payload.length())
  {
    client.stop();
    loggerHttpStatus = -2;
    setLoggerStatus("error", "Failed to write HTTP request");
    return false;
  }

  unsigned long deadline = millis() + 5000UL;
  while(!client.available() && client.connected() && (long)(deadline - millis()) > 0) delay(1);
  if(!client.available())
  {
    client.stop();
    loggerHttpStatus = -3;
    setLoggerStatus("error", "No HTTP response from Loki");
    return false;
  }

  String statusLine = client.readStringUntil('\n');
  int space = statusLine.indexOf(' ');
  int status = space >= 0 ? statusLine.substring(space + 1).toInt() : 0;
  loggerHttpStatus = status;
  String header;
  int contentLength = -1;
  do
  {
    header = client.readStringUntil('\n');
    header.trim();
    if(header.startsWith("Content-Length:")) contentLength = header.substring(15).toInt();
  } while(header.length() && client.connected());
  String response;
  if(contentLength > 0)
  {
    int wanted = contentLength > 300 ? 300 : contentLength;
    response.reserve(wanted + 1);
    unsigned long bodyDeadline = millis() + 4000UL;
    while((int)response.length() < wanted && (long)(bodyDeadline - millis()) > 0)
    {
      while(client.available() && (int)response.length() < wanted) response += (char)client.read();
      if((int)response.length() < wanted) delay(1);
    }
  }
  client.stop();
  if(status >= 200 && status < 300) return true;

  char message[32];
  snprintf(message, sizeof(message), "Loki HTTP %d", status);
  Log(message);
  response.trim();
  String lowerResponse = response;
  lowerResponse.toLowerCase();
  loggerLastPushSkippable = status == 400 &&
    (lowerResponse.indexOf("timestamp too") >= 0 || lowerResponse.indexOf("too far behind") >= 0);
  if(response.length() > 240) response = response.substring(0, 240);
  String detail = "HTTP " + String(status);
  if(response.length()) detail += ": " + response;
  setLoggerStatus("error", detail.c_str());
  return false;
}

// Read and push new battery events.
static void runLogger()
{
  if(!loggerFingerprintLoaded)
  {
    loadLoggerFingerprint(loggerLastFingerprint);
    loggerFingerprintLoaded = true;
  }

  loggerLastAttempt = millis();
  unsigned long started = loggerLastAttempt;
  loggerEventsFound = 0;
  loggerEventsSent = 0;
  loggerEventsSkipped = 0;
  loggerHttpStatus = 0;
  setLoggerStatus("reading", "Reading battery log");
  Log("Loki: poll");

  if(!sendCommandAndReadSerialResponse("log", true, 250))
  {
    Log("Loki log failed");
    setLoggerStatus("error", "Battery log command failed");
    loggerLastDuration = millis() - started;
    return;
  }

  LokiEvent newestEvent;
  if(!logEventAt(g_szRecvBuff, 0, newestEvent))
  {
    Log("Loki parse failed");
    setLoggerStatus("error", "Battery log parse failed");
    loggerLastDuration = millis() - started;
    return;
  }

  int pending = 0;
  LokiEvent event;
  while(logEventAt(g_szRecvBuff, pending, event))
  {
    if(loggerLastFingerprint && logFingerprint(event) == loggerLastFingerprint) break;
    pending++;
  }
  loggerEventsFound = pending;

  char message[40];
  snprintf(message, sizeof(message), "Loki: %d new", pending);
  Log(message);

  for(int position = pending - 1; position >= 0; position--)
  {
    snprintf(message, sizeof(message), "Sending event %d of %d", loggerEventsSent + 1, pending);
    setLoggerStatus("sending", message);
    if(!logEventAt(g_szRecvBuff, position, event))
    {
      setLoggerStatus("error", "Battery log parse failed");
      loggerLastDuration = millis() - started;
      return;
    }
    if(ntpTimeReceived && event.utc > now() + 300)
    {
      loggerEventsSkipped++;
      continue;
    }
    if(!pushLokiEvent(event))
    {
      if(loggerLastPushSkippable)
      {
        loggerEventsSkipped++;
        continue;
      }
      loggerLastDuration = millis() - started;
      return;
    }
    loggerEventsSent++;
    yield();
  }

  loggerLastFingerprint = logFingerprint(newestEvent);
  if(!saveLoggerFingerprint(loggerLastFingerprint))
  {
    Log("Loki cursor save failed");
    setLoggerStatus("error", "Events sent; cursor save failed");
  }
  else
  {
    if(loggerEventsSkipped) snprintf(message, sizeof(message), "Sent %d, skipped %d", loggerEventsSent, loggerEventsSkipped);
    else snprintf(message, sizeof(message), pending ? "Sent %d event(s)" : "No new events", pending);
    setLoggerStatus("ok", message);
    Log(message);
  }
  loggerLastDuration = millis() - started;
}

// Poll every five minutes.
void loggerLoop()
{
  if(!mqttSettings.lokiEnabled) return;
  unsigned long current = millis();
  if(loggerLastPoll && current - loggerLastPoll < LOKI_REFRESH_INTERVAL_MS) return;
  loggerLastPoll = current;
  runLogger();
}

// Return logger diagnostics.
void handleLoggerStatus()
{
  long attemptAgo = loggerLastAttempt ? (long)((millis() - loggerLastAttempt) / 1000UL) : -1;
  long nextRun = -1;
  if(mqttSettings.lokiEnabled)
  {
    unsigned long elapsed = loggerLastPoll ? millis() - loggerLastPoll : LOKI_REFRESH_INTERVAL_MS;
    nextRun = elapsed >= LOKI_REFRESH_INTERVAL_MS ? 0 : (long)((LOKI_REFRESH_INTERVAL_MS - elapsed + 999) / 1000UL);
  }

  String body;
  body.reserve(500);
  body = "{\"enabled\":" + String(mqttSettings.lokiEnabled ? "true" : "false") +
         ",\"state\":\"" + jsonEscape(mqttSettings.lokiEnabled ? loggerState : "disabled") +
         "\",\"detail\":\"" + jsonEscape(mqttSettings.lokiEnabled ? loggerDetail.c_str() : "Enable Loki in settings") +
         "\",\"last_attempt_ago_s\":" + String(attemptAgo) +
         ",\"last_duration_ms\":" + String(loggerLastDuration) +
         ",\"next_run_s\":" + String(nextRun) +
         ",\"events_found\":" + String(loggerEventsFound) +
         ",\"events_sent\":" + String(loggerEventsSent) +
         ",\"events_skipped\":" + String(loggerEventsSkipped) +
         ",\"http_status\":" + String(loggerHttpStatus) +
         ",\"free_heap\":" + String(ESP.getFreeHeap()) +
         ",\"rssi\":" + String(WiFi.RSSI()) +
         ",\"local_ip\":\"" + WiFi.localIP().toString() +
         "\",\"gateway\":\"" + WiFi.gatewayIP().toString() +
         "\",\"uri\":\"" + jsonEscape(mqttSettings.lokiUri) + "\"}";
  server.send(200, "application/json", body);
}

// Run the logger on demand.
void handleLoggerRun()
{
  if(!mqttSettings.lokiEnabled)
  {
    server.send(409, "text/plain", "Loki is disabled");
    return;
  }
  loggerLastPoll = millis();
  runLogger();
  handleLoggerStatus();
}
