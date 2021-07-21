/*
	CSC's Common Network Services Library

	Description: Private library for common network services used in my apps. Not designed for more generalized use.

	Creation Date: 2020-05-16

	Author: Curtis Collins ... with much copied from others

	2020-11-11 C. Collins, added OTA services
	2020-11-12 C. Colllins, migrated to github

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


#define MSGBUFFLEN 500
#define WIFIRETRYCNT 5
#define WIFIRETRYDELAY 5000

#define WIFIREBOOT true

#define MQTTRETRYDELAY 5000
#define MQTTRETRYCOUNT 3

#define ALLNODES 255

bool debug = false;
bool ota_started;

const char* pssid;
const char* ppwd;
const char* assid;
const char* apwd;
const char* otaPwd;

bool wifiTryAlt = false;

int mqttPort = 1883;
int hostEntry = -1;
int msgn = 0;

//const char* mqttTopic;
const char* mqttTopicData;
const char* mqttTopicCtrl;
const char* mqttUid;
const char* mqttPwd;

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

	//timestamp = String(year()) + "-" + String(month()) + "-" + day() + " " + hour() + ":" + minute() + ":" + second();
	timestamp = String(year()) + "-" + String(month()) + "-" + day() + " " + timeClient.getFormattedTime() + " ";

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

void showWiFiInfo()
{
	snprintf(msgbuff, MSGBUFFLEN, "\nWiFi Config:\n\npssid = %s\nassid = %s\nWIFIRETRYCNT = %i\nWIFIRERYDELAY = %i\nwifiTryAlt = %i\n\n", pssid, assid, WIFIRETRYCNT, WIFIRETRYDELAY, wifiTryAlt);
	Serial.print(msgbuff);
}

void connectWiFi()
{
	int retryCnt = 0;
	bool wifiAltTried = false;
	const char* ssid = pssid;
	const char* pwd = ppwd;

	showWiFiInfo();
	WiFi.mode(WIFI_STA);
	Serial.print(F("\nWiFi connecting to "));
	Serial.println(ssid);
	WiFi.begin(ssid, pwd);
	while ((WiFi.status() != WL_CONNECTED) && (retryCnt < WIFIRETRYCNT))
	{
		delay(WIFIRETRYDELAY);
		retryCnt++;
		Serial.print(F("\nWiFi retry count = "));
		Serial.println(retryCnt);
		if ((retryCnt >= WIFIRETRYCNT) && wifiTryAlt && !wifiAltTried)
		{
			ssid = assid;
			pwd = apwd;
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
		Udp.begin(localPort);
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

void startOTA()
{
	if (ota_started)
		return;
	// Port defaults to 8266
	ArduinoOTA.setPort(8266);
	// Hostname defaults to esp8266-[ChipID]
	//if (ArduinoOTA.getHostname() && ArduinoOTA.getHostname().length())

	// No authentication by default
	ArduinoOTA.setPassword(otaPwd);
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

void handleOTA()
{
	startOTA();
	ArduinoOTA.handle();
}


//
// NTP Services
//

// const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

//IPAddress timeServer(132, 163, 4, 101); // time-a.timefreq.bldrdoc.gov
 IPAddress timeServer(132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov
// IPAddress timeServer(132, 163, 4, 103); // time-c.timefreq.bldrdoc.gov

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:                 
	Udp.beginPacket(address, 123); //NTP requests are to port 123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}

time_t getNtpTime()
{
	while (Udp.parsePacket() > 0); // discard any previously received packets
	Serial.println("Transmit NTP Request");
	sendNTPpacket(timeServer);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 1500) {
		int size = Udp.parsePacket();
		if (size >= NTP_PACKET_SIZE) {
			Serial.println("Receive NTP Response");
			Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;
			// convert four bytes starting at location 40 to a long integer
			secsSince1900 = (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
		}
	}
	Serial.println("No NTP Response :-(");
	return 0; // return 0 if unable to get the time
}



bool setupNTP(int timeZone)     // accepts TimeZone in hours and converts to seconds as expected by NTPClient.setTimeOffset
{


	Serial.println("try TimeLib sync start");
	setSyncProvider(getNtpTime);
	if (timeStatus() != timeNotSet) Serial.println("NTP time not set");
	Serial.println("try TimeLib sync end");

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

	return(false);
}
