#ifndef ESP8266
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_wpa2.h"
#endif
#include "iotconfig.hpp"

#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif

IPAddress iotConfigApIP(192, 168, 4, 1);
DNSServer iotConfigDnsServer;
WiFiServer iotConfigServer(80);
WiFiClient iotConfigClient;

#ifdef ESP8266
uint8_t rtc4[5] = "xxxx";
uint8_t firstBoot = 1;
uint8_t iot_rtc_data[IOT_RTC_DATA_SIZE];
#else
RTC_DATA_ATTR uint8_t firstBoot = 1;
RTC_DATA_ATTR uint8_t iot_rtc_data[IOT_RTC_DATA_SIZE];
#endif

unsigned long iotConfigCurrentMillis=0;
static bool iotConfigOtaPrio = false;
static bool iotConfigOnline = false;
static unsigned long iotConfigWifiLossTS=0;
static bool iotConfigResetState = false;

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
#ifdef ESP8266
   ESP.rtcUserMemoryRead(0, (uint32_t*)rtc4, (size_t)4);
   if (strncmp((const char*)rtc4, "init", 4)!=0) {
      firstBoot = 0;
      ESP.rtcUserMemoryRead(4, (uint32_t*)iot_rtc_data, IOT_RTC_DATA_SIZE);
   }
#endif
}

iotConfig::~iotConfig()
{
}


#ifdef ESP8266
#define EVENT_STA_GOT_IP     const WiFiEventStationModeGotIP&
#define EVENT_STA_DISCONNECT const WiFiEventStationModeDisconnected&
#define EVENT_AP_CONNECT     const WiFiEventSoftAPModeStationConnected&
#else
#define EVENT_STA_GOT_IP     void
#define EVENT_STA_DISCONNECT void
#define EVENT_AP_CONNECT     void
#endif

void onStaGotIP(EVENT_STA_GOT_IP) {
   Serial.println("WiFi connected");
   Serial.println("IP address: ");
   Serial.println(WiFi.localIP());
   iotConfigOnline=true;
}

void onStaDisconnect(EVENT_STA_DISCONNECT) {
   Serial.println("WiFi lost connection");
   iotConfigOnline=false;
   iotConfigWifiLossTS=iotConfigCurrentMillis;
}

void onApConnected(EVENT_AP_CONNECT) {
   iotConfigResetState=true;
}

#ifdef ESP8266
WiFiEventHandler  onStaGotIPHandler;
WiFiEventHandler  onStaDisconnectHandler;
WiFiEventHandler  onApConnectedHandler;
#else

static void iotConfigWiFiEvent(WiFiEvent_t event)
{
   Serial.printf("[WiFi-event] event: %d\n", event);

   switch(event)
   {
      case SYSTEM_EVENT_STA_GOT_IP:
          onStaGotIP();
          break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
      case SYSTEM_EVENT_STA_STOP:
      case SYSTEM_EVENT_STA_LOST_IP:
      case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
          onStaDisconnect();
          break;
      case SYSTEM_EVENT_AP_STACONNECTED:
      case SYSTEM_EVENT_AP_STAIPASSIGNED:
          onApConnected();
          break;
      default:
          break;
   }
}
#endif

bool iotConfig::begin(const char *deviceName, const char *initialPasswordN,
                      const size_t eepromSizeN, const size_t rtcDataSizeN, const uint16_t coldBootAPtime)
 
{
#ifndef ESP8266
   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
   esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
#endif

   if (rtcDataSizeN > IOT_RTC_DATA_SIZE)
   {
      return false;
   }
   eepromSize=eepromSizeN+
              sizeof(eepromCRC)+
              sizeof(friendlyName)+
              sizeof(wifiApPassword)+
              sizeof(wifiClientSSID)+
              sizeof(wifiClientUsername)+
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
   assignVariableEEPROM((uint8_t*)&wifiClientUsername, sizeof(wifiClientUsername));
   assignVariableEEPROM((uint8_t*)&wifiClientPassword, sizeof(wifiClientPassword));
   assignVariableEEPROM((uint8_t*)&otaPassword, sizeof(otaPassword));

#ifdef ESP8266
   onStaGotIPHandler      = WiFi.onStationModeGotIP(onStaGotIP);
   onStaDisconnectHandler = WiFi.onStationModeDisconnected(onStaDisconnect);
   onApConnectedHandler   = WiFi.onSoftAPModeStationConnected(onApConnected);
#else
   WiFi.onEvent(iotConfigWiFiEvent);
#endif
   if ((strlen(otaPassword)>0) && ((!firstBoot)||(coldBootAPtime==0)))
   {
      Serial.println();
      Serial.println();
      Serial.print("Connecting to ");
      Serial.println(wifiClientSSID);

      reconnect();
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

void iotConfig::reconnect() {
   WiFi.disconnect();
   if (strlen(wifiClientUsername) == 0) {
      // WPA(2)-PSK / WEP
      WiFi.mode(WIFI_STA);
#ifndef ESP8266
      WiFi.setHostname(friendlyName);
#endif
      WiFi.begin(wifiClientSSID, wifiClientPassword);
   } else {
      // WPA(2)-Enterprise
#ifndef ESP8266
      WiFi.mode(WIFI_STA);
      WiFi.mode(WIFI_STA); // init wifi mode
      esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)wifiClientUsername, strlen(wifiClientUsername));
      esp_wifi_sta_wpa2_ent_set_username((uint8_t *)wifiClientUsername, strlen(wifiClientUsername));
      esp_wifi_sta_wpa2_ent_set_password((uint8_t *)wifiClientPassword, strlen(wifiClientPassword));
      esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT(); //set config settings to default
      esp_wifi_sta_wpa2_ent_enable(&config); // set config settings to enable function
      WiFi.setHostname(friendlyName);
#endif
      WiFi.begin(wifiClientSSID); // connect to wifi
   }
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
     });
   ArduinoOTA
     .onEnd([]() {
       Serial.println("\nEnd");
       iotConfigOtaPrio = false;
     });
   ArduinoOTA
     .onProgress([](unsigned int progress, unsigned int total) {
       Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
     });
   ArduinoOTA
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
#ifdef ESP8266
   ESP.rtcUserMemoryWrite(4, (uint32_t*)iot_rtc_data, IOT_RTC_DATA_SIZE);
   ESP.deepSleep(1000000ULL*2);   
#else
   esp_deep_sleep(1000000ULL*2);   
#endif
}

void iotConfig::saveAndReboot()
{
   updateRTCDATA();
#ifdef ESP8266
   ESP.rtcUserMemoryWrite(4, (uint32_t*)iot_rtc_data, IOT_RTC_DATA_SIZE);
   ESP.deepSleep(1000000ULL*2);   
#endif
   updateEEPROM();
   EEPROM.commit();
#ifndef ESP8266
   esp_deep_sleep(1000000ULL*2);   
#endif
}

bool iotConfig::handle()
{
   iotConfigCurrentMillis = millis();
   static unsigned long iotConfigReconnectTS = 0;
   
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
              if ((iotConfigOnline) && (!otaInitialized))
              {
                 arduinoOTAsetup(friendlyName, otaPassword);
                 otaInitialized = true;
              }
           }
           
           if ((!iotConfigOnline) && (iotConfigCurrentMillis > iotConfigReconnectTS + watchDogTimeout/2 + WIFI_CONNECT_TIME))
           {
              iotConfigReconnectTS = iotConfigCurrentMillis;
              reconnect();
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
                          String wpaTypes[] = { "OPEN", "WEP", "WPA-PSK", "WPA2-PSK", "WPA/WPA2-PSK","WPA2-Enterprise" };
                          apExpireTime=iotConfigCurrentMillis + 60000;
                          iotConfigClient.println("HTTP/1.1 200 OK");
                          iotConfigClient.println("Content-type:text/html");
                          iotConfigClient.println();
                          iotConfigClient.print("<!DOCTYPE html><html><head><title>CaptivePortal</title>");
                          if (iotConfigResetState) {
                            iotConfigResetState = false;
                            iotConfigServerState = iotConfigScanSSIDs;
                          }
                          if (iotConfigServerState==iotConfigScanSSIDs)
                          {
                             iotConfigClient.print("<META HTTP-EQUIV=\"refresh\" CONTENT=\"6\">");
                          }
                          iotConfigClient.print("</head><body><b>");
                          iotConfigClient.print(friendlyName);
                          iotConfigClient.print(" device configuration</b><br>");
                          iotConfigClient.print("MAC-Address: ");
                          iotConfigClient.print(WiFi.macAddress());
                          iotConfigClient.print("<br><br>");
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
                                      iotConfigClient.println("<table><tr>");
                                      iotConfigClient.println("<th>SSID</th>");
                                      iotConfigClient.println("<th>Power</th>");
                                      iotConfigClient.println("<th>Encryption</th>");
                                      iotConfigClient.println("</tr>");
                                      for (int i = 0; i < numScannedNetworks; ++i) {
                                          iotConfigClient.println("<tr>");
                                          // Print SSID and RSSI for each network found
                                          iotConfigClient.print("<td><a href=\"/join/");
                                          iotConfigClient.print(i + 1);
                                          iotConfigClient.print("\">");
                                          iotConfigClient.print(WiFi.SSID(i));
                                          iotConfigClient.print(" </a></td><td>");
                                          iotConfigClient.print(WiFi.RSSI(i));
                                          iotConfigClient.print(" dB</td><td>");
                                          iotConfigClient.println(wpaTypes[WiFi.encryptionType(i)]);
                                          iotConfigClient.println("</td>");
                                          iotConfigClient.println("</tr>");
                                      }
                                      iotConfigClient.println("</table>");
                                  }
                                  iotConfigClient.print("<br><br><a href=\"/reset\">Factory reset</a><br>");
                                  iotConfigClient.print("<br><a href=\"/recovery\">Firmware recovery / unbrick</a><br>");
                                  break;
                                  
                             case iotConfigJoinForm:
                                  iotConfigClient.print("Logging into WiFi <b>");
                                  iotConfigClient.print(WiFi.SSID(joinedNetworkIndex-1));
                                  iotConfigClient.println("</b><br><br>");
                                  iotConfigClient.print("<form method=\"get\" onsubmit=\"javascript:document.location='/login.cgi' + $('pass') + '';\">");
                                  switch (WiFi.encryptionType(joinedNetworkIndex-1))
                                  {
#ifdef ESP8266
                                     case ENC_TYPE_WEP:
                                     case ENC_TYPE_TKIP:
                                     case ENC_TYPE_CCMP:
                                     case ENC_TYPE_AUTO:
#else
                                     case WIFI_AUTH_WEP:
                                     case WIFI_AUTH_WPA_PSK:
                                     case WIFI_AUTH_WPA2_PSK:
                                     case WIFI_AUTH_WPA_WPA2_PSK:
#endif
                                          iotConfigClient.print("WiFi PSK-Key: ");
                                          iotConfigClient.print("<input type=\"password\" name=\"pass\" id=\"pass\" /><br>");
                                          break;
#ifndef ESP8266
                                     case WIFI_AUTH_WPA2_ENTERPRISE:
                                          iotConfigClient.print("WiFi EAP Identity: ");
                                          iotConfigClient.print("<input type=\"text\" name=\"ident\" id=\"ident\" /><br>");
                                          iotConfigClient.print("WiFi EAP Password: ");
                                          iotConfigClient.print("<input type=\"password\" name=\"pass\" id=\"pass\" /><br>");
#endif
                                          break;
                                     default:
                                          break;
                                  }
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
                                  iotConfigClient.print("<br>1) Enter the OTA password and click 'ok'");
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
                          String decodedUsernameString=queryToAscii(getQueryParam(currentLine,"ident"));
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
                             memset((char*)wifiClientSSID, 0, sizeof(wifiClientSSID));
                             memset((char*)wifiClientUsername, 0, sizeof(wifiClientUsername));
                             memset((char*)wifiClientPassword, 0, sizeof(wifiClientPassword));
                             strncpy(wifiClientSSID, WiFi.SSID(joinedNetworkIndex-1).c_str(), sizeof(wifiClientSSID));
                             strncpy(wifiClientUsername, decodedUsernameString.c_str(), sizeof(wifiClientUsername));
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
#ifndef ESP8266
                 iotConfigClient = NULL;
#endif
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
#ifdef ESP8266
           onStaGotIPHandler      = WiFi.onStationModeGotIP(onStaGotIP);
           onStaDisconnectHandler = WiFi.onStationModeDisconnected(onStaDisconnect);
           onApConnectedHandler   = WiFi.onSoftAPModeStationConnected(onApConnected);
#else
           WiFi.onEvent(iotConfigWiFiEvent);
#endif
           Serial.println();
           Serial.println();
           Serial.print("Connecting to ");
           Serial.println(wifiClientSSID);
    
           reconnect();

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
      default:
           break;
   }
   return isOnline();
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
      else if (queryString[i]=='+')
      {
         decodedString=decodedString+" ";
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
      else if (queryString[i]=='+')
      {
         decodedString=decodedString+" ";
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

