void handleReq()
{
  bool respOK;
  if(server.hasArg("code") == false)
  {
    respOK = sendCommandAndReadSerialResponse("");
  }
  else
  {
    respOK = sendCommandAndReadSerialResponse(server.arg("code").c_str());
  }

  if(respOK)
  {
    server.send(200, "text/plain", g_szRecvBuff);
  }
  else
  {
    server.send(500, "text/plain", "????");
  }
}

void handleJsonOut()
{
  if(sendCommandAndReadSerialResponse("pwr") == false)
  {
    server.send(500, "text/plain", "Failed to get response to 'pwr' command");
    return;
  }

  parsePwrResponse(g_szRecvBuff);
  prepareJsonOutput(g_szRecvBuff, sizeof(g_szRecvBuff));
  server.send(200, "application/json", g_szRecvBuff);
}

static time_t berlinTime(time_t utc, const char*& zone)
{
  tmElements_t parts;
  breakTime(utc, parts);
  tmElements_t boundary = {};
  boundary.Year = parts.Year;
  boundary.Month = 3;
  boundary.Day = 31;
  boundary.Hour = 1;
  time_t march = makeTime(boundary);
  boundary.Day = 31 - (weekday(march) - 1);
  time_t summerStart = makeTime(boundary);
  boundary.Month = 10;
  boundary.Day = 31;
  time_t october = makeTime(boundary);
  boundary.Day = 31 - (weekday(october) - 1);
  time_t summerEnd = makeTime(boundary);
  bool summer = utc >= summerStart && utc < summerEnd;
  zone = summer ? "MESZ" : "MEZ";
  return utc + (summer ? 7200 : 3600);
}

static bool readBatteryTime(char* output, size_t outputSize)
{
  if(!sendCommandAndReadSerialResponse("time")) return false;
  const char* value = strstr(g_szRecvBuff, "RTC ");
  if(!value) return false;
  value += 4;
  const char* end = strpbrk(value, "\r\n");
  size_t length = end ? (size_t)(end - value) : strlen(value);
  length = min(length, outputSize - 1);
  memcpy(output, value, length);
  output[length] = 0;
  return true;
}

void handleSyncTime()
{
  const char* names[] = {"epoch", "year", "month", "day", "hour", "minute", "second"};
  for(const char* name : names)
  {
    if(!server.hasArg(name))
    {
      server.send(400, "text/plain", "Missing time value.");
      return;
    }
  }

  unsigned long epoch = strtoul(server.arg("epoch").c_str(), NULL, 10);
  int yearValue = server.arg("year").toInt();
  int monthValue = server.arg("month").toInt();
  int dayValue = server.arg("day").toInt();
  int hourValue = server.arg("hour").toInt();
  int minuteValue = server.arg("minute").toInt();
  int secondValue = server.arg("second").toInt();
  if(epoch < 946684800UL || yearValue < 2000 || yearValue > 2099 || monthValue < 1 || monthValue > 12 || dayValue < 1 || dayValue > 31 || hourValue < 0 || hourValue > 23 || minuteValue < 0 || minuteValue > 59 || secondValue < 0 || secondValue > 59)
  {
    server.send(400, "text/plain", "Invalid time value.");
    return;
  }

  char command[64];
  snprintf(command, sizeof(command), "time %02d %02d %02d %02d %02d %02d", yearValue % 100, monthValue, dayValue, hourValue, minuteValue, secondValue);
  if(!sendCommandAndReadSerialResponse(command) || strstr(g_szRecvBuff, "Command completed successfully") == NULL)
  {
    server.send(502, "text/plain", "Battery time sync failed.");
    return;
  }

  setTime((time_t)epoch);
  ntpTimeReceived = true;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRoot() {
  unsigned long days = 0, hours = 0, minutes = 0;
  unsigned long val = os_getCurrentTimeSec();
  
  days = val / (3600*24);
  val -= days * (3600*24);
  
  hours = val / 3600;
  val -= hours * 3600;
  
  minutes = val / 60;
  val -= minutes*60;
  
  char batteryTime[24] = "Unavailable";
  readBatteryTime(batteryTime, sizeof(batteryTime));
  const char* zone = "";
  time_t localTime = berlinTime(now(), zone);
  tmElements_t local;
  breakTime(localTime, local);

  static char szTmp[5000] = "";
  snprintf(szTmp, sizeof(szTmp)-1, "<html><b>Garage Battery</b><br>ESP time: %d/%02d/%02d %02d:%02d:%02d (%s, Europe/Berlin)<br>Battery time: %s<br><button type='button' onclick='syncTimes(this)'>Sync ESP + battery to PC time</button> <span id='syncStatus'></span><script>async function syncTimes(b){let d=new Date(),q=new URLSearchParams({epoch:Math.floor(Date.now()/1000),year:d.getFullYear(),month:d.getMonth()+1,day:d.getDate(),hour:d.getHours(),minute:d.getMinutes(),second:d.getSeconds()}),s=document.getElementById('syncStatus');b.disabled=true;s.textContent=' Syncing...';try{let r=await fetch('/sync-time?'+q);if(!r.ok)throw new Error(await r.text());location.reload()}catch(e){s.textContent=' '+e.message;b.disabled=false}}</script><br>Uptime: %02d:%02d:%02d.%02d<br><br>free heap: %u<br>Wifi RSSI: %d<BR>Wifi SSID: %s",
            tmYearToCalendar(local.Year), local.Month, local.Day, local.Hour, local.Minute, local.Second, zone, batteryTime,
            (int)days, (int)hours, (int)minutes, (int)val, 
            ESP.getFreeHeap(), WiFi.RSSI(), WiFi.SSID().c_str());


  strncat(szTmp, "<BR><a href='/log'>Runtime log</a> | <a href='/metrics'>Prometheus metrics</a> | <a href='/settings'>Settings</a><HR>", sizeof(szTmp)-1);
  strncat(szTmp, "<form action='/req' method='get'>Command:<input type='text' name='code'/><input type='submit'></form><a href='/req?code=pwr'>Power</a> | <a href='/req?code=help'>Help</a> | <a href='/req?code=log'>Event Log</a> | <a href='/req?code=time'>Time</a>", sizeof(szTmp)-1);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", szTmp);
  server.sendContent_P(OTA_UPDATE_HTML);
  server.sendContent("</html>");
  server.sendContent("");
}

void prepareJsonOutput(char* pBuff, int buffSize)
{
  memset(pBuff, 0, buffSize);
  snprintf(pBuff, buffSize-1, "{\"soc\": %d, \"temp\": %d, \"currentDC\": %ld, \"avgVoltage\": %ld, \"baseState\": \"%s\", \"batteryCount\": %d, \"powerDC\": %ld, \"estPowerAC\": %ld, \"isNormal\": %s}", g_stack.soc, 
                                                                                                                                                                                                            g_stack.temp, 
                                                                                                                                                                                                            g_stack.currentDC, 
                                                                                                                                                                                                            g_stack.avgVoltage, 
                                                                                                                                                                                                            g_stack.baseState, 
                                                                                                                                                                                                            g_stack.batteryCount, 
                                                                                                                                                                                                            g_stack.getPowerDC(), 
                                                                                                                                                                                                            g_stack.getEstPowerAc(),
                                                                                                                                                                                                            g_stack.isNormal() ? "true" : "false");
}

static int prometheusState(const char* state)
{
  if(strcmp(state, "Charge") == 0) return 0;
  if(strcmp(state, "Dischg") == 0) return 1;
  if(strcmp(state, "Idle") == 0) return 2;
  if(strcmp(state, "Balance") == 0) return 3;
  return -1;
}

struct BatteryMetricRecord
{
  uint8_t unit;
  uint8_t id;
  int32_t volt;
  int32_t curr;
  int32_t temp;
  int32_t coulomb;
  int16_t soc;
  int8_t baseState;
  uint8_t balanceCount;
};

struct StatUnitRecord
{
  bool hasCycles;
  bool hasSoh;
  bool hasDsgCap;
  float cycles;
  float soh;
  float dsgCap;
};

struct StatRangeRecord
{
  uint8_t unit;
  uint8_t family;
  char range[16];
  float value;
};

static BatteryMetricRecord batteryRecords[MAX_BATTERY_METRIC_ROWS];
static StatUnitRecord statUnits[MAX_PYLON_BATTERIES];
static StatRangeRecord statRanges[MAX_STAT_RANGE_METRICS];
static size_t batteryRecordCount;
static size_t statRangeCount;
static bool statCacheValid;
static unsigned long statLastFetch;

class MetricWriter
{
public:
  MetricWriter() : used(0) {}

  void printf(const char* format, ...)
  {
    char line[320];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if(length <= 0) return;
    size_t bytes = min((size_t)length, sizeof(line) - 1);
    if(used + bytes > sizeof(buffer)) flush();
    if(bytes > sizeof(buffer))
    {
      server.sendContent(line, bytes);
      delay(1);
      return;
    }
    memcpy(buffer + used, line, bytes);
    used += bytes;
  }

  void finish()
  {
    flush();
  }

private:
  void flush()
  {
    if(used)
    {
      server.sendContent(buffer, used);
      used = 0;
      delay(1);
    }
  }

  size_t used;
  char buffer[METRICS_SEND_BUFFER_SIZE];
};

static char* trimText(char* text)
{
  while(isspace((unsigned char)*text)) ++text;
  char* end = text + strlen(text);
  while(end > text && isspace((unsigned char)end[-1])) --end;
  *end = 0;
  return text;
}

static bool collectBatteryMetrics(int unit)
{
  char* lineSave = NULL;
  for(char* line = strtok_r(g_szRecvBuff, "\r\n", &lineSave); line; line = strtok_r(NULL, "\r\n", &lineSave))
  {
    char* fields[16];
    int count = 0;
    char* fieldSave = NULL;
    for(char* field = strtok_r(line, " \t", &fieldSave); field && count < 16; field = strtok_r(NULL, " \t", &fieldSave)) fields[count++] = field;
    if(count < 12 || !isdigit((unsigned char)fields[0][0]) || !isdigit((unsigned char)fields[1][0])) continue;
    if(batteryRecordCount >= MAX_BATTERY_METRIC_ROWS) return false;

    BatteryMetricRecord& record = batteryRecords[batteryRecordCount++];
    record.unit = unit;
    record.id = atoi(fields[0]);
    record.volt = atol(fields[1]);
    record.curr = atol(fields[2]);
    record.temp = atol(fields[3]);
    record.baseState = prometheusState(fields[4]);
    record.soc = atoi(fields[8]);
    record.coulomb = atol(fields[9]);
    record.balanceCount = strcmp(fields[11], "Y") == 0 ? 1 : 0;
    if(strcmp(fields[11], "N") != 0 && strcmp(fields[11], "Y") != 0)
      for(const char* p = fields[11]; *p; ++p) if(*p == '1') ++record.balanceCount;
  }
  return true;
}

static void normalizeRange(const char* input, char* output, size_t outputSize)
{
  char clean[24] = "";
  size_t used = 0;
  for(size_t i=0; input[i] && used + 1 < sizeof(clean); ++i)
  {
    char c = tolower((unsigned char)input[i]);
    if(isspace((unsigned char)c) || c == '%') continue;
    if(c == '~' || c == '_') c = '-';
    if(c == 't' && tolower((unsigned char)input[i + 1]) == 'o') { c = '-'; ++i; }
    clean[used++] = c;
  }
  clean[used] = 0;

  char* start = clean;
  while(*start == '-' || *start == ':') ++start;
  size_t length = strlen(start);
  while(length && (start[length - 1] == '-' || start[length - 1] == ':')) start[--length] = 0;
  if(length >= 5 && strcmp(start + length - 5, "above") == 0)
  {
    start[length - 5] = 0;
    strlcpy(output, "gt", outputSize);
    strlcat(output, start, outputSize);
  }
  else if(strncmp(start, "above", 5) == 0)
  {
    strlcpy(output, "gt", outputSize);
    strlcat(output, start + 5, outputSize);
  }
  else if(length && start[length - 1] == '+')
  {
    start[length - 1] = 0;
    strlcpy(output, "gt", outputSize);
    strlcat(output, start, outputSize);
  }
  else strlcpy(output, start, outputSize);
}

static bool storeStatRange(int unit, int family, const char* range, float value)
{
  for(size_t i=0; i<statRangeCount; ++i)
  {
    if(statRanges[i].unit == unit && statRanges[i].family == family && strcmp(statRanges[i].range, range) == 0)
    {
      statRanges[i].value = value;
      return true;
    }
  }
  if(statRangeCount >= MAX_STAT_RANGE_METRICS) return false;
  StatRangeRecord& record = statRanges[statRangeCount++];
  record.unit = unit;
  record.family = family;
  strlcpy(record.range, range, sizeof(record.range));
  record.value = value;
  return true;
}

static bool collectStatMetrics(int unit, bool& parsed)
{
  parsed = false;
  StatUnitRecord& stat = statUnits[unit - 1];
  char* save = NULL;
  for(char* line = strtok_r(g_szRecvBuff, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save))
  {
    char* colon = strchr(line, ':');
    if(!colon) continue;
    *colon = 0;
    char* label = trimText(line);
    char* valueStart = trimText(colon + 1);
    char* valueEnd = NULL;
    float value = strtod(valueStart, &valueEnd);
    if(valueEnd == valueStart) continue;

    if(strcasecmp(label, "Cycle Times") == 0 || strcasecmp(label, "Cycles") == 0) { stat.cycles = value; stat.hasCycles = true; parsed = true; continue; }
    if(strcasecmp(label, "SOH") == 0) { stat.soh = value; stat.hasSoh = true; parsed = true; continue; }
    if(strcasecmp(label, "Dsg Cap") == 0) { stat.dsgCap = value; stat.hasDsgCap = true; parsed = true; continue; }

    size_t labelLength = strlen(label);
    size_t suffixLength = 0;
    if(labelLength >= 5 && strcasecmp(label + labelLength - 5, " Secs") == 0) suffixLength = 5;
    else if(labelLength >= 4 && strcasecmp(label + labelLength - 4, " Sec") == 0) suffixLength = 4;
    if(!suffixLength) continue;
    label[labelLength - suffixLength] = 0;

    int family = -1;
    const char* rawRange = NULL;
    if(strncasecmp(label, "ChgCurr ", 8) == 0) { family = 0; rawRange = label + 8; }
    else if(strncasecmp(label, "DsgCurr ", 8) == 0) { family = 1; rawRange = label + 8; }
    else if(strncasecmp(label, "Soc ", 4) == 0) { family = 2; rawRange = label + 4; }
    if(family < 0) continue;

    char range[16];
    normalizeRange(rawRange, range, sizeof(range));
    if(family == 2 && strcmp(range, "0-20") != 0 && strcmp(range, "20-60") != 0 && strcmp(range, "gt60") != 0) continue;
    if(!storeStatRange(unit, family, range, value)) return false;
    parsed = true;
  }
  return true;
}

static void emitHeader(MetricWriter& output, const char* subsystem, const char* name, const char* help)
{
  output.printf("# HELP %s_%s_%s %s\n", mqttSettings.promNamespace, subsystem, name, help);
  output.printf("# TYPE %s_%s_%s gauge\n", mqttSettings.promNamespace, subsystem, name);
}

static void emitPowerFamily(MetricWriter& output, int family, const char* name, const char* help)
{
  bool hasValues = false;
  for(int i=0; i<MAX_PYLON_BATTERIES; ++i)
    if(g_stack.batts[i].isPresent && (family != 5 || g_stack.batts[i].mosTempr[0])) { hasValues = true; break; }
  if(!hasValues) return;
  emitHeader(output, "power", name, help);
  for(int i=0; i<MAX_PYLON_BATTERIES; ++i)
  {
    const pylonBattery& battery = g_stack.batts[i];
    if(!battery.isPresent || (family == 5 && !battery.mosTempr[0])) continue;
    double value = family == 0 ? battery.voltage :
                   family == 1 ? battery.current :
                   family == 2 ? battery.tempr / 1000.0 :
                   family == 3 ? prometheusState(battery.baseState) :
                   family == 4 ? battery.soc : atof(battery.mosTempr) / 10.0;
    output.printf("%s_power_%s{id=\"%d\"} %.6f\n", mqttSettings.promNamespace, name, i + 1, value);
  }
}

static void emitBatteryFamily(MetricWriter& output, int family, const char* name, const char* help)
{
  if(!batteryRecordCount) return;
  emitHeader(output, "battery", name, help);
  for(size_t i=0; i<batteryRecordCount; ++i)
  {
    const BatteryMetricRecord& record = batteryRecords[i];
    double value = family == 0 ? record.volt :
                   family == 1 ? record.curr :
                   family == 2 ? record.temp / 1000.0 :
                   family == 3 ? record.baseState :
                   family == 4 ? record.soc :
                   family == 5 ? record.coulomb : record.balanceCount;
    output.printf("%s_battery_%s{unit=\"bat%d\",id=\"%d\"} %.6f\n", mqttSettings.promNamespace, name, record.unit, record.id, value);
  }
}

static void emitStatUnitFamily(MetricWriter& output, int family, const char* name, const char* help, int units)
{
  bool hasValues = false;
  for(int unit=0; unit<units; ++unit)
    if((family == 0 && statUnits[unit].hasCycles) || (family == 1 && statUnits[unit].hasSoh) || (family == 2 && statUnits[unit].hasDsgCap)) { hasValues = true; break; }
  if(!hasValues) return;
  emitHeader(output, "battery_stat", name, help);
  for(int unit=0; unit<units; ++unit)
  {
    const StatUnitRecord& stat = statUnits[unit];
    bool present = family == 0 ? stat.hasCycles : family == 1 ? stat.hasSoh : stat.hasDsgCap;
    if(!present) continue;
    double value = family == 0 ? stat.cycles : family == 1 ? stat.soh : stat.dsgCap;
    output.printf("%s_battery_stat_%s{unit=\"bat%d\"} %.6f\n", mqttSettings.promNamespace, name, unit + 1, value);
  }
}

static void emitStatRangeFamily(MetricWriter& output, int family, const char* name, const char* help, const char* rangeLabel)
{
  bool hasValues = false;
  for(size_t i=0; i<statRangeCount; ++i) if(statRanges[i].family == family) { hasValues = true; break; }
  if(!hasValues) return;
  emitHeader(output, "battery_stat", name, help);
  for(size_t i=0; i<statRangeCount; ++i)
  {
    const StatRangeRecord& record = statRanges[i];
    if(record.family != family) continue;
    output.printf("%s_battery_stat_%s{unit=\"bat%d\",%s=\"%s\"} %.6f\n", mqttSettings.promNamespace, name, record.unit, rangeLabel, record.range, (double)record.value);
  }
}

static void emitAllMetrics(MetricWriter& output)
{
  if(mqttSettings.promFreeHeap)
  {
    output.printf("# HELP %s_esp_free_heap_bytes ESP8266 free heap in bytes.\n", mqttSettings.promNamespace);
    output.printf("# TYPE %s_esp_free_heap_bytes gauge\n", mqttSettings.promNamespace);
    output.printf("%s_esp_free_heap_bytes %u\n", mqttSettings.promNamespace, ESP.getFreeHeap());
  }
  emitPowerFamily(output, 0, "volt", "Power supply voltage in millivolts.");
  emitPowerFamily(output, 1, "curr", "Power supply current in milliamps.");
  emitPowerFamily(output, 2, "temp_celsius", "Power supply board temperature in degrees Celsius. Assumes input is milli-degrees C.");
  emitPowerFamily(output, 3, "base_state", "Power supply base state code (e.g., 0: Charge, 1: Dischg, 2: Idle, -1: N/A).");
  emitPowerFamily(output, 4, "soc_percent", "Power supply State of Charge or equivalent percentage (from 'Coulomb' field).");
  emitPowerFamily(output, 5, "mos_temp_celsius", "Power supply MOS temperature in degrees Celsius. Assumes input is milli-degrees C if numeric.");
  emitBatteryFamily(output, 0, "volt", "Battery voltage in millivolts.");
  emitBatteryFamily(output, 1, "curr", "Battery current in milliamps.");
  emitBatteryFamily(output, 2, "temp_celsius", "Battery temperature in degrees Celsius. Assumes input is milli-degrees C (e.g., 17000 -> 17.0 C).");
  emitBatteryFamily(output, 3, "base_state", "Battery base state code (0: Charge, 1: Dischg, 2: Idle, 3: Balance, -1: Unknown).");
  emitBatteryFamily(output, 4, "soc", "Battery State of Charge in percent.");
  emitBatteryFamily(output, 5, "coulomb", "Battery remaining capacity in milliampere-hours.");
  emitBatteryFamily(output, 6, "bal_active_count", "Number of active balancing channels. If BAL is 'N' or similar, this will be 0.");
  emitStatUnitFamily(output, 0, "cycles", "Battery cycle count from stat output.", MAX_PYLON_BATTERIES);
  emitStatUnitFamily(output, 1, "soh_percent", "Battery state of health in percent from stat output.", MAX_PYLON_BATTERIES);
  emitStatUnitFamily(output, 2, "dsg_cap", "Cumulative discharge capacity from stat output, in the device's reported units.", MAX_PYLON_BATTERIES);
  emitStatRangeFamily(output, 0, "chg_curr_secs", "Charge current seconds by current range from stat output.", "current_range");
  emitStatRangeFamily(output, 1, "dsg_curr_secs", "Discharge current seconds by current range from stat output.", "current_range");
  emitStatRangeFamily(output, 2, "soc_secs", "SOC seconds by SOC range (0-20, 20-60, gt60) from stat output.", "soc_range");
}

void handleMetrics()
{
  batteryRecordCount = 0;

  bool pwrReceived = sendCommandAndReadSerialResponse("pwr");
  if(!pwrReceived || !parsePwrResponse(g_szRecvBuff))
  {
    String error = pwrReceived ? "PWR response could not be parsed:\n" : "No PWR response received.\n";
    if(pwrReceived) error += g_szRecvBuff;
    server.send(502, "text/plain", error);
    return;
  }

  int units = g_stack.batteryCount;
  for(int unit=1; unit<=units; ++unit)
  {
    char command[20]; snprintf(command, sizeof(command), "bat %d", unit);
    if(!sendCommandAndReadSerialResponse(command, false, METRICS_COMMAND_WAIT_LOOPS))
    {
      char message[40]; snprintf(message, sizeof(message), "Metrics: %s failed", command);
      Log(message);
      continue;
    }
    if(!collectBatteryMetrics(unit))
    {
      server.send(502, "text/plain", "Too many BAT rows.\n");
      return;
    }
  }
  if(!statCacheValid || millis() - statLastFetch >= STAT_REFRESH_INTERVAL_MS)
  {
    bool refreshed = false;
    for(int unit=1; unit<=units; ++unit)
    {
      char command[20]; snprintf(command, sizeof(command), "stat %d", unit);
      if(!sendCommandAndReadSerialResponse(command, false, METRICS_COMMAND_WAIT_LOOPS))
      {
        char message[40]; snprintf(message, sizeof(message), "Metrics: %s failed", command);
        Log(message);
        continue;
      }
      bool parsed = false;
      if(!collectStatMetrics(unit, parsed))
      {
        server.send(502, "text/plain", "Too many STAT rows.\n");
        return;
      }
      refreshed |= parsed;
    }
    if(refreshed)
    {
      statCacheValid = true;
      statLastFetch = millis();
    }
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.client().setSync(true);
  Log("Metrics: send");
  server.send(200, "text/plain; version=0.0.4; charset=utf-8", "");
  MetricWriter response;
  emitAllMetrics(response);
  response.finish();
  server.sendContent("");
  Log("Metrics: done");
}
