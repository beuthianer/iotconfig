#ifndef IOTCONFIG_H
#define IOTCONFIG_H IOTCONFIG_H

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define IOT_RTC_DATA_SIZE 64
#define WIFI_CONNECT_TIME 10000

extern unsigned long iotConfigCurrentMillis;

typedef struct
{
  uint8_t *varPtr;
  int nvIndex;
  size_t allocSize;
} memAllocation_t;

String queryToAscii(String queryString);
String getQueryParam(String queryString, String paramName);

class iotConfig
{
   public:
      iotConfig();
      ~iotConfig();
      bool begin(const char *deviceName, const char *initialPasswordN,
                 const size_t eepromSizeN, const size_t rtcDataSizeN, const uint16_t coldBootAPtime);
      void setWiFiClientWatchDogTimeout(const uint32_t timeoutMS);
      void recoveryChanceWait();
      bool assignVariableEEPROM(uint8_t *pointer, const size_t varSize);
      bool assignVariableRTCDATA(uint8_t *pointer, const size_t varSize);
      void factoryReset();
      void updateEEPROM();
      void updateRTCDATA();
      void reboot();
      void saveAndReboot();
      void reconnect();
      bool handle();
      bool isOnline();
      char *getFriendlyName();
      char *getSSID();
      IPAddress getIP();

   private:
      bool addVariableInfo(memAllocation_t **store,
                           int *indexPtr,
                           memAllocation_t *info);
      uint32_t calcCRC();
      String queryToAscii(String queryString);
      String getQueryParam(String queryString, String paramName);
      void arduinoOTAsetup(const char *friendlyName, const char *otaPassword);

      enum {iotConfigNoneMode, iotConfigServerMode, iotConfigClientMode, iotConfigTestWiFi, iotConfigWiFiTestWaitConnect} iotConfigMode;
      enum {iotConfigScanSSIDs, iotConfigShowSSIDs, iotConfigJoinForm, iotConfigResetForm, iotConfigRecoveryForm, iotConfigError} iotConfigServerState;
      enum {iotConfigErrorTypo, iotConfigErrorNoName, iotConfigErrorWrongPassword} iotConfigErrorType;
      int numScannedNetworks;
      int joinedNetworkIndex;

      char friendlyName[32];
      char wifiApPassword[32];
      char wifiClientSSID[32];
      char wifiClientUsername[32];
      char wifiClientPassword[32];
      char otaPassword[32];
      unsigned long clientConnectTime;
      bool closeConn;
      unsigned long clientTimeOut;
      unsigned long apExpireTime;
      unsigned long watchDogTimeout;
      bool otaInitialized;
      String currentLine;

      uint16_t bootUps;
      uint32_t eepromCRC;
      size_t eepromSize;
      size_t eepromAssignPointer;
      size_t rtcDataSize;
      size_t rtcDataAssignPointer;

      memAllocation_t *eepromAllocData;
      memAllocation_t *rtcAllocData;
      int eepromDataIndex;
      int rtcDataIndex;
};

#endif
