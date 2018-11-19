#include "driver/rtc_io.h"
#include "esp_deep_sleep.h"
#include "iotconfig.hpp"

#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif

IPAddress iotConfigApIP(192, 168, 4, 1);
DNSServer iotConfigDnsServer;
WiFiServer iotConfigServer(80);
WiFiClient iotConfigClient;

RTC_DATA_ATTR uint8_t firstBoot = 1;
RTC_DATA_ATTR uint8_t iot_rtc_data[IOT_RTC_DATA_SIZE];

unsigned long iotConfigCurrentMillis=0;
static bool iotConfigOtaPrio = false;
static bool iotConfigOnline = false;
static unsigned long iotConfigWifiLossTS=0;

iotConfig::iotConfig()
{
   eepromAllocData = NULL;
   rtcAllocData = NULL;
   eepromDataIndex = 0;
   rtcDataIndex = 0;
   iotConfigMode = iotConfigNoneMode;
   iotConfigServerState = iotConfigServerState;
   numScannedNetworks = 0;
   joinedNetworkIndex = 0;
   clientConnectTime = 0;
   clientTimeOut = 2000;
   closeConn = false;
   currentLine = "";
   apExpireTime = 0;
   watchDogTimeout = 20000;
   otaInitialized = false;
}

iotConfig::~iotConfig()
{
}

static void iotConfigWiFiEvent(WiFiEvent_t event)
{
   Serial.printf("[WiFi-event] event: %d\n", event);

   switch(event)
   {
      case SYSTEM_EVENT_STA_GOT_IP:
          Serial.println("WiFi connected");
          Serial.println("IP address: ");
          Serial.println(WiFi.localIP());
          iotConfigOnline=true;
          break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
          Serial.println("WiFi lost connection");
          iotConfigOnline=false;
          iotConfigWifiLossTS=iotConfigCurrentMillis;
          break;
   }
}

bool iotConfig::begin(const char *deviceName, const char *initialPasswordN,
                      const size_t eepromSizeN, const size_t rtcDataSizeN, const uint16_t coldBootAPtime)
 
{
   esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
   esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
   esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);

   if (rtcDataSizeN > IOT_RTC_DATA_SIZE)
   {
      return false;
   }
   eepromSize=eepromSizeN+
              sizeof(eepromCRC)+
              sizeof(friendlyName)+
              sizeof(wifiApPassword)+
              sizeof(wifiClientSSID)+
              sizeof(wifiClientPassword)+
              sizeof(otaPassword);
   rtcDataSize=rtcDataSizeN;

   EEPROM.begin(eepromSize);

   assignVariableEEPROM((uint8_t*)&eepromCRC, sizeof(eepromCRC));

   if (firstBoot)
   {
      Serial.println("INFO: First boot, cleaning RTC_DATA memory");
      for (int i=0; i<rtcDataSizeN; i++)
      {
         iot_rtc_data[i]=0;
      }
   }
   if (eepromCRC != calcCRC())
   {
      Serial.println("WARN: EEPROM CRC mismatch, erasing EEPROM");
      factoryReset();
   }

   assignVariableEEPROM((uint8_t*)&friendlyName, sizeof(friendlyName));
   assignVariableEEPROM((uint8_t*)&wifiApPassword, sizeof(wifiApPassword));
   if (strlen(friendlyName)==0)
   {
      strncpy(friendlyName,
              deviceName,
              min(  strlen(deviceName),sizeof(friendlyName)  ) ); 
      strncpy(wifiApPassword,
              initialPasswordN,
              min(  strlen(initialPasswordN),sizeof(wifiApPassword)  ) ); 
      Serial.print("INFO: Setting default friendlyName to: ");
      Serial.println(friendlyName);
   }

   assignVariableEEPROM((uint8_t*)&wifiClientSSID, sizeof(wifiClientSSID));
   assignVariableEEPROM((uint8_t*)&wifiClientPassword, sizeof(wifiClientPassword));
   assignVariableEEPROM((uint8_t*)&otaPassword, sizeof(otaPassword));

   if ((strlen(otaPassword)>0) && ((!firstBoot)||(coldBootAPtime==0)))
   {
      WiFi.onEvent(iotConfigWiFiEvent);
      Serial.println();
      Serial.println();
      Serial.print("Connecting to ");
      Serial.println(wifiClientSSID);

      WiFi.mode(WIFI_STA);
      WiFi.setHostname(friendlyName);
      WiFi.begin(wifiClientSSID, wifiClientPassword);
      iotConfigMode=iotConfigClientMode;
   }
   else
   {
      Serial.print("INFO: Setting up Access Point with SSID: ");
      Serial.println(friendlyName);
      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(iotConfigApIP, iotConfigApIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(friendlyName, wifiApPassword);
      // if DNSServer is started with "*" for domain name, it will reply with
      // provided IP to all DNS request
      iotConfigDnsServer.start(53, "*", iotConfigApIP);
      iotConfigServer.begin();
      iotConfigMode=iotConfigServerMode;
      apExpireTime=millis() + coldBootAPtime;
   }
   
   return true;
}

void iotConfig::arduinoOTAsetup(const char *friendlyName, const char *otaPassword)
{
   ArduinoOTA.setHostname(friendlyName);
   ArduinoOTA.setPassword(otaPassword);
   ArduinoOTA
     .onStart([]() {
       String type;
       if (ArduinoOTA.getCommand() == U_FLASH)
         type = "sketch";
       else // U_SPIFFS
        type = "filesystem";
         // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
       Serial.println("Start updating " + type);
       iotConfigOtaPrio = true;
     })
     .onEnd([]() {
       Serial.println("\nEnd");
       iotConfigOtaPrio = false;
     })
     .onProgress([](unsigned int progress, unsigned int total) {
       Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
     })
     .onError([](ota_error_t error) {
       Serial.printf("Error[%u]: ", error);
       if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
       else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
       else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
       else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
       else if (error == OTA_END_ERROR) Serial.println("End Failed");
       iotConfigOtaPrio = false;
     });
   ArduinoOTA.begin();
}

void iotConfig::recoveryChanceWait()
{
   while (millis() < apExpireTime)
   {
      handle();
   }
}

void iotConfig::setWiFiClientWatchDogTimeout(const uint32_t timeoutMS)
{
   watchDogTimeout = timeoutMS;
}

bool iotConfig::assignVariableEEPROM(uint8_t *pointer, const size_t varSize)
{
   memAllocation_t newInfo;

   if ((eepromAssignPointer+varSize) > eepromSize)
   {
      Serial.println("ERROR: No variable space available for EEPROM");
      return false;
   }
   newInfo.varPtr=pointer;
   newInfo.nvIndex=eepromAssignPointer;
   newInfo.allocSize=varSize;
   if (addVariableInfo(&eepromAllocData, &eepromDataIndex, &newInfo))
   {
      Serial.print("Reading ");
      Serial.print(varSize);
      Serial.print(" bytes of EEPROM data at index ");
      Serial.print(eepromAssignPointer);
      Serial.print(" into RAM @");
      Serial.println((int)pointer,HEX);
      for (int i=0; i<varSize; i++)
      {
         pointer[i]=EEPROM.read(eepromAssignPointer+i);
         //Serial.println(pointer[i]);
      }
      eepromAssignPointer+=varSize;
      return true;
   }
   return false;
}

bool iotConfig::assignVariableRTCDATA(uint8_t *pointer, const size_t varSize)
{
   memAllocation_t newInfo;

   if ((rtcDataAssignPointer+varSize) > rtcDataSize)
   {
      Serial.println("ERROR: No variable space available for RTC_DATA");
      return false;
   }
   newInfo.varPtr=pointer;
   newInfo.nvIndex=rtcDataAssignPointer;
   newInfo.allocSize=varSize;
   if (addVariableInfo(&rtcAllocData, &rtcDataIndex, &newInfo))
   {
      Serial.print("Reading ");
      Serial.print(varSize);
      Serial.print(" bytes of RTC_DATA at index ");
      Serial.print(rtcDataAssignPointer);
      Serial.print(" into RAM @");
      Serial.println((int)pointer,HEX);
      for (int i=0; i<varSize; i++)
      {
         pointer[i]=iot_rtc_data[rtcDataAssignPointer+i];
         //Serial.println(pointer[i]);
      }
      rtcDataAssignPointer+=varSize;
      return true;
   }
   return false;
}

bool iotConfig::addVariableInfo(memAllocation_t **store,
                                int *indexPtr,
                                memAllocation_t *info)
{
   memAllocation_t *newStore;
   int idx=*indexPtr;

   newStore=(memAllocation_t*)malloc((idx+1)*sizeof(memAllocation_t));
   if (!newStore)
   {
      Serial.println("ERROR allocating space for variable storage info");
      return false;
   }
   if (*store)
   {
      memcpy(newStore,*store,idx*sizeof(memAllocation_t));
      free(*store);
   }
   *store=newStore;
   memcpy((void*)(*(store)+idx),(void*)info,sizeof(memAllocation_t));
   (*indexPtr)++;
   return true;
}

void iotConfig::factoryReset()
{
   for (int i=0; i<eepromSize; i++)
   {
      EEPROM.write(i, 0);
   }
   EEPROM.commit();
}

void iotConfig::updateEEPROM()
{
   for (int n=eepromDataIndex-1; n>=0; n--)
   {
      if (n==0)
      {
         Serial.print("INFO: Calculating CRC: ");
         eepromCRC=calcCRC();
         Serial.println(eepromCRC,HEX);
      }
      Serial.print("INFO: Writing variable (@");
      Serial.print((int)eepromAllocData[n].varPtr,HEX);
      Serial.print(") of ");
      Serial.print(eepromAllocData[n].allocSize,DEC);
      Serial.print(" bytes into EEPROM at addr ");
      Serial.println(eepromAllocData[n].nvIndex,DEC);
      for (int i=0; i<eepromAllocData[n].allocSize; i++)
      {
         EEPROM.write(eepromAllocData[n].nvIndex+i,
                      eepromAllocData[n].varPtr[i]);
      }
   }  
}

void iotConfig::updateRTCDATA()
{
   for (int n=rtcDataIndex-1; n>=0; n--)
   {
      Serial.print("INFO: Writing variable (@");
      Serial.print((int)rtcAllocData[n].varPtr,HEX);
      Serial.print(") of ");
      Serial.print(rtcAllocData[n].allocSize,DEC);
      Serial.print(" bytes into RTC_DATA at addr ");
      Serial.println(rtcAllocData[n].nvIndex,DEC);
      for (int i=0; i<rtcAllocData[n].allocSize; i++)
      {
         iot_rtc_data[rtcAllocData[n].nvIndex+i]=rtcAllocData[n].varPtr[i];
      }
   }  
}

uint32_t iotConfig::calcCRC()
{
   const unsigned long crc_table[16] = {
     0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
     0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
     0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
     0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
   }; 
 
   unsigned long crc = ~0L;
 
   for (int index = sizeof(uint32_t) ; index < eepromSize ; index++)
   {
     uint8_t readByte = EEPROM.read(index);
     crc = crc_table[(crc ^ readByte) & 0x0f] ^ (crc >> 4);
     crc = crc_table[(crc ^ (readByte >> 4)) & 0x0f] ^ (crc >> 4);
     crc = ~crc;
   }
   return crc;
}

void iotConfig::reboot()
{
   esp_deep_sleep(1000000ULL*2);   
}

void iotConfig::saveAndReboot()
{
   updateRTCDATA();
   updateEEPROM();
   EEPROM.commit();
   esp_deep_sleep(1000000ULL*2);   
}

bool iotConfig::handle()
{
   iotConfigCurrentMillis = millis();
   
   switch(iotConfigMode)
   {
      case iotConfigClientMode:
           if (otaInitialized)
           {        
              do {
                 ArduinoOTA.handle();
              } while (iotConfigOtaPrio);
           }
           else
           {
              if (iotConfigOnline)
              {
                 arduinoOTAsetup(friendlyName, otaPassword);
                 otaInitialized = true;
              }
           }
           
           if ((!iotConfigOnline) && (watchDogTimeout > 0))
           {
              if (iotConfigCurrentMillis > iotConfigWifiLossTS + watchDogTimeout)
              {
                 reboot();
              }
           }
           break;

      case iotConfigServerMode:   
           iotConfigDnsServer.processNextRequest();

           if ((firstBoot) && (iotConfigCurrentMillis > apExpireTime) && (strlen(otaPassword)>0))
           {
              Serial.println("INFO: Change from AP mode to Client mode");
              firstBoot = 0;
              WiFi.disconnect(true);
              WiFi.mode(WIFI_STA);
              reboot();
           }
                    
           if (iotConfigClient)
           {
              firstBoot = 0;
              if (iotConfigClient.connected() && (!closeConn || iotConfigClient.available()) && (iotConfigCurrentMillis < (clientConnectTime+clientTimeOut)))
              {
                 if (iotConfigClient.available())
                 {
                    clientConnectTime = iotConfigCurrentMillis;
                    char c = iotConfigClient.read();
                    if (c == '\n') 
                    {
                       if (currentLine.length() == 0)
                       {
                          apExpireTime=iotConfigCurrentMillis + 60000;
                          iotConfigClient.println("HTTP/1.1 200 OK");
                          iotConfigClient.println("Content-type:text/html");
                          iotConfigClient.println();
                          iotConfigClient.print("<!DOCTYPE html><html><head><title>CaptivePortal</title>");
                          if (iotConfigServerState==iotConfigScanSSIDs)
                          {
                             iotConfigClient.print("<META HTTP-EQUIV=\"refresh\" CONTENT=\"6\">");
                          }
                          iotConfigClient.print("</head><body>");
                          iotConfigClient.print(friendlyName);
                          iotConfigClient.print(" device configuration<br>");
                          switch(iotConfigServerState)
                          {
                             case iotConfigScanSSIDs:
                                  Serial.println("scan start");
                                  iotConfigClient.println("Scanning WiFi networks, please wait ...<br>");
                                  iotConfigClient.print("</body></html>");
                                  iotConfigClient.stop();
                                  
                                  // WiFi.scanNetworks will return the number of networks found
                                  numScannedNetworks = WiFi.scanNetworks();
                                  Serial.println("scan done");
                                  iotConfigServerState=iotConfigShowSSIDs;
                                  break;

                             case iotConfigShowSSIDs:
                                  if (numScannedNetworks == 0) {
                                      iotConfigClient.println("no networks found<br>");
                                      iotConfigServerState = iotConfigScanSSIDs;
                                  } else {
                                      iotConfigClient.print(numScannedNetworks);
                                      iotConfigClient.println(" networks found:<br><br>");
                                      for (int i = 0; i < numScannedNetworks; ++i) {
                                          // Print SSID and RSSI for each network found
                                          iotConfigClient.print("<a href=\"/join/");
                                          iotConfigClient.print(i + 1);
                                          iotConfigClient.print("\">Join ");
                                          iotConfigClient.print(WiFi.SSID(i));
                                          iotConfigClient.print(" (");
                                          iotConfigClient.print(WiFi.RSSI(i));
                                          iotConfigClient.print(")");
                                          iotConfigClient.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" <br>":"*</a><br>");
                                      }
                                  }
                                  iotConfigClient.print("<br><br><a href=\"/reset\">Factory reset</a><br>");
                                  iotConfigClient.print("<br><a href=\"/recovery\">Firmware recovery / unbrick</a><br>");
                                  break;
                                  
                             case iotConfigJoinForm:
                                  iotConfigClient.print("Logging into WiFi <b>");
                                  iotConfigClient.print(WiFi.SSID(joinedNetworkIndex-1));
                                  iotConfigClient.println("</b><br><br>");
                                  iotConfigClient.print("<form method=\"get\" onsubmit=\"javascript:document.location='/login.cgi' + $('pass') + '';\">");
                                  iotConfigClient.print("WiFi PSK-Key: ");
                                  iotConfigClient.print("<input type=\"password\" name=\"pass\" id=\"pass\" /><br>");
                                  iotConfigClient.print("Friendly Name: ");
                                  iotConfigClient.print("<input type=\"text\" name=\"fname\" id=\"fname\" /><br>");
                                  if (strlen(otaPassword) == 0)
                                  {
                                     iotConfigClient.print("New OTA-Password: ");
                                     iotConfigClient.print("<input type=\"password\" name=\"ota\" id=\"ota\" /><br>");
                                     iotConfigClient.print("repeat OTA-Password: ");
                                     iotConfigClient.print("<input type=\"password\" name=\"otar\" id=\"otar\" /><br>");
                                  }
                                  iotConfigClient.print("<input type=\"submit\" value=\"ok\"/></form>");
                                  break;

                             case iotConfigResetForm:
                                  iotConfigClient.print("<br><b>Factory-Reset");
                                  iotConfigClient.print("</b><br><br>WARNING: All stored data will be lost!");
                                  iotConfigClient.println("<br>");
                                  iotConfigClient.print("<form method=\"get\" onsubmit=\"javascript:document.location='/reset.cgi' + $('pass') + '';\">");
                                  iotConfigClient.print("<br>Enter OTA Password: ");
                                  iotConfigClient.print("<input type=\"password\" name=\"fdpass\" id=\"fdpass\" /><br>");
                                  iotConfigClient.print("<input type=\"submit\" value=\"ok\"/></form>");
                                  break;

                             case iotConfigRecoveryForm:
                                  iotConfigClient.print("<br><b>Firmware Recovery / Unbrick</b><br>");
                                  iotConfigClient.print("<br>1) Enter the OTA password and klick 'ok'");
                                  iotConfigClient.print("<br>2) Connect the development PC to the ESP's AP!");
                                  iotConfigClient.print("<br>3) Start Arduino IDE, choose port 'recovery ");
                                  iotConfigClient.print(friendlyName);
                                  iotConfigClient.print("' and upload new sketch.");
                                  iotConfigClient.println("<br>");
                                  iotConfigClient.print("<form method=\"get\" onsubmit=\"javascript:document.location='/reset.cgi' + $('pass') + '';\">");
                                  iotConfigClient.print("<br>Enter OTA Password: ");
                                  iotConfigClient.print("<input type=\"password\" name=\"fdpass\" id=\"fdpass\" /><br>");
                                  iotConfigClient.print("<input type=\"submit\" value=\"ok\"/></form>");
                                  break;

                             case iotConfigError:
                                  iotConfigClient.print("<br><b>ERROR</b><br><font color=\"red\">");
                                  switch (iotConfigErrorType)
                                  {
                                     case iotConfigErrorNoName:
                                          iotConfigClient.print("FriendlyName must be at least one alphanumeric character.");
                                          break;
                                     case iotConfigErrorTypo:
                                          iotConfigClient.print("OTA passwords did not match.");
                                          break;
                                     case iotConfigErrorWrongPassword:
                                          iotConfigClient.print("Wrong password - Access denied!");
                                          break;
                                  }
                                  iotConfigClient.print("</font><br>");
                                  iotConfigServerState = iotConfigScanSSIDs;
                                  break;
                          }
                          if (iotConfigServerState!=iotConfigScanSSIDs)
                          {
                             iotConfigClient.print("</body></html>");
                          }
                          closeConn = true;
                          break;
                       } else {
                          currentLine = "";
                       }
                    } else if (c != '\r')
                    {
                       currentLine += c;
                    }

                    if ( currentLine.startsWith("GET /join/") &&
                         currentLine.endsWith(" HTTP")
                       )
                    {
                       currentLine.remove(0,10);
                       currentLine.remove(currentLine.length()-5,5);
                       if (currentLine.length() > 6)
                       {
                          String decodedPSKString=queryToAscii(getQueryParam(currentLine,"pass"));
                          String decodedOTAString=queryToAscii(getQueryParam(currentLine,"ota"));
                          String decodedOTARString=queryToAscii(getQueryParam(currentLine,"otar"));
                          String decodedString=queryToAscii(getQueryParam(currentLine,"fname"));
                          iotConfigMode = iotConfigTestWiFi;

                          if (decodedString.length() > 0)
                          {
                             strncpy(friendlyName, decodedString.c_str(), sizeof(friendlyName));
                          }
                          else
                          {
                             iotConfigServerState = iotConfigError;
                             iotConfigErrorType = iotConfigErrorNoName;
                             iotConfigMode = iotConfigServerMode;
                          }

                          if ((strlen(otaPassword)==0) && (iotConfigMode == iotConfigTestWiFi))
                          {
                             if (decodedOTAString != decodedOTARString)
                             {
                                iotConfigServerState = iotConfigError;
                                iotConfigErrorType = iotConfigErrorTypo;
                                iotConfigMode = iotConfigServerMode;
                             }
                             else
                             {
                                strncpy(otaPassword, decodedOTAString.c_str(), sizeof(otaPassword));
                             }
                          }


                          if (iotConfigMode == iotConfigTestWiFi)
                          {
                             strncpy(wifiClientSSID, WiFi.SSID(joinedNetworkIndex-1).c_str(), sizeof(wifiClientSSID));
                             strncpy(wifiClientPassword, decodedPSKString.c_str(), sizeof(wifiClientPassword));
                          }
                       }
                       else
                       {
                          joinedNetworkIndex=currentLine.toInt();
                          iotConfigServerState=iotConfigJoinForm;
                       }
                    }
                    if ( currentLine.startsWith("GET /reset") &&
                         currentLine.endsWith(" HTTP")
                       )
                    {
                       iotConfigServerState = iotConfigResetForm;
                       currentLine.remove(currentLine.length()-5,5);

                       String decodedPassString=queryToAscii(getQueryParam(currentLine,"fdpass"));
                       if (currentLine.indexOf("fdpass") >= 0)
                       {
                          if (strncmp(otaPassword, decodedPassString.c_str(), sizeof(otaPassword)) == 0)
                          {
                             factoryReset();
                             reboot();
                          }
                          else
                          {
                             iotConfigServerState = iotConfigError;
                             iotConfigErrorType = iotConfigErrorWrongPassword;
                          }
                       }
                    }
                    if ( currentLine.startsWith("GET /recovery") &&
                         currentLine.endsWith(" HTTP")
                       )
                    {
                       iotConfigServerState = iotConfigRecoveryForm;
                       currentLine.remove(currentLine.length()-5,5);

                       String decodedPassString=queryToAscii(getQueryParam(currentLine,"fdpass"));
                       if (currentLine.indexOf("fdpass") >= 0)
                       {
                          if (strncmp(otaPassword, decodedPassString.c_str(), sizeof(otaPassword)) == 0)
                          {
                             arduinoOTAsetup(String("recovery " + String(friendlyName)).c_str(), otaPassword);
                             while (1)
                             {
                                ArduinoOTA.handle();
                             }
                          }
                          else
                          {
                             iotConfigServerState = iotConfigError;
                             iotConfigErrorType = iotConfigErrorWrongPassword;
                          }
                       }
                    }
                 }
              }
              else
              {
                 Serial.println("Connection closed");
                 clientConnectTime = iotConfigCurrentMillis;
                 closeConn = false;
                 iotConfigClient.stop();
                 iotConfigClient = NULL;
              }
           }
           else
           {
              iotConfigClient = iotConfigServer.available();   // listen for incoming clients
              currentLine = "";
              clientConnectTime = iotConfigCurrentMillis;
           }
           break;

      case iotConfigTestWiFi:
           firstBoot = 0;
           iotConfigServer.stop();
           WiFi.mode(WIFI_STA);
           WiFi.enableAP(false);
           WiFi.enableSTA(true);
           WiFi.onEvent(iotConfigWiFiEvent);
           Serial.println();
           Serial.println();
           Serial.print("Connecting to ");
           Serial.println(wifiClientSSID);
    
           WiFi.begin(wifiClientSSID, wifiClientPassword);
           iotConfigMode=iotConfigWiFiTestWaitConnect;
           clientConnectTime = iotConfigCurrentMillis;
           break;

      case iotConfigWiFiTestWaitConnect:
           if (iotConfigOnline)
           {
              saveAndReboot();
           } else if (iotConfigCurrentMillis > (clientConnectTime + 15000))
           {
              reboot();
           }
           break;
   }
   return (iotConfigMode == iotConfigClientMode);
}

String iotConfig::queryToAscii(String queryString)
{
   String decodedString="";
   for (int i=0; i<queryString.length(); i++)
   {
      if (queryString[i]=='%')
      {
         char hexVal=strtol((queryString.substring(i+1,i+3)).c_str(), NULL, 16);
         //Serial.print("hexVal=");
         //Serial.println(hexVal);
         decodedString=decodedString+hexVal;
         i+=2;
      }
      else
      {
         decodedString=decodedString+queryString[i];
      }
   }
   return decodedString;
}

String iotConfig::getQueryParam(String queryString, String paramName)
{
   int qmIndex = queryString.indexOf("?");
   if (qmIndex > -1)
   {
      queryString.remove(0, qmIndex+1);
   }

   int lastIndex;
   do {
      lastIndex = queryString.indexOf("&");
      String param;
      if (lastIndex >= 0)
      {
         param = queryString.substring(0, lastIndex);
         queryString.remove(0, lastIndex + 1);
      }
      else
      {
         param = queryString;
      }
      int eqIndex = param.indexOf("=");
      if (eqIndex >= 0)
      {
         if (param.substring(0, eqIndex)==paramName)
         {
            return String(param.substring(eqIndex+1));
         }
      }
   } while (lastIndex>=0);
   return String();
}

char *iotConfig::getFriendlyName()
{
   return friendlyName;
}

char *iotConfig::getSSID()
{
   return wifiClientSSID;
}

IPAddress iotConfig::getIP()
{
   return WiFi.localIP();
}

bool iotConfig::isOnline()
{
  return iotConfigOnline;
}






String queryToAscii(String queryString)
{
   String decodedString="";
   for (int i=0; i<queryString.length(); i++)
   {
      if (queryString[i]=='%')
      {
         char hexVal=strtol((queryString.substring(i+1,i+3)).c_str(), NULL, 16);
         //Serial.print("hexVal=");
         //Serial.println(hexVal);
         decodedString=decodedString+hexVal;
         i+=2;
      }
      else
      {
         decodedString=decodedString+queryString[i];
      }
   }
   return decodedString;
}

String getQueryParam(String queryString, String paramName)
{
   int qmIndex = queryString.indexOf("?");
   if (qmIndex > -1)
   {
      queryString.remove(0, qmIndex+1);
   }

   int lastIndex;
   do {
      lastIndex = queryString.indexOf("&");
      String param;
      if (lastIndex >= 0)
      {
         param = queryString.substring(0, lastIndex);
         queryString.remove(0, lastIndex + 1);
      }
      else
      {
         param = queryString;
      }
      int eqIndex = param.indexOf("=");
      if (eqIndex >= 0)
      {
         if (param.substring(0, eqIndex)==paramName)
         {
            return String(param.substring(eqIndex+1));
         }
      }
   } while (lastIndex>=0);
   return String();
}

