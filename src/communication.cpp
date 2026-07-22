void switchBaud(int newRate)
{
  if(g_baudRate == newRate)
  {
    return;
  }
  
  if(g_baudRate != 0)
  {
    Serial.flush();
    delay(20);
    Serial.end();
    delay(20);
  }

  char szMsg[50];
  snprintf(szMsg, sizeof(szMsg)-1, "New baud: %d", newRate);
  Log(szMsg);
  
  Serial.begin(newRate);
  g_baudRate = newRate;

  delay(20);
}

void waitForSerial(int loops)
{
  for(int ix=0; ix<loops;ix++)
  {
    if(Serial.available()) break;
    delay(10);
  }
}

int readFromSerial(int waitLoops = 150)
{
  memset(g_szRecvBuff, 0, sizeof(g_szRecvBuff));
  int recvBuffLen = 0;
  bool foundTerminator = false;
  
  waitForSerial(waitLoops);
  
  while(Serial.available())
  {
    char szResponse[256] = "";
    const int readNow = Serial.readBytesUntil('>', szResponse, sizeof(szResponse)-1);
    if(readNow > 0 && 
       szResponse[0] != '\0')
    {
      if(readNow + recvBuffLen + 1 >= (int)(sizeof(g_szRecvBuff)))
      {
        Log("WARNING: Read too much data on the console!");
        break;
      }
      
      strcat(g_szRecvBuff, szResponse);
      recvBuffLen += readNow;

      const size_t responseLength = strlen(g_szRecvBuff);
      if(responseLength >= 5 && strcmp(g_szRecvBuff + responseLength - 5, "pylon") == 0)
      {
        strcat(g_szRecvBuff, ">"); //readBytesUntil will skip this, so re-add
        foundTerminator = true;
        break; //found end of the string
      }

      if(strstr(g_szRecvBuff, "Press [Enter] to be continued,other key to exit"))
      {
        //we need to send new line character so battery continues the output
        Serial.write("\r");
      }

      waitForSerial(waitLoops);
    }
  }

  if(recvBuffLen > 0 )
  {
    if(foundTerminator == false)
    {
      Log("Failed to find pylon> terminator");
    }
  }

  return recvBuffLen;
}

bool readFromSerialAndSendResponse()
{
  const int recvBuffLen = readFromSerial();
  if(recvBuffLen > 0)
  {
    server.sendContent(g_szRecvBuff);
    return true;
  }

  return false;
}

bool sendCommandAndReadSerialResponse(const char* pszCommand, bool wakeRetry, int waitLoops)
{
  switchBaud(115200);

  // Discard bytes left by a previous background command.
  while(Serial.available()) Serial.read();

  if(pszCommand[0] != '\0')
  {
    Serial.write(pszCommand);
  }
  Serial.write("\n");

  const int recvBuffLen = readFromSerial(waitLoops);
  const bool complete = strstr(g_szRecvBuff, "pylon>") != NULL;
  const bool rejected = strstr(g_szRecvBuff, "Unknown command") != NULL;
  if(recvBuffLen > 0 && !rejected && (pszCommand[0] == '\0' || complete))
  {
    return true;
  }

  if(!wakeRetry) return false;

  // Wake the console and retry foreground requests.
  wakeUpConsole();

  if(pszCommand[0] != '\0')
  {
    Serial.write(pszCommand);
  }
  Serial.write("\n");

  const int retryLength = readFromSerial();
  return retryLength > 0 &&
         strstr(g_szRecvBuff, "Unknown command") == NULL &&
         (pszCommand[0] == '\0' || strstr(g_szRecvBuff, "pylon>") != NULL);
}

unsigned long os_getCurrentTimeSec()
{
  static unsigned int wrapCnt = 0;
  static unsigned long lastVal = 0;
  unsigned long currentVal = millis();

  if(currentVal < lastVal)
  {
    wrapCnt++;
  }

  lastVal = currentVal;
  unsigned long seconds = currentVal/1000;
  
  //millis will wrap each 50 days, as we are interested only in seconds, let's keep the wrap counter
  return (wrapCnt*4294967) + seconds;
}

void syncTime()
{
  time_t currentTimeGMT = getNtpTime(mqttSettings.ntpServer);
  if(currentTimeGMT)
  {
    ntpTimeReceived = true;
    setTime(currentTimeGMT);
    timer.setTimeout(NTP_RESYNC_INTERVAL_MS, syncTime);
  }  
  else
  {
    timer.setTimeout(5000, syncTime); //try again in 5 seconds
  }
}

void wakeUpConsole()
{
  switchBaud(1200);

  //byte wakeUpBuff[] = {0x7E, 0x32, 0x30, 0x30, 0x31, 0x34, 0x36, 0x38, 0x32, 0x43, 0x30, 0x30, 0x34, 0x38, 0x35, 0x32, 0x30, 0x46, 0x43, 0x43, 0x33, 0x0D};
  //Serial.write(wakeUpBuff, sizeof(wakeUpBuff));
  Serial.write("~20014682C0048520FCC3\r");
  delay(1000);

  byte newLineBuff[] = {0x0D, 0x0A};
  switchBaud(115200);
  
  for(int ix=0; ix<10; ix++)
  {
    Serial.write(newLineBuff, sizeof(newLineBuff));
    delay(1000);

    if(Serial.available())
    {
      while(Serial.available())
      {
        Serial.read();
      }
      
      break;
    }
  }
}

struct pylonBattery
{
  bool isPresent;
  long  soc;     //Coulomb in %
  long  voltage; //in mV
  long  current; //in mA, negative value is discharge
  long  tempr;   //temp of case or BMS?
  long  cellTempLow;
  long  cellTempHigh;
  long  cellVoltLow;
  long  cellVoltHigh;
  char baseState[9];    //Charge | Dischg | Idle
  char voltageState[9]; //Normal
  char currentState[9]; //Normal
  char tempState[9];    //Normal
  char time[20];        //2019-06-08 04:00:29
  char b_v_st[9];       //Normal  (battery voltage?)
  char b_t_st[9];       //Normal  (battery temperature?)
  char mosTempr[12];

  bool isCharging()    const { return strcmp(baseState, "Charge")   == 0; }
  bool isDischarging() const { return strcmp(baseState, "Dischg")   == 0; }
  bool isIdle()        const { return strcmp(baseState, "Idle")     == 0; }
  bool isBalancing()   const { return strcmp(baseState, "Balance")  == 0; }
  

  bool isNormal() const
  {
    if(isCharging()    == false &&
       isDischarging() == false &&
       isIdle()        == false &&
       isBalancing()   == false)
    {
      return false; //base state looks wrong!
    }

    return  strcmp(voltageState, "Normal") == 0 &&
            strcmp(currentState, "Normal") == 0 &&
            strcmp(tempState,    "Normal") == 0 &&
            strcmp(b_v_st,       "Normal") == 0 &&
            strcmp(b_t_st,       "Normal") == 0 ;
  }
};

struct batteryStack
{
  int batteryCount;
  int soc;  //in %, if charging: average SOC, otherwise: lowest SOC
  int temp; //in mC, if highest temp is > 15C, this will show the highest temp, otherwise the lowest
  long currentDC;    //mAh current going in or out of the battery
  long avgVoltage;    //in mV
  char baseState[9];  //Charge | Dischg | Idle | Balance | Alarm!

  pylonBattery batts[MAX_PYLON_BATTERIES];

  bool isNormal() const
  {
    for(int ix=0; ix<MAX_PYLON_BATTERIES; ix++)
    {
      if(batts[ix].isPresent && 
         batts[ix].isNormal() == false)
      {
        return false;
      }
    }

    return true;
  }

  //in W
  long getPowerDC() const
  {
    return (long)(((double)currentDC/1000.0)*((double)avgVoltage/1000.0));
  }

  //W estimated power on AC side (taking into account Sofar ME3000SP losses)
  long getEstPowerAc() const
  {
    double powerDC = (double)getPowerDC();
    if(powerDC == 0)
    {
      return 0;
    }
    else if(powerDC < 0)
    {
      //we are discharging, on AC side we will see less power due to losses
      if(powerDC < -1000)
      {
        return (long)(powerDC*0.94);
      }
      else if(powerDC < -600)
      {
        return (long)(powerDC*0.90);
      }
      else
      {
        return (long)(powerDC*0.87);
      }
    }
    else
    {
      //we are charging, on AC side we will have more power due to losses
      if(powerDC > 1000)
      {
        return (long)(powerDC*1.06);
      }
      else if(powerDC > 600)
      {
        return (long)(powerDC*1.1);
      }
      else
      {
        return (long)(powerDC*1.13);
      }
    }
  }
};

batteryStack g_stack;


long extractInt(const char* pStr, int pos)
{
  return atol(pStr+pos);
}

void extractStr(const char* pStr, int pos, char* strOut, int strOutSize)
{
  strOut[strOutSize-1] = '\0';
  strncpy(strOut, pStr+pos, strOutSize-1);
  strOutSize--;
  
  
  //trim right
  while(strOutSize > 0)
  {
    if(isspace(strOut[strOutSize-1]))
    {
      strOut[strOutSize-1] = '\0';
    }
    else
    {
      break;
    }

    strOutSize--;
  }
}

/* Output has mixed \r and \r\n
pwr

@

Power Volt   Curr   Tempr  Tlow   Thigh  Vlow   Vhigh  Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St  

1     49735  -1440  22000  19000  19000  3315   3317   Dischg   Normal   Normal   Normal   93%      2019-06-08 04:00:30  Normal   Normal  

....   

8     -      -      -      -      -      -      -      Absent   -        -        -        -        -                    -        -       

Command completed successfully

$$

pylon

---- us5000 Soft  version V2.3:
Power Volt   Curr   Tempr  Tlow   Tlow.Id  Thigh  Thigh.Id Vlow   Vlow.Id  Vhigh  Vhigh.Id Base.St  Volt.St  Curr.St  Temp.St  Coulomb  Time                 B.V.St   B.T.St  MosTempr M.T.St   SysAlarm.St
1     49435  0      9900   7500   8        8000   12       3295   1        3296   0        Idle     Normal   Normal   Normal   60%      2025-12-24 17:33:57  Normal   Normal  9400     Normal   Normal  
2     -      -      -      -      -      -      -      Absent   -        -        -        -        -                    -        -       

*/
int findOffset(const char *pStr, const char *param)
{
    const char *lineStart = pStr;

    while (*lineStart) {
        /* Find end of current line */
        const char *lineEnd = strchr(lineStart, '\n');
        if (!lineEnd)
            lineEnd = lineStart + strlen(lineStart);

        /* Check if line starts with "Power" */
        if (strncmp(lineStart, "Power", 5) == 0) {
            const char *pos = strstr(lineStart, param);
            if (pos && pos < lineEnd) {
                return (int)(pos - lineStart);
            }
            return -1; /* Parameter not found */
        }

        /* Move to next line */
        if (*lineEnd == '\0')
            break;
        lineStart = lineEnd + 1;
    }

    return -1; /* "Power" line not found */
}

bool parsePwrResponse(const char* pStr)
{
  if(strstr(pStr, "Command completed successfully") == NULL)
  {
    return false;
  }
  
  int chargeCnt    = 0;
  int dischargeCnt = 0;
  int idleCnt      = 0;
  int alarmCnt     = 0;
  int socAvg       = 0;
  int socLow       = 0;
  int tempHigh     = 0;
  int tempLow      = 0;

  memset(&g_stack, 0, sizeof(g_stack));

  DECL_FIND_OFFSET_OR_FAIL(offset1,  pStr,"Base.St");
  DECL_FIND_OFFSET_OR_FAIL(offset2,  pStr,"Volt.St");
  DECL_FIND_OFFSET_OR_FAIL(offset3,  pStr,"Curr.St");
  DECL_FIND_OFFSET_OR_FAIL(offset4,  pStr,"Temp.St");
  DECL_FIND_OFFSET_OR_FAIL(offset5,  pStr,"Time");
  DECL_FIND_OFFSET_OR_FAIL(offset6,  pStr,"B.V.St");
  DECL_FIND_OFFSET_OR_FAIL(offset7,  pStr,"B.T.St");
  DECL_FIND_OFFSET_OR_FAIL(offset8,  pStr,"Volt "); //extra space not to confuse with Volt.St
  DECL_FIND_OFFSET_OR_FAIL(offset9,  pStr,"Curr ");
  DECL_FIND_OFFSET_OR_FAIL(offset10, pStr,"Tempr ");
  DECL_FIND_OFFSET_OR_FAIL(offset11, pStr,"Tlow ");
  DECL_FIND_OFFSET_OR_FAIL(offset12, pStr,"Thigh ");
  DECL_FIND_OFFSET_OR_FAIL(offset13, pStr,"Vlow ");
  DECL_FIND_OFFSET_OR_FAIL(offset14, pStr,"Vhigh ");    
  DECL_FIND_OFFSET_OR_FAIL(offset15, pStr,"Coulomb ");
  int offsetMos = findOffset(pStr, "MosTempr");
  if(offsetMos < 0) offsetMos = findOffset(pStr, "MosTemp");
  
  for(int ix=0; ix<MAX_PYLON_BATTERIES; ix++)
  {
    char szToFind[32] = "";
    snprintf(szToFind, sizeof(szToFind)-1, "\r\r\n%d     ", ix+1);

    const char* pLineStart = strstr(pStr, szToFind);
    if(pLineStart == NULL)
    {
      return false;
    }

    pLineStart += 3; //move past \r\r\n    
    
    //US5000 and US2000 show "Absent" field at the same posion
    //For US2000 this matches "Base.St" column, for US5000 it does not match any column
    if (strncmp(pLineStart + 55, "Absent", 6) == 0)
    {
      strcpy(g_stack.batts[ix].baseState, "Absent");
      g_stack.batts[ix].isPresent = false;
    }
    else
    {
      extractStr(pLineStart, offset1, g_stack.batts[ix].baseState, sizeof(g_stack.batts[ix].baseState));
      g_stack.batts[ix].isPresent = true;
    }

    if(g_stack.batts[ix].isPresent)
    {
      extractStr(pLineStart, offset2, g_stack.batts[ix].voltageState, sizeof(g_stack.batts[ix].voltageState));
      extractStr(pLineStart, offset3, g_stack.batts[ix].currentState, sizeof(g_stack.batts[ix].currentState));
      extractStr(pLineStart, offset4, g_stack.batts[ix].tempState, sizeof(g_stack.batts[ix].tempState));
      extractStr(pLineStart, offset5, g_stack.batts[ix].time, sizeof(g_stack.batts[ix].time));
      extractStr(pLineStart, offset6, g_stack.batts[ix].b_v_st, sizeof(g_stack.batts[ix].b_v_st));
      extractStr(pLineStart, offset7, g_stack.batts[ix].b_t_st, sizeof(g_stack.batts[ix].b_t_st));
      g_stack.batts[ix].voltage = extractInt(pLineStart, offset8);
      g_stack.batts[ix].current = extractInt(pLineStart, offset9);
      g_stack.batts[ix].tempr   = extractInt(pLineStart, offset10);
      g_stack.batts[ix].cellTempLow    = extractInt(pLineStart, offset11);
      g_stack.batts[ix].cellTempHigh   = extractInt(pLineStart, offset12);
      g_stack.batts[ix].cellVoltLow    = extractInt(pLineStart, offset13);
      g_stack.batts[ix].cellVoltHigh   = extractInt(pLineStart, offset14);
      g_stack.batts[ix].soc            = extractInt(pLineStart, offset15);
      if(offsetMos >= 0) extractStr(pLineStart, offsetMos, g_stack.batts[ix].mosTempr, sizeof(g_stack.batts[ix].mosTempr));

      //////////////////////////////// Post-process ////////////////////////
      g_stack.batteryCount++;
      g_stack.currentDC += g_stack.batts[ix].current;
      g_stack.avgVoltage += g_stack.batts[ix].voltage;
      socAvg += g_stack.batts[ix].soc;

      if(g_stack.batts[ix].isNormal() == false){ alarmCnt++; }
      else if(g_stack.batts[ix].isCharging()){chargeCnt++;}
      else if(g_stack.batts[ix].isDischarging()){dischargeCnt++;}
      else if(g_stack.batts[ix].isIdle()){idleCnt++;}
      else{ alarmCnt++; } //should not really happen!

      if(g_stack.batteryCount == 1)
      {
        socLow = g_stack.batts[ix].soc;
        tempLow  = g_stack.batts[ix].cellTempLow;
        tempHigh = g_stack.batts[ix].cellTempHigh;
      }
      else
      {
        if(socLow > g_stack.batts[ix].soc){socLow = g_stack.batts[ix].soc;}
        if(tempHigh < g_stack.batts[ix].cellTempHigh){tempHigh = g_stack.batts[ix].cellTempHigh;}
        if(tempLow > g_stack.batts[ix].cellTempLow){tempLow = g_stack.batts[ix].cellTempLow;}
      }
    }
  }

  //now update stack state:
  if(g_stack.batteryCount > 0)
  {  
    g_stack.avgVoltage /= g_stack.batteryCount;
    g_stack.soc = socLow;
  }

  if(tempHigh > 15000) //15C
  {
    g_stack.temp = tempHigh; //in the summer we highlight the warmest cell
  }
  else
  {
    g_stack.temp = tempLow; //in the winter we focus on coldest cell
  }

  if(alarmCnt > 0)
  {
    strcpy(g_stack.baseState, "Alarm!");
  }
  else if(chargeCnt == g_stack.batteryCount)
  {
    strcpy(g_stack.baseState, "Charge");
    g_stack.soc = (int)(socAvg / g_stack.batteryCount);
  }
  else if(dischargeCnt == g_stack.batteryCount)
  {
    strcpy(g_stack.baseState, "Dischg");
  }
  else if(idleCnt == g_stack.batteryCount)
  {
    strcpy(g_stack.baseState, "Idle");
  }
  else
  {
    strcpy(g_stack.baseState, "Balance");
  }


  return true;
}
