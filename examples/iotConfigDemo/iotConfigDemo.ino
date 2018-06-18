#include <WiFi.h>
#include "iotconfig.hpp"

unsigned long currentMillis=0;
unsigned long nextEvent=0;

iotConfig ic;
int merker1;
int merker2;
int merker3;

WiFiServer server(8080);

void setup()
{
    Serial.begin(115200);

    delay(10);
    // We start by connecting to a WiFi network
  
    ic.begin("iotesp32", "admin", 100, 10, 10000);
    ic.setWiFiClientWatchDogTimeout(10000);
    ic.recoveryChanceWait();

    merker1=100;
    ic.assignVariableEEPROM((uint8_t*)&merker1, sizeof(int));
    ic.assignVariableEEPROM((uint8_t*)&merker2, sizeof(int));
    ic.assignVariableRTCDATA((uint8_t*)&merker3, sizeof(int));
    merker1++;
    merker2--;
    merker3+=10;
    Serial.print("merker1: ");
    Serial.println(merker1);
    Serial.print("merker2: ");
    Serial.println(merker2);
    Serial.print("merker3: ");
    Serial.println(merker3);
    server.begin();

    //ic.factoryReset();
}

void loop(){
  bool configured = ic.handle();

  currentMillis=millis();
  
  if (currentMillis > nextEvent)
  {
     if (!configured)
     {
        Serial.println("Device not configured");
     }
     else
     {
        Serial.print("Configured: SSID: ");
        Serial.print(ic.getSSID());
        Serial.print(", FriendlyName: ");
        Serial.print(ic.getFriendlyName());
        Serial.print(", IP: ");
        Serial.println(ic.getIP());
     }
     nextEvent += 5000;
  }


  static WiFiClient client;
  static IPAddress remoteIPaddr;
  static String currentLine = "";
  static bool closeConn = false;
  static unsigned long clientConnectTime = 0;
  const unsigned long clientTimeOut = 2000;

  if (client)
  {
     if (client.connected() && (!closeConn || client.available()) && (currentMillis < (clientConnectTime+clientTimeOut)))
     {
        if (client.available())
        {
           char c = client.read();
           
           if (c == '\n') 
           {
              if ((currentLine.length() == 0))
              { 
                 client.println();
                 closeConn=true;
              } else {
                 currentLine = "";
              }
           } else if (c != '\r')
           {
              currentLine += c;
           }

           if ( currentLine.startsWith("GET /api/v1/test") &&
                currentLine.endsWith(" HTTP")
              )
           {
              Serial.println("Requested test");
              remoteIPaddr = client.remoteIP();
              
              currentLine.remove(0,16);
              currentLine.remove(currentLine.length()-5,5);

              String sessionid = queryToAscii(getQueryParam(currentLine,"sessionid"));
              String myname = queryToAscii(getQueryParam(currentLine,"name"));
             
              Serial.println("sessionid: " + sessionid);
              Serial.println("myname: " + myname);
              Serial.print("remote: ");
              Serial.println(remoteIPaddr);
              
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type: application/json");
              client.println();
              client.println("{");
              client.println("   \"yeah\": true");
              client.println("}");
              closeConn = true;
           }
        }
     }
     else
     {
        Serial.println("Connection closed");
        clientConnectTime = currentMillis;
        closeConn = false;
        client.stop();
        client = NULL;
     }
  }
  else
  {
     client = server.available();   // listen for incoming clients
     currentLine = "";
     clientConnectTime = currentMillis;
  }
}

