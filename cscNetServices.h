/*
	CSC's Common Network Services Library

	Description: Private library for common network services used in my apps. Not designed for more generalized use.

	Creation Date: 2020-05-16

	Author: Curtis Collins ... with much copied from others

  Todo:
  
     - Migrate off TimeLib to NTPClient
	 
*/

#ifdef ESP8266 
   #include <ESP8266WiFi.h> 
   #include <ESP8266mDNS.h>
#endif

#ifdef ESP32 
  #include <WiFi.h> 
  #include <ESPmDNS.h>
#endif


#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TimeLib.h> 
#include <NTPClient.h>
#include <ArduinoOTA.h>


#define MSGBUFFLEN 300
#define WIFIRETRYCNT 5
#define WIFIRETRYDELAY 5000

#define WIFIREBOOT true

#define MQTTRETRYDELAY 5000
#define MQTTRETRYCOUNT 3

#define ALLNODES 255

bool debug = true;
bool ota_started;

const char* pssid = "COWIFI151420636/0";
const char* ppwd = "WiFi-89951645";
const char* assid = "Bodega";
const char* apwd = "Sail2012";
const char* otaPwd = "Sail2012";

bool wifiTryAlt = false;

int mqttPort = 1883;
int hostEntry = -1;
int msgn = 0;

const char* mqttTopic = "(empty)";
const char* mqttTopicData = "(empty)";
const char* mqttTopicCtrl = "(empty)";
const char* mqttUid = " ";
const char* mqttPwd = " ";

char nodeName[24] = "";
char msgbuff[MSGBUFFLEN] = "";

WiFiClient wifiClient;

PubSubClient mqttClient(wifiClient);
WiFiUDP Udp;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

unsigned int localPort = 8888;  // local port to listen for UDP packets

//
// NTP Servers defs
//

//static const char ntpServerName[] = "us.pool.ntp.org";
static const char ntpServerName[] = "time.nist.gov";
//static const char ntpServerName[] = "time-a.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-b.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-c.timefreq.bldrdoc.gov";

//
// Time Zone
//

int timeZone = 0;

//
// General fucntions
//

String timestampString()
{
	String timestamp = "";
	char time24[11] = "hh:mm:ss: ";

	msgn = sprintf(time24, "%02i:%02i:%02i: ", hour(), minute(), second());

	//timestamp = String(year()) + "-" + String(month()) + "-" + day() + " " + hour() + ":" + minute() + ":" + second();
	timestamp = String(year()) + "-" + String(month()) + "-" + day() + " " + String(time24) + " ";

	return(timestamp);

}

void outputMsg(char* msg)
{
    Serial.println();
    Serial.print(timestampString());
	Serial.print(nodeName);
	Serial.print(" ");
	Serial.println(msg);
	Serial.flush();
	msg = "";
}

//
// WiFi Services
//

void connectWiFi()
{
	int retryCnt = 0;
	bool wifiAltTried = false;
	const char* ssid = pssid;
    const char* pwd = ppwd;

//	WiFi.hostname(nodeName);
	WiFi.mode(WIFI_STA);
	Serial.print(F("\nWiFi connecting to "));
	Serial.println(ssid);
	WiFi.begin(ssid, pwd);
	while ((WiFi.status() != WL_CONNECTED) && (retryCnt < WIFIRETRYCNT))
	{
		delay(WIFIRETRYDELAY);
		retryCnt++;
		if ((retryCnt >= WIFIRETRYCNT) && wifiTryAlt && !wifiAltTried)
		{
		    ssid = assid;
			pwd  = apwd;
			retryCnt = 0;
			Serial.print(F("\nWiFi connecting to alt SSID... "));
			Serial.println(ssid);
			WiFi.begin(ssid, pwd);
			wifiAltTried = true;
		};
	}

	if (WiFi.status() == WL_CONNECTED)
	{
		Serial.print(F("\nWiFi connected to "));
		Serial.print(ssid);
		Serial.print(F(" "));
		Serial.println(WiFi.localIP());
	}
	else
	{
		if (WIFIREBOOT)
		{
			Serial.println(F("WiFi not connected, rebooting..."));
			ESP.restart();
		}
		else
		{
			Serial.println(F("WiFi not connected."));
		}
	}
}

//
// mDNS Services
//

bool setupMdns(char* nodeName)
{
    Serial.println("\nmDNS Setup...");
	if (!MDNS.begin(nodeName))
	{
		Serial.println("Error setting up MDNS responder!");
		return(false);
	}
	return(true);
}

int findService(char* serviceType, char* protocol)
{
	int n = 0;
	n = MDNS.queryService(serviceType, protocol); // Send out query for esp tcp services
	if (n == 0)
	{
		Serial.println("no services found");
	}
	else
	{
		msgn = snprintf(msgbuff, MSGBUFFLEN, "%i service(s) found, service 0: ", n);
		Serial.print(msgbuff);
		Serial.print(MDNS.hostname(0));
		Serial.print(" ");
		Serial.print(MDNS.IP(0));
		Serial.print(":");
		Serial.println(MDNS.port(0));
		return (0); // return index to 1st service only
	}
	return(-1);  // no service found
}

//
// MQTT Services
//


bool subscribeMQTT(const char* topic)
{
    bool rc = false;
	
	if (!mqttClient.subscribe(topic))
	{
		Serial.print("\nMQTT subscription to ");
		Serial.print(topic);
		Serial.println(" failed!");
		
	}
	else
	{
	   Serial.print("\nMQTT mqttTopic subscribed: ");
	   Serial.print(topic);
	   rc = true;
	}
	
	return(rc);

}

void connectMQTT(bool subscribe, const char* topic, IPAddress server)
{
	int retry_cnt = 0;
	bool rc = false;
	
	Serial.print("\nConnecting to MQTT Server: ");
	Serial.println(server);
	while (!mqttClient.connected()) {

		if (mqttClient.connect(nodeName))
		{
			Serial.println("MQTT connected");
			if (subscribe)
			{
                rc = subscribeMQTT(topic);
				if (!rc) 
				{
				msgn = snprintf(msgbuff, MSGBUFFLEN, "\nSubscription to MQTT Topic %s failed", topic);
				Serial.println(msgbuff);
				}
				
			}

			retry_cnt = 0;
		}
		else
		{
			Serial.print("MQTT connect failed,");
			Serial.print(mqttClient.state());
			Serial.println(" try again in 5 seconds");
			delay(MQTTRETRYDELAY);
			retry_cnt++;
		}

		if ((retry_cnt > MQTTRETRYCOUNT) || (subscribe && (!rc)))
		{
			Serial.println();
			Serial.println("Rebooting...");
			ESP.restart();
		}
	}
}

bool setupMQTT(IPAddress mqttserver, int mqttPort, bool mqttsubscribe, const char* topic, std::function<void(char*, uint8_t*, unsigned int)> callback)
{
	Serial.println();
	Serial.println("MQTT Setup... ");
	mqttClient.setServer(mqttserver, mqttPort);
	mqttClient.setCallback(callback);
	connectMQTT(mqttsubscribe, topic, mqttserver);
	if (mqttClient.state() != MQTT_CONNECTED)
	{
	 Serial.println("MQTT NOT connected!");
	 return(false);
	}
	else
	 {
	   return(true);
	 };
	    
  return(false);
}

bool publishMQTT(const char* topic, const char* payload)
{
  int retryCnt = 0;
  bool mqttSent = false;
  
  while (!mqttClient.publish(topic, payload) && (retryCnt < MQTTRETRYCOUNT))
  {
    if (debug) Serial.println("MQTT Published failed, retrying....");
    retryCnt++;
    delay(MQTTRETRYDELAY);
  }  
  
  if (retryCnt < MQTTRETRYCOUNT) mqttSent = true;
  
  return(mqttSent);
  
}

//
// OTA Services
//

void StartOTAIfRequired()
{
	if (ota_started)
		return;
	// Port defaults to 8266
	ArduinoOTA.setPort(8266);
	// Hostname defaults to esp8266-[ChipID]
	//if (ArduinoOTA.getHostname() && ArduinoOTA.getHostname().length())

	// No authentication by default
	ArduinoOTA.setPassword((const char*)"123");
	ArduinoOTA.onStart([]() {
		Serial.println("OTA Start");
		});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nOTA End");
		});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
		});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
		});
	ArduinoOTA.begin();
	ota_started = true;
	delay(500);
}

void HandleOTA()
{
	StartOTAIfRequired();
	ArduinoOTA.handle();
}

  
//
// NTP Services
//

bool setupNTP(int timeZone)     // accepts TimeZone in hours and converts to seconds as expected by NTPClient.setTimeOffset
{
	Serial.println("\nNTP Setup...\nwaiting for NTP server sync");
	timeClient.begin();
	timeClient.setTimeOffset(timeZone * 3600);
	Serial.print("Time Zone Set to "); Serial.println(timeZone);
	if (timeClient.update()) 
	{ 
       Serial.print("\nNTP time set to: "); 
	   Serial.println(timeClient.getFormattedTime());
	   return(true);
	} 
	  else 
	  { 
	     Serial.println("\nNTP time NOT set");
	     return(false);
	  }
	
	return(true);
}
