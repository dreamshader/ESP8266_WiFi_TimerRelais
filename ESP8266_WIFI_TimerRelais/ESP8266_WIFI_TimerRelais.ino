//
// ************************************************************************
// Use an ESP8266 modul as a WiFi switch
// (C) 2016 Dirk Schanz aka dreamshader
// ************************************************************************
// 41148
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// ************************************************************************
//
//   REMEMBER TO SET MATCHING VALUES FOR YOUR WLAN ACCESS!
//   On the two lines around 104 and below set
//   #define FACTORY_WLAN_SSID to YOUR SSID
//   #define FACTORY_WLAN_PASSPHRASE to YOUR PASSPHRASE for access
//
//   If using a PCF8574 as portexpander, set ESP_HAS_PCF8574 (see below)
//   If you have directly connected GPIO00/GPIO02 as I/Os unsing e.g.
//   a transitor as an amplifier, unset ESP_HAS_PCF8574.
//   In addition, you have to take care whether your logic is active HIGH
//   or active LOW ( as in case of the PCF8574 ). For that you have to
//   set RELAIS_ON and RELAIS_OFF to the proper values.

//   The module connects to the specified WLAN as a WiFi-client. After
//   that, it requests the current date an time via network time protocol
//   from a specific NTP-server.
//   Last step in starting up is to create a http-server listening to a
//   specified port for requests.
//   The web server handles a configuration page for up to eight channels.
//   Each channel may be switched on and off on up to two user defined
//   times.
//   In addition for each channel there are two flags to allow switching
//   on or off using the http-API.
//   There are three modes for each channel. ON and OFF override the
//   user defined times if their check-boxes are active.
//   AUTO activates the user-defined times for each channel and time
//   that is activated ( checked ).
//   These setting information are stored in the EEPROM and restored
//   on every reboot of the module.
//
// ************************************************************************
//
//   Current hardware is a witty esp developer board:
//   https://blog.the-jedi.co.uk/2016/01/02/wifi-witty-esp12f-board/
//
//   Select "NodeMCU 1.0 (ESP-12E Module)" as board in the Arduino IDE
//
//   fyi: witty pins for integrated RGB LED
//     const int RED = 15;
//     const int GREEN = 12;
//     const int BLUE = 13;
//   if using such a board, set IS_WITTY_MODULE
//
// ************************************************************************
//
//
//-------- History --------------------------------------------------------
//
// 2016/10/28: initial version
// 2016/12/08: added flag handling
// 2016/12/12: added functions for web-update
// 2016/12/18: several cleanups and styling changes
// 2016/12/18: added some verifications on setup page
// 2016/12/18: optimized for size (must be less than 50% of an ESP-01)
// 2016/12/18: added Web-API
// 2016/12/18: UDP client and server functionality for multicasting
// 2016/12/22: EXT1 and EXT2 handling on multicast request added
// 2016/12/22: EXT1 and EXT2 calls in Web-API too
//
//
// ************************************************************************
//
// ---- suppress debug output to serial line ------------------------------
// set beQuiet = no debug output
//
#define BE_QUIET			false
bool beQuiet;
//
// If you are using a witty module, define this to use the RGB-LED 
// as a status LED
#define IS_WITTY_MODULE
// #undef IS_WITTY_MODULE
//
//
// ************************************************************************
// include necessary lib header
// ************************************************************************
//
#include <ESP8266WiFi.h>        // WiFi functionality
#include <ESP8266WebServer.h>   // HTTP server
#include <TimeLib.h>            // time keeping feature
#include <WiFiUdp.h>            // udp for network time
#include <Timezone.h>           // for adjust date/time to local timezone
// https://github.com/JChristensen/Timezone
#include "PCF8574.h"
#include <Wire.h>
#include <Hash.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

// #define __ASSERT_USE_STDERR     //
// #include <assert.h>             // for future use


#include "dsEeprom.h"           // simplified access to onchip EEPROM
#include "SimpleLog.h"          // fprintf()-like logging

//
// ************************************************************************
// defines for default settings
// ************************************************************************
//
// set DEFAULT_WLAN_SSID and DEFAULT_WLAN_PASSPHRASE to match
// your network
//
#define DEFAULT_UDP_LISTENPORT		8888
#define DEFAULT_WLAN_SSID               ""
#define DEFAULT_WLAN_PASSPHRASE         ""

#define DEAULT_NTP_SERVERNAME		"us.pool.ntp.org"

#define DEFAULT_USE_DHCP                true
#define DEFAULT_HTTP_SERVER_IP          ""
#define DEFAULT_HTTP_SERVER_PORT        "8080"
#define DEFAULT_NODENAME                "esp8266"
#define DEFAULT_ADMIN_PASSWORD          "esp8266"

#define RUN_MODE_AUTO    0
#define RUN_MODE_EXT1    1
#define RUN_MODE_EXT2    2
#define DEFAULT_RUN_MODE RUN_MODE_AUTO

// ------ define if GPIO00 and GPIO02 are SCL/SDA to a PCF8574
//
#define ESP_HAS_PCF8574
//
// ------ undefine if GPIO00 and GPIO02 directly drive two relais
//        using a transistor amplifier
//
// #undef ESP_HAS_PCF8574
//
//
#ifdef ESP_HAS_PCF8574
#define RELAIS_OFF               HIGH
#define RELAIS_ON                LOW
#else
#define RELAIS_OFF               HIGH
#define RELAIS_ON                LOW
#endif // ESP_HAS_PCF8574
//
//
// ************************************************************************
// Logging
// ************************************************************************
//
static SimpleLog Logger;
//
// ************************************************************************
// define timezone and related globals ...
// ************************************************************************
//
// Central European Time (Frankfurt, Paris)
// -- Central European Summer Time
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};
// -- Central European Standard Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};
Timezone CE(CEST, CET);
TimeChangeRule *tcr;
//
// ************************************************************************
// define UDP related globals ...
// ************************************************************************
//
WiFiUDP Udp;
//
// ************************************************************************
// setup NTP Server to connect to
// ************************************************************************
//
String ntpServerName;
String ntpServerPort;
//
// ************************************************************************
// setup local SSID
// ************************************************************************
//
String wlanSSID;
//
// ************************************************************************
// global short that holds current runmode
// ************************************************************************
//
short volatile runMode;
//
#define UDP_REQUEST_EXT1_START   "EXT1:start"
#define UDP_REQUEST_EXT2_START   "EXT2:start"
#define UDP_REQUEST_EXT1_STOP    "EXT1:stop"
#define UDP_REQUEST_EXT2_STOP    "EXT2:stop"
//
#define UDP_UNKNOWN_REQUEST      -2
//
// ************************************************************************
// setup for  online-updates
// ************************************************************************
//
String updateUrl;
short lastRelease;
short lastVersion;
short lastPatchlevel;
//
short lastNumber;
//
short nextUpdateMonth;
short nextUpdateDay;
short nextUpdateWeekDay;
short nextUpdateHour;
short nextUpdateMinute;
short updateInterval;
short updateMode;
//
#define EEPROM_MAXLEN_UPDATE_URL         45
#define EEPROM_MAXLEN_LAST_RELEASE        2
#define EEPROM_MAXLEN_LAST_VERSION        2
#define EEPROM_MAXLEN_LAST_PATCHLEVEL     2
//
#define EEPROM_MAXLEN_LAST_NUMBER         2
//
#define EEPROM_MAXLEN_NEXT_UPDATE_MONTH   2
#define EEPROM_MAXLEN_NEXT_UPDATE_DAY     2
#define EEPROM_MAXLEN_NEXT_UPDATE_WEEKDAY 2
#define EEPROM_MAXLEN_NEXT_UPDATE_HOUR    2
#define EEPROM_MAXLEN_NEXT_UPDATE_MINUTE  2
//
#define EEPROM_MAXLEN_UPDATE_INTERVAL     2
#define EEPROM_MAXLEN_UPDATE_MODE         2
//
//
#define DEFAULT_UPDATE_URL              "http://192.168.1.122/ESP8266/"
#define DEFAULT_UPDATE_LAST_RELEASE     0
#define DEFAULT_UPDATE_LAST_VERSION     0
#define DEFAULT_UPDATE_LAST_PATCHLEVEL  0
#define DEFAULT_UPDATE_LAST_NUMBER      0
#define DEFAULT_UPDATE_NEXT_MONTH       0
#define DEFAULT_UPDATE_NEXT_DAY         0
#define DEFAULT_UPDATE_NEXT_WEEKDAY     0
#define DEFAULT_UPDATE_NEXT_HOUR        0
#define DEFAULT_UPDATE_NEXT_MINUTE      0
#define DEFAULT_UPDATE_INTERVAL         0
#define DEFAULT_UPDATE_MODE             0
//
// ************************************************************************
// setup passphrase for local WLAN
// ************************************************************************
//
String wlanPassphrase;
//
// ************************************************************************
// some additional globals holding settings
// ************************************************************************
//
// get IP using DHCP
bool useDhcp;
// IP of the started HTTP-server
String wwwServerIP;
// Port on that the started HTTP-server is listening
String wwwServerPort;
// Nodename - not currently in use. Part of standard data.
String nodeName;
// SHA1 - hash used as api key
String apiHashKey;
// Password to get access to admin page. Not currently in use.
String adminPasswd;
//
//
//
// ************************************************************************
//
// ---------- global dsEeprom instance and layout for extended EEPROM data
//
// ************************************************************************
//
dsEeprom eeprom;
//
// ------------------------------------------------------------------------
//
// ---------- layout EEPROM ext. data section
//
// ------------------------------------------------------------------------
//



#define EEPROM_MAXLEN_NTP_SERVER_NAME    20
#define EEPROM_MAXLEN_NTP_SERVER_PORT     5

#define EEPROM_ADD_SYS_VARS_BEGIN        EEPROM_STD_DATA_END

#define EEPROM_POS_NTP_SERVER_NAME       (EEPROM_ADD_SYS_VARS_BEGIN)
#define EEPROM_POS_NTP_SERVER_PORT       (EEPROM_POS_NTP_SERVER_NAME  + EEPROM_MAXLEN_NTP_SERVER_NAME   + EEPROM_LEADING_LENGTH)

#define EEPROM_ADD_SYS_VARS_END          (EEPROM_POS_NTP_SERVER_PORT  + EEPROM_MAXLEN_NTP_SERVER_PORT + EEPROM_LEADING_LENGTH)

#ifdef EEPROM_EXT_DATA_BEGIN
#undef EEPROM_EXT_DATA_BEGIN
#define EEPROM_EXT_DATA_BEGIN             EEPROM_ADD_SYS_VARS_END
#else
#define EEPROM_EXT_DATA_BEGIN             EEPROM_ADD_SYS_VARS_END
#endif

// known: EEPROM_MAXLEN_BOOLEAN see dsEeprom.h

#define MAX_ACTION_TABLE_LINES         8

#define EEPROM_MAXLEN_TBL_ROW_NAME    15
#define EEPROM_MAXLEN_TBL_ROW_MODE     5
#define EEPROM_MAXLEN_TBL_ENABLED      EEPROM_MAXLEN_BOOLEAN
#define EEPROM_MAXLEN_TBL_HR_FROM      2
#define EEPROM_MAXLEN_TBL_MIN_FROM     2
#define EEPROM_MAXLEN_TBL_HR_TO        2
#define EEPROM_MAXLEN_TBL_MIN_TO       2
#define EEPROM_MAXLEN_TBL_EXT_ENABLED  EEPROM_MAXLEN_BOOLEAN


#define EEPROM_ACTION_TBL_ENTRY_START  EEPROM_EXT_DATA_BEGIN
#define EEPROM_POS_TBL_ROW_NAME        EEPROM_EXT_DATA_BEGIN
//
#define EEPROM_POS_TBL_ROW_MODE        (EEPROM_POS_TBL_ROW_NAME     + EEPROM_MAXLEN_TBL_ROW_NAME    + EEPROM_LEADING_LENGTH)
//
#define EEPROM_POS_TBL_ENABLED1        (EEPROM_POS_TBL_ROW_MODE     + EEPROM_MAXLEN_TBL_ROW_MODE    + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_HR1_FROM        (EEPROM_POS_TBL_ENABLED1     + EEPROM_MAXLEN_TBL_ENABLED     + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_MIN1_FROM       (EEPROM_POS_TBL_HR1_FROM     + EEPROM_MAXLEN_TBL_HR_FROM     + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_HR1_TO          (EEPROM_POS_TBL_MIN1_FROM    + EEPROM_MAXLEN_TBL_MIN_FROM    + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_MIN1_TO         (EEPROM_POS_TBL_HR1_TO       + EEPROM_MAXLEN_TBL_HR_TO       + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_EXT1_ENABLED    (EEPROM_POS_TBL_MIN1_TO      + EEPROM_MAXLEN_TBL_MIN_TO      + EEPROM_LEADING_LENGTH)

#define EEPROM_POS_TBL_ENABLED2        (EEPROM_POS_TBL_EXT1_ENABLED + EEPROM_MAXLEN_TBL_EXT_ENABLED + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_HR2_FROM        (EEPROM_POS_TBL_ENABLED2     + EEPROM_MAXLEN_TBL_ENABLED     + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_MIN2_FROM       (EEPROM_POS_TBL_HR2_FROM     + EEPROM_MAXLEN_TBL_HR_FROM     + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_HR2_TO          (EEPROM_POS_TBL_MIN2_FROM    + EEPROM_MAXLEN_TBL_MIN_FROM    + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_MIN2_TO         (EEPROM_POS_TBL_HR2_TO       + EEPROM_MAXLEN_TBL_HR_TO       + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_TBL_EXT2_ENABLED    (EEPROM_POS_TBL_MIN2_TO      + EEPROM_MAXLEN_TBL_MIN_TO      + EEPROM_LEADING_LENGTH)

#define EEPROM_ACTION_TBL_ENTRY_END    (EEPROM_POS_TBL_EXT2_ENABLED + EEPROM_MAXLEN_TBL_EXT_ENABLED + EEPROM_LEADING_LENGTH)
#define EEPROM_ACTION_TBL_ENTRY_LENGTH (EEPROM_ACTION_TBL_ENTRY_END - EEPROM_ACTION_TBL_ENTRY_START)
// #define EEPROM_EXT_DATA_END            (EEPROM_ACTION_TBL_ENTRY_START + (EEPROM_ACTION_TBL_ENTRY_LENGTH * MAX_ACTION_TABLE_LINES))
//
//
//
#define EEPROM_POS_UPDATE_URL            (EEPROM_ACTION_TBL_ENTRY_START + (EEPROM_ACTION_TBL_ENTRY_LENGTH * MAX_ACTION_TABLE_LINES))
#define EEPROM_POS_LAST_RELEASE          (EEPROM_POS_UPDATE_URL          + EEPROM_MAXLEN_UPDATE_URL          + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_LAST_VERSION          (EEPROM_POS_LAST_RELEASE        + EEPROM_MAXLEN_LAST_RELEASE        + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_LAST_PATCHLEVEL       (EEPROM_POS_LAST_VERSION        + EEPROM_MAXLEN_LAST_VERSION        + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_LAST_NUMBER           (EEPROM_POS_LAST_PATCHLEVEL     + EEPROM_MAXLEN_LAST_PATCHLEVEL     + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_NEXT_UPDATE_MONTH     (EEPROM_POS_LAST_NUMBER         + EEPROM_MAXLEN_LAST_NUMBER         + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_NEXT_UPDATE_DAY       (EEPROM_POS_NEXT_UPDATE_MONTH   + EEPROM_MAXLEN_NEXT_UPDATE_MONTH   + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_NEXT_UPDATE_WEEKDAY   (EEPROM_POS_NEXT_UPDATE_DAY     + EEPROM_MAXLEN_NEXT_UPDATE_DAY     + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_NEXT_UPDATE_HOUR      (EEPROM_POS_NEXT_UPDATE_WEEKDAY + EEPROM_MAXLEN_NEXT_UPDATE_WEEKDAY + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_NEXT_UPDATE_MINUTE    (EEPROM_POS_NEXT_UPDATE_HOUR    + EEPROM_MAXLEN_NEXT_UPDATE_HOUR    + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_UPDATE_INTERVAL       (EEPROM_POS_NEXT_UPDATE_MINUTE  + EEPROM_MAXLEN_NEXT_UPDATE_MINUTE  + EEPROM_LEADING_LENGTH)
#define EEPROM_POS_UPDATE_MODE           (EEPROM_POS_UPDATE_INTERVAL     + EEPROM_MAXLEN_UPDATE_INTERVAL     + EEPROM_LEADING_LENGTH)

#define EEPROM_EXT_DATA_END              (EEPROM_POS_UPDATE_MODE         + EEPROM_MAXLEN_UPDATE_MODE         + EEPROM_LEADING_LENGTH)

#if not defined (EEPROM_EXT_DATA_END)
#define EEPROM_EXT_DATA_END      EEPROM_MAX_SIZE
#endif // EEPROM_EXT_DATA_END 

#define ACTION_FLAG_ACTIVE         1
#define ACTION_FLAG_INACTIVE       2
#define ACTION_FLAG_WAITING        4
#define ACTION_FLAG_NO_ACTION      8
#define ACTION_FLAG_STOPPED       16
#define ACTION_FLAG_OVERRIDDEN    32
#define ACTION_FLAG_ALWAYS_ON     64
#define ACTION_FLAG_ALWAYS_OFF   128
#define ACTION_FLAG_MODE_EXT1    256
#define ACTION_FLAG_MODE_EXT2    512

#define NUM_SETUP_FORM_FIELDS     14

struct _action_entry {
  String   name;
  String   mode;
  short    actionFlag;
  bool     enabled_1;
  String   hourFrom_1;
  String   minuteFrom_1;
  String   hourTo_1;
  String   minuteTo_1;
  bool     extEnable_1;
  bool     enabled_2;
  String   hourFrom_2;
  String   minuteFrom_2;
  String   hourTo_2;
  String   minuteTo_2;
  bool     extEnable_2;
};

struct _action_entry tblEntry[MAX_ACTION_TABLE_LINES];

String formFieldName[NUM_SETUP_FORM_FIELDS];


#define KW_IDX_BEZEICHNER   0
#define KW_IDX_MODE         1
#define KW_IDX_ENABLED_1    2
#define KW_IDX_HFROM_1      3
#define KW_IDX_MFROM_1      4
#define KW_IDX_HTO_1        5
#define KW_IDX_MTO_1        6
#define KW_IDX_EXT_1        7
#define KW_IDX_ENABLED_2    8
#define KW_IDX_HFROM_2      9
#define KW_IDX_MFROM_2     10
#define KW_IDX_HTO_2       11
#define KW_IDX_MTO_2       12
#define KW_IDX_EXT_2       13

#define DEFAULT_FORMVAR_BEZEICHNER  ""
#define DEFAULT_FORMVAR_MODE        ""
#define DEFAULT_FORMVAR_ENABLED_1   false
#define DEFAULT_FORMVAR_HFROM_1     ""
#define DEFAULT_FORMVAR_MFROM_1     ""
#define DEFAULT_FORMVAR_HTO_1       ""
#define DEFAULT_FORMVAR_MTO_1       ""
#define DEFAULT_FORMVAR_EXT_1       false
#define DEFAULT_FORMVAR_ENABLED_2   false
#define DEFAULT_FORMVAR_HFROM_2     ""
#define DEFAULT_FORMVAR_MFROM_2     ""
#define DEFAULT_FORMVAR_HTO_2       ""
#define DEFAULT_FORMVAR_MTO_2       ""
#define DEFAULT_FORMVAR_EXT_2       false
    


#ifdef ESP_HAS_PCF8574
#define CONNECTED_RELAIS    8
#else
#define CONNECTED_RELAIS    2
#endif

// ************************************************************************
// setup local port for NTP communication
// ************************************************************************
//
unsigned int localUDPPort =	DEFAULT_UDP_LISTENPORT;
//
// A UDP instance to let us send and receive packets over UDP
WiFiUDP multicastInstance;
// Multicast declarations
IPAddress multicastIP(239, 0, 0, 57);
unsigned int multicastPort = 12345;      // local port to listen on
//
// ************************************************************************
// define clock an data pin for iic
// ************************************************************************
//
static int default_sda_pin = 2;
static int default_scl_pin = 0;
//
// static int default_sda_pin = 4;
// static int default_scl_pin = 5;
//
//
// ************************************************************************
// create an instance of PCF8574
// ************************************************************************
//
PCF8574 PCF_38(0x38);  // add switches to lines  (used as input)
//
//
// ************************************************************************
//
// ---------- none specific code - several helper functions
//
// ************************************************************************
//
// ------------------------------------------------------------------------
//
// ---------- handle diagnostic informations given by assertion and abort execution
//
// ------------------------------------------------------------------------
//
// void __assert(const char *__func, const char *__file, int __lineno, const char *__sexp)
// {
//    // transmit diagnostic informations through serial link.
//    Serial.println(__func);
//    Serial.println(__file);
//    Serial.println(__lineno, DEC);
//    Serial.println(__sexp);
//    Serial.flush();
//    // abort program execution.
//    abort();
// }
//
// ------------------------------------------------------------------------
//
// ---------- make a string from MAC address
//
// ------------------------------------------------------------------------
//
String macToID(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i)
  {
    result += String(mac[i], 16);
  }
  result.toUpperCase();
  return result;
}
//
// ------------------------------------------------------------------------
//
// ---------- generate a unique nodename from MAC address
//
// ------------------------------------------------------------------------
//
void generateNodename()
{
  String id;
  uint8_t mac[6];

  WiFi.macAddress(mac);
  id = macToID(mac);
  nodeName = "ESP_" + id;
}
//
// ------------------------------------------------------------------------
//
// ---------- print several ESP8266 chip information
//
// ------------------------------------------------------------------------
//
void dumpInfo()
{

#ifdef DO_LOG

  if ( !beQuiet )
  {
    Serial.print("WiFi.macAddress .....:");
    Serial.println(WiFi.macAddress());
    Serial.print("WiFi.localIP ........:");
    Serial.println(WiFi.localIP());
    WiFi.printDiag(Serial);

    Serial.print("ESP.getFreeHeap .....:");
    Serial.println(ESP.getFreeHeap());
    Serial.print("ESP.getChipId .......:");
    Serial.println(ESP.getChipId());
    Serial.print("ESP.getFlashChipId ..: ");
    Serial.println(ESP.getFlashChipId());
    Serial.print("ESP.getFlashChipSize : ");
    Serial.println(ESP.getFlashChipSize());
    Serial.print("ESP.getFlashChipSpeed: ");
    Serial.println(ESP.getFlashChipSpeed());
    Serial.print("ESP.getCycleCount ...: ");
    Serial.println(ESP.getCycleCount());
    Serial.print("ESP.getVcc ..........: ");
    Serial.println(ESP.getVcc());
  }

#endif // DO_LOG

}
//
// ------------------------------------------------------------------------
//
// ---------- RESTART the ESP ... trial an error phase to this feature
//
// ------------------------------------------------------------------------
//
void espRestart()
{
  ESP.restart();
}
//
// ************************************************************************
//
// ---------- NTP specific code
//
// ************************************************************************
//
// ---- definitions for the NTP packet handling ----
//
// NTP time is in the first 48 bytes of message
//
const int NTP_PACKET_SIZE = 48;
//
//
// ---- functions for the NTP packet handling ----
//
// ------------------------------------------------------------------------
//
// time_t getNtpTime()
//
// ------------------------------------------------------------------------
//
time_t getNtpTime()
{
  // NTP server's ip address
  static IPAddress ntpServerIP;

  // buffer to hold incoming & outgoing packets
  static byte packetBuffer[NTP_PACKET_SIZE];

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
#ifdef DO_LOG
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "Transmit NTP Request");
#endif // DO_LOG
  // get a random server from the pool
  WiFi.hostByName(ntpServerName.c_str(), ntpServerIP);

#ifdef DO_LOG
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "ntpServerName: %s\n", ntpServerName.c_str());
#endif // DO_LOG

  sendNTPpacket(ntpServerIP, packetBuffer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
#ifdef DO_LOG
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "received NTP response\n");
#endif // DO_LOG
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL;
    }
  }
#ifdef DO_LOG
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "NO NTP response\n");
#endif // DO_LOG
  return 0; // return 0 if unable to get the time
}
//
// ------------------------------------------------------------------------
//
// void sendNTPpacket(IPAddress &address)
//   send an NTP request to the time server at the given address
//
// ------------------------------------------------------------------------
//
void sendNTPpacket(IPAddress &address, byte packetBuffer[])
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
//
// ************************************************************************
//
// ---------- page preparation and handling
//
// ************************************************************************
//
// ------------------------------------------------------------------------
//
// ---------- load and handle setup page ----
//
void apiPage();
void setupPage();
void doCreateLine( int tmRow );
void doTimeSelect( int tmNum, int tmRow, char* sRow );
void doTimeOnSelect( char *sTarget, char *sSwitch, char *sBgColor, int tmNum, char* sRow, const char *sHour, const char* sMinute );
// ------------------------------------------------------------------------
//

//
// ------------------------------------------------------------------------
//
// ---------- load and handle API page ----
//
// ------------------------------------------------------------------------
//
//
//
// ************************************************************************
//
// defines and globals related to setup()
//
// ************************************************************************
//
#define SERIAL_BAUD	115200
//
// ************************************************************************
//
// ---------- web server related globals and defines
//
// ************************************************************************
//
ESP8266WebServer server; // DEFAULT_HTTP_LISTENPORT
String pageContent;
int serverStatusCode;
//
#define SERVER_METHOD_GET       1
#define SERVER_METHOD_POST      2
#define ARGS_ADMIN_PAGE         2
//
// ************************************************************************
//
// ---------- misc. helper functions
//
// ************************************************************************
//
//
// ------------------------------------------------------------------------
//
// ---------- set properties to default values
//
// ------------------------------------------------------------------------
//
void resetAdminSettings2Default( void )
{
  ntpServerName  = DEAULT_NTP_SERVERNAME;
  wlanSSID       = DEFAULT_WLAN_SSID;
  wlanPassphrase = DEFAULT_WLAN_PASSPHRASE;
  useDhcp        = DEFAULT_USE_DHCP;
  wwwServerIP    = DEFAULT_HTTP_SERVER_IP;
  wwwServerPort  = DEFAULT_HTTP_SERVER_PORT;
  nodeName       = DEFAULT_NODENAME;
  adminPasswd    = DEFAULT_ADMIN_PASSWORD;
}

//
// ------------------------------------------------------------------------
//
// ---------- reset action flags
//
// ------------------------------------------------------------------------
//
void resetActionFlags( void )
{
  int currLine;

  //    for( currLine = 0; currLine < MAX_ACTION_TABLE_LINES; currLine++ )
  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "reset action flag for port %d\n", currLine );
    }
#endif // DO_LOG

    tblEntry[currLine].actionFlag = ACTION_FLAG_INACTIVE | ACTION_FLAG_NO_ACTION;
  }

}


//
// ------------------------------------------------------------------------
//
// ---------- store sytem and environmental settings to EEPROM
//
// ------------------------------------------------------------------------
//
int storeAdminSettings()
{
  int retVal = 0;

  eeprom.storeString(  wlanSSID,      EEPROM_MAXLEN_WLAN_SSID,       EEPROM_POS_WLAN_SSID );

  eeprom.storeString(  wlanPassphrase,    EEPROM_MAXLEN_WLAN_PASSPHRASE, EEPROM_POS_WLAN_PASSPHRASE );

  eeprom.storeString(  wwwServerIP,   EEPROM_MAXLEN_SERVER_IP,       EEPROM_POS_SERVER_IP );

  eeprom.storeString(  wwwServerPort, EEPROM_MAXLEN_SERVER_PORT,     EEPROM_POS_SERVER_PORT );

  eeprom.storeString(  nodeName,      EEPROM_MAXLEN_NODENAME,        EEPROM_POS_NODENAME );

  eeprom.storeString(  adminPasswd,   EEPROM_MAXLEN_ADMIN_PASSWORD,  EEPROM_POS_ADMIN_PASSWORD );

  eeprom.validate();

  return ( retVal );

}

//
// ------------------------------------------------------------------------
//
// ---------- restore sytem and environmentsl settings to EEPROM
//
// ------------------------------------------------------------------------
//
int restoreAdminSettings()
{
  int retVal = 0;
  unsigned long crcCalc;
  unsigned long crcRead;

  if ( eeprom.isValid() )
  {
    crcCalc = eeprom.crc( EEPROM_STD_DATA_BEGIN, EEPROM_EXT_DATA_END );

    eeprom.restoreRaw( (char*) &crcRead, EEPROM_POS_CRC32, EEPROM_MAXLEN_CRC32, EEPROM_MAXLEN_CRC32);


    if ( (crcCalc == crcRead) )
    {
      eeprom.restoreString(  wlanSSID,    EEPROM_POS_WLAN_SSID,       EEPROM_MAXLEN_WLAN_SSID );
      eeprom.restoreString(  wlanPassphrase,  EEPROM_POS_WLAN_PASSPHRASE, EEPROM_MAXLEN_WLAN_PASSPHRASE );
      eeprom.restoreString(  wwwServerIP, EEPROM_POS_SERVER_IP,       EEPROM_MAXLEN_SERVER_IP );
      eeprom.restoreString(  wwwServerPort, EEPROM_POS_SERVER_PORT,     EEPROM_MAXLEN_SERVER_PORT );
      eeprom.restoreString(  nodeName,    EEPROM_POS_NODENAME,        EEPROM_MAXLEN_NODENAME );
      eeprom.restoreString(  adminPasswd, EEPROM_POS_ADMIN_PASSWORD,  EEPROM_MAXLEN_ADMIN_PASSWORD );
      retVal = E_SUCCESS;
    }
    else
    {
      retVal = E_BAD_CRC;
    }
  }
  else
  {
    retVal = E_INVALID_MAGIC;
  }

  return ( retVal );

}

//
// ------------------------------------------------------------------------
//
// ---------- store additional sytem and environmental settings to EEPROM
//
// ------------------------------------------------------------------------
//
int storeAddSysvars()
{
  int retVal = 0;

  eeprom.storeString(  ntpServerName,      EEPROM_MAXLEN_NTP_SERVER_NAME,       EEPROM_POS_NTP_SERVER_NAME );
  eeprom.storeString(  ntpServerPort,      EEPROM_MAXLEN_NTP_SERVER_PORT,       EEPROM_POS_NTP_SERVER_PORT );


  return ( retVal );
}

//
// ------------------------------------------------------------------------
//
// ---------- restore additional sysvars to EEPROM
//
// ------------------------------------------------------------------------
//
int restoreAddSysvars()
{
  int retVal = 0;

  eeprom.restoreString(  ntpServerName,    EEPROM_POS_NTP_SERVER_NAME,       EEPROM_MAXLEN_NTP_SERVER_NAME );
  eeprom.restoreString(  ntpServerPort,    EEPROM_POS_NTP_SERVER_PORT,       EEPROM_MAXLEN_NTP_SERVER_PORT );

  eeprom.validate();

  return ( retVal );
}







//
// ------------------------------------------------------------------------
//
// ---------- store the table with action information to EEPROM
//
// ------------------------------------------------------------------------
//
int storeActionTable()
{
  int retVal = 0;
  int currLine;


  // for( currLine = 0; currLine < MAX_ACTION_TABLE_LINES; currLine++ )
  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    eeprom.storeString(tblEntry[currLine].name, EEPROM_MAXLEN_TBL_ROW_NAME,
                       EEPROM_POS_TBL_ROW_NAME +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
    eeprom.storeString(tblEntry[currLine].mode, EEPROM_MAXLEN_TBL_ROW_MODE,
                       EEPROM_POS_TBL_ROW_MODE +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeBoolean((char*)&tblEntry[currLine].enabled_1, EEPROM_POS_TBL_ENABLED1 +
                        (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
    eeprom.storeString(tblEntry[currLine].hourFrom_1, EEPROM_MAXLEN_TBL_HR_FROM,
                       EEPROM_POS_TBL_HR1_FROM +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
    eeprom.storeString(tblEntry[currLine].minuteFrom_1, EEPROM_MAXLEN_TBL_MIN_FROM,
                       EEPROM_POS_TBL_MIN1_FROM +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
    eeprom.storeString(tblEntry[currLine].hourTo_1, EEPROM_MAXLEN_TBL_HR_TO,
                       EEPROM_POS_TBL_HR1_TO +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
    eeprom.storeString(tblEntry[currLine].minuteTo_1, EEPROM_MAXLEN_TBL_MIN_TO,
                       EEPROM_POS_TBL_MIN1_TO +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
    eeprom.storeBoolean((char*)&tblEntry[currLine].extEnable_1, EEPROM_POS_TBL_EXT1_ENABLED +
                        (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeBoolean((char*)&tblEntry[currLine].enabled_2, EEPROM_POS_TBL_ENABLED2 +
                        (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeString(tblEntry[currLine].hourFrom_2, EEPROM_MAXLEN_TBL_HR_FROM,
                       EEPROM_POS_TBL_HR2_FROM +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeString(tblEntry[currLine].minuteFrom_2, EEPROM_MAXLEN_TBL_MIN_FROM,
                       EEPROM_POS_TBL_MIN2_FROM +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeString(tblEntry[currLine].hourTo_2, EEPROM_MAXLEN_TBL_HR_TO,
                       EEPROM_POS_TBL_HR2_TO +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeString(tblEntry[currLine].minuteTo_2, EEPROM_MAXLEN_TBL_MIN_TO,
                       EEPROM_POS_TBL_MIN2_TO +
                       (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );

    eeprom.storeBoolean((char*)&tblEntry[currLine].extEnable_2, EEPROM_POS_TBL_EXT2_ENABLED +
                        (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
  }

  eeprom.validate();

  return ( retVal );

}

//
// ------------------------------------------------------------------------
//
// ---------- initialize the table with empty information
//
// ------------------------------------------------------------------------
//
void resetActionTable2Default()
{
  int currLine;
  
  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    tblEntry[currLine].name         = DEFAULT_FORMVAR_BEZEICHNER;
    tblEntry[currLine].mode         = DEFAULT_FORMVAR_MODE;
    tblEntry[currLine].enabled_1    = DEFAULT_FORMVAR_ENABLED_1;
    tblEntry[currLine].hourFrom_1   = DEFAULT_FORMVAR_HFROM_1;
    tblEntry[currLine].minuteFrom_1 = DEFAULT_FORMVAR_MFROM_1;
    tblEntry[currLine].hourTo_1     = DEFAULT_FORMVAR_HTO_1;
    tblEntry[currLine].minuteTo_1   = DEFAULT_FORMVAR_MTO_1;
    
    tblEntry[currLine].extEnable_1  = DEFAULT_FORMVAR_EXT_1;
    tblEntry[currLine].enabled_2    = DEFAULT_FORMVAR_ENABLED_2;
    tblEntry[currLine].hourFrom_2   = DEFAULT_FORMVAR_HFROM_2;
    tblEntry[currLine].minuteFrom_2 = DEFAULT_FORMVAR_MFROM_2;
    tblEntry[currLine].hourTo_2     = DEFAULT_FORMVAR_HTO_2;
    tblEntry[currLine].minuteTo_2   = DEFAULT_FORMVAR_MTO_2;
    tblEntry[currLine].extEnable_2  = DEFAULT_FORMVAR_EXT_2;
  }

}

//
// ------------------------------------------------------------------------
//
// ---------- restore table with action information from EEPROM
//
// ------------------------------------------------------------------------
//
int restoreActionTable()
{
  int retVal = 0;
  unsigned long crcCalc;
  unsigned long crcRead;
  int currLine;

  if ( eeprom.isValid() )
  {
    crcCalc = eeprom.crc( EEPROM_STD_DATA_BEGIN, EEPROM_EXT_DATA_END );

    eeprom.restoreRaw( (char*) &crcRead, EEPROM_POS_CRC32, EEPROM_MAXLEN_CRC32, EEPROM_MAXLEN_CRC32);

    if ( crcCalc == crcRead )
    {

      // for( currLine = 0; currLine < MAX_ACTION_TABLE_LINES; currLine++ )
      for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
      {
        eeprom.restoreString(tblEntry[currLine].name,
                             EEPROM_POS_TBL_ROW_NAME + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_ROW_NAME );
        eeprom.restoreString(tblEntry[currLine].mode,
                             EEPROM_POS_TBL_ROW_MODE + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_ROW_MODE );
        eeprom.restoreBoolean((char*)&tblEntry[currLine].enabled_1, EEPROM_POS_TBL_ENABLED1 +
                              (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
        eeprom.restoreString(tblEntry[currLine].hourFrom_1,
                             EEPROM_POS_TBL_HR1_FROM + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_HR_FROM );
        eeprom.restoreString(tblEntry[currLine].minuteFrom_1,
                             EEPROM_POS_TBL_MIN1_FROM + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_MIN_FROM );
        eeprom.restoreString(tblEntry[currLine].hourTo_1,
                             EEPROM_POS_TBL_HR1_TO + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_HR_TO );
        eeprom.restoreString(tblEntry[currLine].minuteTo_1,
                             EEPROM_POS_TBL_MIN1_TO + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_MIN_TO );
        eeprom.restoreBoolean((char*)&tblEntry[currLine].extEnable_1, EEPROM_POS_TBL_EXT1_ENABLED +
                              (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
        eeprom.restoreBoolean((char*)&tblEntry[currLine].enabled_2, EEPROM_POS_TBL_ENABLED2 +
                              (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
        eeprom.restoreString(tblEntry[currLine].hourFrom_2,
                             EEPROM_POS_TBL_HR2_FROM + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_HR_FROM );
        eeprom.restoreString(tblEntry[currLine].minuteFrom_2,
                             EEPROM_POS_TBL_MIN2_FROM + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_MIN_FROM );
        eeprom.restoreString(tblEntry[currLine].hourTo_2,
                             EEPROM_POS_TBL_HR2_TO + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_HR_TO );
        eeprom.restoreString(tblEntry[currLine].minuteTo_2,
                             EEPROM_POS_TBL_MIN2_TO + (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH),
                             EEPROM_MAXLEN_TBL_MIN_TO );
        eeprom.restoreBoolean((char*)&tblEntry[currLine].extEnable_2, EEPROM_POS_TBL_EXT2_ENABLED +
                              (currLine * EEPROM_ACTION_TBL_ENTRY_LENGTH) );
      }
    }
    else
    {
      retVal = E_BAD_CRC;
    }
  }
  else
  {
    retVal = E_INVALID_MAGIC;
  }

  return ( retVal );

}

//
// ------------------------------------------------------------------------
//
// ---------- store update info to EEPROM
//
// ------------------------------------------------------------------------
//
int storeUpdateInfo()
{
  int retVal = 0;

  eeprom.storeString( updateUrl,               EEPROM_MAXLEN_UPDATE_URL,           EEPROM_POS_UPDATE_URL);
  eeprom.storeRaw( (char*) &lastRelease,       EEPROM_MAXLEN_LAST_RELEASE,         EEPROM_POS_LAST_RELEASE);
  eeprom.storeRaw( (char*) &lastVersion,       EEPROM_MAXLEN_LAST_VERSION,         EEPROM_POS_LAST_VERSION);
  eeprom.storeRaw( (char*) &lastPatchlevel,    EEPROM_MAXLEN_LAST_PATCHLEVEL,      EEPROM_POS_LAST_PATCHLEVEL);
  eeprom.storeRaw( (char*) &lastNumber,        EEPROM_MAXLEN_LAST_NUMBER,          EEPROM_POS_LAST_NUMBER);
  eeprom.storeRaw( (char*) &nextUpdateMonth,   EEPROM_MAXLEN_NEXT_UPDATE_MONTH,    EEPROM_POS_NEXT_UPDATE_MONTH);
  eeprom.storeRaw( (char*) &nextUpdateDay,     EEPROM_MAXLEN_NEXT_UPDATE_DAY,      EEPROM_POS_NEXT_UPDATE_DAY);
  eeprom.storeRaw( (char*) &nextUpdateWeekDay, EEPROM_MAXLEN_NEXT_UPDATE_WEEKDAY,  EEPROM_POS_NEXT_UPDATE_WEEKDAY);
  eeprom.storeRaw( (char*) &nextUpdateHour,    EEPROM_MAXLEN_NEXT_UPDATE_HOUR,     EEPROM_POS_NEXT_UPDATE_HOUR);
  eeprom.storeRaw( (char*) &nextUpdateMinute,  EEPROM_MAXLEN_NEXT_UPDATE_MINUTE,   EEPROM_POS_NEXT_UPDATE_MINUTE);
  eeprom.storeRaw( (char*) &updateInterval,    EEPROM_MAXLEN_UPDATE_INTERVAL,      EEPROM_POS_UPDATE_INTERVAL);
  eeprom.storeRaw( (char*) &updateMode,        EEPROM_MAXLEN_UPDATE_MODE,          EEPROM_POS_UPDATE_MODE);

  eeprom.validate();

  return ( retVal );

}

//
// ------------------------------------------------------------------------
//
// ---------- restore update info from EEPROM
//
// ------------------------------------------------------------------------
//
int restoreUpdateInfo()
{
  int retVal = 0;

  eeprom.restoreString( updateUrl,               EEPROM_POS_UPDATE_URL,           EEPROM_MAXLEN_UPDATE_URL);
  eeprom.restoreRaw( (char*) &lastRelease,       EEPROM_POS_LAST_RELEASE,         EEPROM_MAXLEN_LAST_RELEASE,        EEPROM_MAXLEN_LAST_RELEASE);
  eeprom.restoreRaw( (char*) &lastVersion,       EEPROM_POS_LAST_VERSION,         EEPROM_MAXLEN_LAST_VERSION,        EEPROM_MAXLEN_LAST_VERSION);
  eeprom.restoreRaw( (char*) &lastPatchlevel,    EEPROM_POS_LAST_PATCHLEVEL,      EEPROM_MAXLEN_LAST_PATCHLEVEL,     EEPROM_MAXLEN_LAST_PATCHLEVEL);
  eeprom.restoreRaw( (char*) &lastNumber,        EEPROM_POS_LAST_NUMBER,          EEPROM_MAXLEN_LAST_NUMBER,         EEPROM_MAXLEN_LAST_NUMBER);
  eeprom.restoreRaw( (char*) &nextUpdateMonth,   EEPROM_POS_NEXT_UPDATE_MONTH,    EEPROM_MAXLEN_NEXT_UPDATE_MONTH,   EEPROM_MAXLEN_NEXT_UPDATE_MONTH);
  eeprom.restoreRaw( (char*) &nextUpdateDay,     EEPROM_POS_NEXT_UPDATE_DAY,      EEPROM_MAXLEN_NEXT_UPDATE_DAY,     EEPROM_MAXLEN_NEXT_UPDATE_DAY);
  eeprom.restoreRaw( (char*) &nextUpdateWeekDay, EEPROM_POS_NEXT_UPDATE_WEEKDAY,  EEPROM_MAXLEN_NEXT_UPDATE_WEEKDAY, EEPROM_MAXLEN_NEXT_UPDATE_WEEKDAY);
  eeprom.restoreRaw( (char*) &nextUpdateHour,    EEPROM_POS_NEXT_UPDATE_HOUR,     EEPROM_MAXLEN_NEXT_UPDATE_HOUR,    EEPROM_MAXLEN_NEXT_UPDATE_HOUR);
  eeprom.restoreRaw( (char*) &nextUpdateMinute,  EEPROM_POS_NEXT_UPDATE_MINUTE,   EEPROM_MAXLEN_NEXT_UPDATE_MINUTE,  EEPROM_MAXLEN_NEXT_UPDATE_MINUTE);
  eeprom.restoreRaw( (char*) &updateInterval,    EEPROM_POS_UPDATE_INTERVAL,      EEPROM_MAXLEN_UPDATE_INTERVAL,     EEPROM_MAXLEN_UPDATE_INTERVAL);
  eeprom.restoreRaw( (char*) &updateMode,        EEPROM_POS_UPDATE_MODE,          EEPROM_MAXLEN_UPDATE_MODE,         EEPROM_MAXLEN_UPDATE_MODE);

  return ( retVal );
}

//
// ------------------------------------------------------------------------
//
// ---------- reset uopdate information to default values
//
// ------------------------------------------------------------------------
//
void resetUpdateInfo2Default()
{
  updateUrl         = DEFAULT_UPDATE_URL;
  lastRelease       = DEFAULT_UPDATE_LAST_RELEASE;
  lastVersion       = DEFAULT_UPDATE_LAST_VERSION;
  lastPatchlevel    = DEFAULT_UPDATE_LAST_PATCHLEVEL;
  lastNumber        = DEFAULT_UPDATE_LAST_NUMBER;
  nextUpdateMonth   = DEFAULT_UPDATE_NEXT_MONTH;
  nextUpdateDay     = DEFAULT_UPDATE_NEXT_DAY;
  nextUpdateWeekDay = DEFAULT_UPDATE_NEXT_WEEKDAY;
  nextUpdateHour    = DEFAULT_UPDATE_NEXT_HOUR;
  nextUpdateMinute  = DEFAULT_UPDATE_NEXT_MINUTE;
  updateInterval    = DEFAULT_UPDATE_INTERVAL;
  updateMode        = DEFAULT_UPDATE_MODE;
}

//
// ************************************************************************
//
// ---------- toggleRelais()
//
// ************************************************************************
//
void toggleRelais(int port)
{

#ifdef ESP_HAS_PCF8574
  PCF_38.toggle(port);
#else
  static int lastState;

  digitalWrite( port, ~lastState );
  lastState = ~lastState;

#endif // ESP_HAS_PCF8574

}


//
// ************************************************************************
//
// ---------- switchRelais()
//
// ************************************************************************
//
void switchRelais(int port, int newStatus)
{

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, "switch relais[%d] to %d\n", port, newStatus);  
  }
#endif // DO_LOG

#ifdef ESP_HAS_PCF8574
  PCF_38.write(port, newStatus);
#else
  digitalWrite( port, newStatus );
#endif // ESP_HAS_PCF8574

  if( newStatus == RELAIS_ON )
  {
    tblEntry[port].actionFlag |= ACTION_FLAG_ACTIVE;
  }
  else
  {
    tblEntry[port].actionFlag &= ~ACTION_FLAG_ACTIVE;
  }


}
//
// ************************************************************************
//
// ---------- void alwaysOn( int timerNo, int port )
//
// ************************************************************************
//
void alwaysOn(int port)
{

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "... set always on for port %d\n",port);
  }
#endif // DO_LOG

  if ( (tblEntry[port].actionFlag & ACTION_FLAG_ALWAYS_ON) == 0 )
  {
    tblEntry[port].actionFlag |= ACTION_FLAG_ALWAYS_ON;
    switchRelais(port, RELAIS_ON);

  }
}

//
// ************************************************************************
//
// ---------- void alwaysOff( int timerNo, int port )
//
// ************************************************************************
//
void alwaysOff(int port)
{

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "... set always off for port %d\n", port);
  }
#endif // DO_LOG

  if ( (tblEntry[port].actionFlag & ACTION_FLAG_ALWAYS_OFF) == 0 )
  {
    tblEntry[port].actionFlag |= ACTION_FLAG_ALWAYS_OFF;
    switchRelais(port, RELAIS_OFF);
  }
}

//
// ************************************************************************
//
// ---------- startupActions()
//
// ************************************************************************
//
void startupActions( void )
{
  int i;
  time_t secsSinceEpoch;
  short nowMinutes;
  short chkMinutesFrom, chkMinutesTo;
  short wrapMinutes;
  time_t utc;

  utc = now();
  secsSinceEpoch = CE.toLocal(utc, &tcr);
  nowMinutes = ( hour(secsSinceEpoch) * 60 ) + minute(secsSinceEpoch);

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "nowMinutes is %d\n", nowMinutes );
  }
#endif // DO_LOG

  for ( i = 0; i < CONNECTED_RELAIS; i++ )
  {
    tblEntry[i].actionFlag = (ACTION_FLAG_INACTIVE | ACTION_FLAG_NO_ACTION);

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "flasg cleared, set port %d to LOW!\n", i );
    }
#endif // DO_LOG

    switchRelais(i, RELAIS_OFF);

    if ( tblEntry[i].mode.equalsIgnoreCase("auto") )
    {
      // if in AUTO mode

      if ( tblEntry[i].enabled_1 )
      {
        // time 1 is enabled

        chkMinutesFrom = (atoi(tblEntry[i].hourFrom_1.c_str()) * 60) + atoi(tblEntry[i].minuteFrom_1.c_str());
        chkMinutesTo = (atoi(tblEntry[i].hourTo_1.c_str()) * 60) + atoi(tblEntry[i].minuteTo_1.c_str());

        if ( chkMinutesTo < chkMinutesFrom )
        {
          // time wrap
          wrapMinutes = (24*60);
#ifdef DO_LOG
          if ( !beQuiet )
          {
            Logger.Log(LOGLEVEL_DEBUG, (const char*) "wrap minutes time 1: %d\n", wrapMinutes);
          }
#endif // DO_LOG

          chkMinutesTo += wrapMinutes;

        }

        if ( nowMinutes >= chkMinutesFrom && nowMinutes < chkMinutesTo )
        {
#ifdef DO_LOG
          if ( !beQuiet )
          {
            Logger.Log(LOGLEVEL_DEBUG,
                       (const char*) "Current time(%d) is after Begin(%d) but before End(%d) of time 1 ... starting action\n",
                       nowMinutes, chkMinutesFrom, chkMinutesTo );
            Logger.Log(LOGLEVEL_DEBUG, (const char*) "switch port %d back to HIGH!\n", i );
          }
#endif // DO_LOG

          switchRelais(i, RELAIS_ON);
        }
      }

      if ( tblEntry[i].enabled_2 )
      {

        chkMinutesFrom = (atoi(tblEntry[i].hourFrom_2.c_str()) * 60) + atoi(tblEntry[i].minuteFrom_2.c_str());
        chkMinutesTo = (atoi(tblEntry[i].hourTo_2.c_str()) * 60) + atoi(tblEntry[i].minuteTo_2.c_str());

        if ( chkMinutesTo < chkMinutesFrom )
        {
          // time wrap
          wrapMinutes = 24 * 60;
#ifdef DO_LOG
          if ( !beQuiet )
          {
            Logger.Log(LOGLEVEL_DEBUG, (const char*) "wrap minutes time 2: %d\n", wrapMinutes);
          }
#endif // DO_LOG

          chkMinutesTo += wrapMinutes;
        }

        if ( nowMinutes >= chkMinutesFrom && nowMinutes < chkMinutesTo )
        {
          // set this entry to active
#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG,
                         (const char*) "Current time(%d) is after Begin(%d) but before End(%d) of time 2 ... starting action\n",
                         nowMinutes, chkMinutesFrom, chkMinutesTo );
              Logger.Log(LOGLEVEL_DEBUG, (const char*) "switch port %d back to HIGH!\n", i );
            }
#endif // DO_LOG

            switchRelais(i, RELAIS_ON);
          }
        }

    }
    else // if( tblEntry[i].mode.equalsIgnoreCase("auto") )
    {
      if ( tblEntry[i].mode.equalsIgnoreCase("on") )
      {
        alwaysOn(i);
      }
      else
      {
        alwaysOff(i);
      }
    }
  }
}

//
// ------------------------------------------------------------------------
//
const int RED = 15;
const int GREEN = 12;
const int BLUE = 13;
//
void LEDOff()
{
    analogWrite(RED, 0 );
    analogWrite(GREEN, 0);
    analogWrite(BLUE, 0);
}
//
void LEDRed( )
{
    analogWrite(RED, 127 );
    analogWrite(GREEN, 0);
    analogWrite(BLUE, 0);
}

void LEDGreen( )
{
    analogWrite(RED, 0 );
    analogWrite(GREEN, 127);
    analogWrite(BLUE, 0);
}

void LEDBlue( )
{
    analogWrite(RED, 0 );
    analogWrite(GREEN, 0);
    analogWrite(BLUE, 127);
}

//
// ************************************************************************
//
// ---------- setup()
//
// ************************************************************************
//
#define FIRMWARE_CHECK   "0.0.1"
#define AUTO_RECONNECT_SECONDS   120

void setup()
{

  unsigned long crcCalc;
  unsigned long crcRead;
  unsigned long lastMillis;
  String multiIP;

#ifdef IS_WITTY_MODULE
  // witty pins for integrated RGB LED
  const int RED = 15;
  const int GREEN = 12;
  const int BLUE = 13;

  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);
  pinMode(BLUE, OUTPUT);

  analogWrite(RED, 0 );
  analogWrite(GREEN, 0);
  analogWrite(BLUE, 0);
#endif // IS_WITTY_MODULE


  IPAddress localIP;
  beQuiet = BE_QUIET;


  Serial.begin(SERIAL_BAUD);
  delay(10);

  generateNodename();
  apiHashKey = sha1(nodeName);
 
  Logger.Init(LOGLEVEL_DEBUG, &Serial);

#ifdef DO_LOG
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "EEPROM_ACTION_TBL_ENTRY_START = %d\n", EEPROM_ACTION_TBL_ENTRY_START );
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "EEPROM_ACTION_TBL_ENTRY_LENGTH = %d\n", EEPROM_ACTION_TBL_ENTRY_LENGTH );
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "EEPROM_EXT_DATA_END = %d\n", EEPROM_EXT_DATA_END );
  Logger.Log(LOGLEVEL_DEBUG, (const char*) "Current firmware is = %s\n", FIRMWARE_CHECK );

  Logger.Log(LOGLEVEL_DEBUG, (const char*) "UUID(SHA1) is: %s\n", apiHashKey.c_str());
#endif // DO_LOG

#ifdef ESP_HAS_PCF8574
  Wire.begin(default_sda_pin, default_scl_pin);
  //  Wire.begin();
  //  scanIIC();
#endif // ESP_HAS_PCF8574

  // default run mode = auto
  runMode = DEFAULT_RUN_MODE;

  resetAdminSettings2Default();
  resetUpdateInfo2Default();
  resetActionTable2Default();

  resetActionFlags();

  if ( eeprom.init( EEPROM_EXT_DATA_END, eeprom.version2Magic(), LOGLEVEL_QUIET ) < EE_STATUS_INVALID_CRC )
//  if( eeprom.init( EEPROM_EXT_DATA_END, 0x00, LOGLEVEL_QUIET ) < EE_STATUS_INVALID_CRC )
  {
    if ( eeprom.isValid() )
    {
#ifdef IS_WITTY_MODULE
      analogWrite(GREEN, 0);
      analogWrite(BLUE, 127);
#endif // IS_WITTY_MODULE


#ifdef DO_LOG
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "eeprom content is valid!\n");
#endif // DO_LOG

      crcCalc = eeprom.crc( EEPROM_STD_DATA_BEGIN, EEPROM_EXT_DATA_END );

      eeprom.restoreRaw( (char*) &crcRead, EEPROM_POS_CRC32, EEPROM_MAXLEN_CRC32, EEPROM_MAXLEN_CRC32);

#ifdef DO_LOG
      if ( !beQuiet )
      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "restored crc: %x calc crc: %x\n", crcRead, crcCalc);
      }
#endif // DO_LOG

      if ( (crcCalc == crcRead) )
      {
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "crc MATCH!\n" );
        }
#endif // DO_LOG

#ifdef IS_WITTY_MODULE
        analogWrite(RED, 0 );
        analogWrite(GREEN, 127);
        analogWrite(BLUE, 0);
#endif // IS_WITTY_MODULE

        restoreAdminSettings();
        restoreAddSysvars();
        restoreActionTable();
        restoreUpdateInfo();
      }
      else
      {
#ifdef IS_WITTY_MODULE
        analogWrite(RED, 127 );
        analogWrite(GREEN, 0);
        analogWrite(BLUE, 0);
#endif // IS_WITTY_MODULE

        eeprom.wipe();
        resetAdminSettings2Default();
        resetUpdateInfo2Default();
        resetActionTable2Default();
        storeAdminSettings();
        storeAddSysvars();
        storeActionTable();
        storeUpdateInfo();
        eeprom.setMagic( eeprom.version2Magic() );
        eeprom.validate();
      }
    }
    else
    {
#ifdef IS_WITTY_MODULE
      analogWrite(RED, 127 );
      analogWrite(GREEN, 0);
      analogWrite(BLUE, 0);
#endif // IS_WITTY_MODULE

      eeprom.wipe();
      resetAdminSettings2Default();
      resetUpdateInfo2Default();
      resetActionTable2Default();
      storeAdminSettings();
      storeAddSysvars();
      storeActionTable();
      storeUpdateInfo();
      eeprom.setMagic( eeprom.version2Magic() );
      eeprom.validate();
    }
  }
  else
  {
#ifdef IS_WITTY_MODULE
    analogWrite(RED, 127 );
    analogWrite(GREEN, 0);
    analogWrite(BLUE, 0);
#endif // IS_WITTY_MODULE

    eeprom.wipe();
    resetAdminSettings2Default();
    resetUpdateInfo2Default();
    resetActionTable2Default();
    storeAdminSettings();
    storeAddSysvars();
    storeActionTable();
    storeUpdateInfo();
    eeprom.setMagic( eeprom.version2Magic() );
    eeprom.validate();
  }



#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Connecting to >%s< using password >%s<\n", wlanSSID.c_str(), wlanPassphrase.c_str());
  }
#endif // DO_LOG

  WiFi.mode(WIFI_STA);

  lastMillis = millis();
  while ( millis() - lastMillis < AUTO_RECONNECT_SECONDS && WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  if ( WiFi.status() != WL_CONNECTED )
  {
    WiFi.begin(wlanSSID.c_str(), wlanPassphrase.c_str());
    while ( WiFi.status() != WL_CONNECTED )
    {
      delay(500);
    }
  }

#ifdef IS_WITTY_MODULE
  analogWrite(RED, 0 );
  analogWrite(GREEN, 127);
  analogWrite(BLUE, 0);
#endif // IS_WITTY_MODULE

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "WiFi connected\n" );
  }
#endif // DO_LOG

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Starting NTP over UDP\n");
  }
#endif // DO_LOG

  Udp.begin(localUDPPort);

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Local port: %d ... waiting for time sync", Udp.localPort());
  }
#endif // DO_LOG

  // after syncing set initial time
  setSyncProvider(getNtpTime);
  setSyncInterval(SECS_PER_HOUR);
  setTime(now());

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Time synced via NTP. Sync interval is set to %d seconds.\n", SECS_PER_HOUR );
  }
#endif // DO_LOG

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Starting HTTP server\n");
  }
#endif // DO_LOG

    multicastInstance.beginMulticast(WiFi.localIP(), multicastIP, multicastPort);
    multiIP = multicastIP.toString();

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Serial.print("Multicast listener started at : %s:%d\n", multiIP.c_str(), multicastPort );
  }
#endif // DO_LOG

  server = ESP8266WebServer( (unsigned long) wwwServerPort.toInt() );
  server.on("/", setupPage);
  server.on("/api", apiPage);

  server.begin();

  localIP = WiFi.localIP();
  wwwServerIP = localIP.toString();

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Webserver started. URL is: http://%s:%d\n", wwwServerIP.c_str(), wwwServerPort.toInt());
  }
#endif // DO_LOG

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "Startup actions ...\n");
  }
#endif // DO_LOG

  LEDGreen();

  startupActions();

  dumpInfo();

}


//
// ************************************************************************
//
// ---------- void scanIIC
//
// ************************************************************************
//
void scanIIC()
{
  byte error, address;
  int nDevices;

  Serial.println("Scanning...");

  nDevices = 0;
  for (address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");

      nDevices++;
    }
    else if (error == 4)
    {
      Serial.print("Unknow error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
}


bool isValidHour( const char* hour )
{
    bool retVal = false;
    if( hour != NULL )
    {
        if( strlen(hour) >= 2 )
        {
            retVal = false;

            if( hour[0] >= '0' && hour[0] <= '9' &&
                hour[1] >= '0' && hour[1] <= '9' )
            {
                if( atoi(hour) <= 23 )
                {
                    retVal = true;
                }
            }
        }
        else
        {
            if( hour[0] >= '0' && hour[0] <= '9' )
            {
                retVal = true;
            }
        }
    }
    return( retVal);
}


bool isValidMinute( const char* minute )
{
    bool retVal = false;
    if( minute != NULL )
    {

        if( strlen(minute) >= 2 )
        {
            retVal = false;

            if( minute[0] >= '0' && minute[0] <= '9' &&
                minute[1] >= '0' && minute[1] <= '9' )
            {
                if( atoi(minute) <= 59 )
                {
                    retVal = true;
                }
            }
        }
        else
        {
            if( minute[0] >= '0' && minute[0] <= '9' )
            {
                retVal = true;
            }
        }

    }
    return( retVal);
}

//
// ************************************************************************
//
// ---------- int check4Action()
//
// ************************************************************************
//

int check4Action( int nowMinutes )
{
  int i;
  int retVal = 0;
  time_t secsSinceEpoch;
//  short nowMinutes;
  short chkMinutesFrom, chkMinutesTo;
  time_t utc;

//  utc = now();
//  secsSinceEpoch = CE.toLocal(utc, &tcr);
//  nowMinutes = ( hour(secsSinceEpoch) * 60 ) + minute(secsSinceEpoch);

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "check4Action: check for minute-value: %d\n", nowMinutes );
  }
#endif // DO_LOG

  //  for( i = 0; i < MAX_ACTION_TABLE_LINES; i++ )
  for ( i = 0; i < CONNECTED_RELAIS; i++ )
  {


#ifdef DO_LOG
    if ( !beQuiet )
    {
Logger.Log(LOGLEVEL_DEBUG," [%d] Enable: %d, mode: %s, hr1from: %s min1from: %s hr1to: %s min1to: %s hr2from: %s min2from: %s hr2to: %s min2to: %s \n",
	i, tblEntry[i].enabled_1, tblEntry[i].mode.c_str(), tblEntry[i].hourFrom_1.c_str(), tblEntry[i].minuteFrom_1.c_str(),
tblEntry[i].hourTo_1.c_str(), tblEntry[i].minuteTo_1.c_str(), tblEntry[i].hourFrom_2.c_str(), tblEntry[i].minuteFrom_2.c_str(),
tblEntry[i].hourTo_2.c_str(), tblEntry[i].minuteTo_2.c_str() ); 
    }
#endif // DO_LOG


      if ( tblEntry[i].enabled_1 )
      {

        if ( tblEntry[i].mode.equalsIgnoreCase("auto") )
        {

          chkMinutesFrom = (atoi(tblEntry[i].hourFrom_1.c_str()) * 60) + atoi(tblEntry[i].minuteFrom_1.c_str());
          chkMinutesTo = (atoi(tblEntry[i].hourTo_1.c_str()) * 60) + atoi(tblEntry[i].minuteTo_1.c_str());

          if ( nowMinutes >= chkMinutesFrom )
          {
#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG,
                         (const char*) "action is time 1 - Begin %s:%s [%d] port %d. Actionflag is %d\n",
                         tblEntry[i].hourFrom_1.c_str(), tblEntry[i].minuteFrom_1.c_str(),
                         chkMinutesFrom, i, tblEntry[i].actionFlag);
            }
#endif // DO_LOG

            if ( (tblEntry[i].actionFlag & ACTION_FLAG_ACTIVE) == 0 )
            {
              if( runMode == RUN_MODE_EXT1 || runMode == RUN_MODE_EXT2 ) 
              {
                if( tblEntry[i].extEnable_1 || tblEntry[i].extEnable_2 )
                {
                  switchRelais(i, RELAIS_ON);
                  if( runMode == RUN_MODE_EXT1 )
                  {
                    tblEntry[i].actionFlag |= ACTION_FLAG_MODE_EXT1;
                  }
                  else
                  {
                    tblEntry[i].actionFlag |= ACTION_FLAG_MODE_EXT2;
                  }
                }
              }
              else
              {
                switchRelais(i, RELAIS_ON);
#ifdef DO_LOG
                if ( !beQuiet )
                {
                  Logger.Log(LOGLEVEL_DEBUG, (const char*) "set actionFlag for time 1 to active\n");
                }
#endif // DO_LOG
              }
            }
          }

          if( nowMinutes >= chkMinutesTo )
          {
#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG,
                         (const char*) "action is time 1 - End %s:%s [%d] port %d. Actionflag is %d\n",
                         tblEntry[i].hourTo_1.c_str(), tblEntry[i].minuteTo_1.c_str(),
                         chkMinutesTo, i, tblEntry[i].actionFlag);
            }
#endif // DO_LOG

            if ( (tblEntry[i].actionFlag & ACTION_FLAG_ACTIVE) != 0 )
            {
              if( runMode != RUN_MODE_EXT1 && runMode != RUN_MODE_EXT2 ) 
              {

                switchRelais(i, RELAIS_OFF);

#ifdef DO_LOG
                if ( !beQuiet )
                {
                  Logger.Log(LOGLEVEL_DEBUG, (const char*) "set actionFlag for time 1 to inactive\n");
                }
#endif // DO_LOG
              }
            }
            else
            {
#ifdef DO_LOG
              if ( !beQuiet )
              {
                Logger.Log(LOGLEVEL_DEBUG, (const char*) "nothing to do ... actionFlag for time 1 is already inactive\n");
              }
#endif // DO_LOG
            }
          }
        }
        else // if( tblEntry[i].mode.equalsIgnoreCase("auto") )
        {
#ifdef DO_LOG
          if ( !beQuiet )
          {
            Logger.Log(LOGLEVEL_DEBUG, (const char*) "... entry for port %d time #1 is not auto\n", i);
          }
#endif // DO_LOG

          if ( tblEntry[i].mode.equalsIgnoreCase("on") )
          {
            alwaysOn(i);
          }
          else
          {
            if ( tblEntry[i].mode.equalsIgnoreCase("off") )
            {
              alwaysOff(i);
            }
          }
        }
      }
      else // if( tblEntry[i].enabled_1 )
      {
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "nothing to do ... entry for port %d time #1 disabled\n", i);
        }
#endif // DO_LOG
      }

      if ( tblEntry[i].enabled_2 )
      {
        if ( tblEntry[i].mode.equalsIgnoreCase("auto") )
        {
          chkMinutesFrom = (atoi(tblEntry[i].hourFrom_2.c_str()) * 60) + atoi(tblEntry[i].minuteFrom_2.c_str());
          chkMinutesTo = (atoi(tblEntry[i].hourTo_2.c_str()) * 60) + atoi(tblEntry[i].minuteTo_2.c_str());

          if( nowMinutes >= chkMinutesFrom )
          {
#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG,
                         (const char*) "action is time 2 - Begin %s:%s [%d] port %d. Actionflag is %d\n",
                         tblEntry[i].hourFrom_2.c_str(), tblEntry[i].minuteFrom_2.c_str(),
                         chkMinutesFrom, i tblEntry[i].actionFlag);
            }
#endif // DO_LOG

            if ( (tblEntry[i].actionFlag & ACTION_FLAG_ACTIVE) == 0 )
            {
              if( runMode != RUN_MODE_AUTO ) 
              {

// #ifdef DO_LOG
//                 if ( !beQuiet )
//                 {
                  Logger.Log(LOGLEVEL_DEBUG, "check4Action -> Actionflag = %d and runMode=%d[%s]\n",
                             tblEntry[i].actionFlag, runMode, runMode == RUN_MODE_EXT1?"EXT1":"EXT2" );
//                 }
// #endif // DO_LOG

                if( tblEntry[i].extEnable_1 || tblEntry[i].extEnable_2 )
                {
                  switchRelais(i, RELAIS_ON);
                  if( runMode == RUN_MODE_EXT1 )
                  {
                    tblEntry[i].actionFlag |= ACTION_FLAG_MODE_EXT1;
                  }
                  else
                  {
                    tblEntry[i].actionFlag |= ACTION_FLAG_MODE_EXT2;
                  }
                }
              }
              else
              {
                switchRelais(i, RELAIS_ON);

#ifdef DO_LOG
                if ( !beQuiet )
                {
                  Logger.Log(LOGLEVEL_DEBUG, (const char*) "set actionFlag for time 2 to active\n");
                }
#endif // DO_LOG
              }
            }
          }

          if( nowMinutes >= chkMinutesTo )
          {
#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG,
                         (const char*) "action is time 1 - End %s:%s [%d] port %d. Actionflag is %d\n",
                         tblEntry[i].hourTo_2.c_str(), tblEntry[i].minuteTo_2.c_str(),
                         chkMinutesTo, i, tblEntry[i].actionFlag);
            }
#endif // DO_LOG

            if ( (tblEntry[i].actionFlag & ACTION_FLAG_ACTIVE) != 0 )
            {
              if( runMode == RUN_MODE_AUTO )
              {
                switchRelais(i, RELAIS_OFF);
// #ifdef DO_LOG
//                 if ( !beQuiet )
//                 {
                     Logger.Log(LOGLEVEL_DEBUG, "Run mode Auto ... switch off\n");
//                 }
// #endif // DO_LOG
              }
              else
              {
// #ifdef DO_LOG
//                 if ( !beQuiet )
//                 {
                     Logger.Log(LOGLEVEL_DEBUG, "Run mode is NOT auto - no need to switch\n");
//                 }
// #endif // DO_LOG
              }
            }

#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG, (const char*) "set actionFlag for time 2 to inactive\n");
            }
#endif // DO_LOG

          }
          else
          {
#ifdef DO_LOG
            if ( !beQuiet )
            {
              Logger.Log(LOGLEVEL_DEBUG, (const char*) "nothing to do ... actionFlag for time 2 is already inactive\n");
            }
#endif // DO_LOG
          }
        }
        else // if( tblEntry[tmRow-1].mode.equalsIgnoreCase("auto") )
        {
#ifdef DO_LOG
          if ( !beQuiet )
          {
            Logger.Log(LOGLEVEL_DEBUG, (const char*) "... entry for port %d time #2 is not auto\n", i);
          }
#endif // DO_LOG

          if ( tblEntry[i].mode.equalsIgnoreCase("on") )
          {
            alwaysOn(i);
          }
          else
          {
            alwaysOff(i);
          }
        }
      }
      else // if( tblEntry[i].enabled_2 )
      {
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "nothing to do ... entry for port %d time #2 disabled\n", i);
        }
#endif // DO_LOG
      }
  }

  return ( retVal );
}

//
// ************************************************************************
//
// ---------- int handleUpdateEx()
//
// ************************************************************************
//
int handleUpdateEx( const char* updateUrl )
{
    t_httpUpdate_return retVal;

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int handleUpdate()
//
// ************************************************************************
//
int handleUpdate( void )
{
  static t_httpUpdate_return retVal;
  String newFirmwareFile, markerFile;
  HTTPClient http;
  int httpCode;

  //    ESPhttpUpdate.rebootOnUpdate( true );

  newFirmwareFile = updateUrl + String("firmware.bin") + String(lastNumber + 1);
  markerFile = updateUrl + String("firmware.") + String(lastNumber + 1);

  http.begin(markerFile.c_str());
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK)
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "marker file %s exists\n", markerFile.c_str());
    }
#endif // DO_LOG

    http.end();
    lastNumber++;
    storeUpdateInfo();

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "Handle update, next firmware is: %s\n", newFirmwareFile.c_str());
    }
#endif // DO_LOG
    retVal = ESPhttpUpdate.update(newFirmwareFile.c_str());

    //        retVal = ESPhttpUpdate.update(newFirmwareFile.c_str());

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "Update done retval was %x\n", retVal);
    }
#endif // DO_LOG


    //        switch( (retVal = ESPhttpUpdate.update(newFirmwareFile.c_str())) )
    switch ( retVal )
    {
      case HTTP_UPDATE_FAILED:
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "HTTP_UPDATE_FAILED! Error (%d): %s\n",
                     ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        }
#endif // DO_LOG
        break;
      case HTTP_UPDATE_NO_UPDATES:
        //                lastNumber++;
        //                storeUpdateInfo();
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "HTTP_UPDATE_NO_UPDATES\n");
        }
#endif // DO_LOG
        break;
      case HTTP_UPDATE_OK:
        //                lastNumber++;
        //                storeUpdateInfo();
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "HTTP_UPDATE_OK - lastNumber = %d\n", lastNumber);
        }
#endif // DO_LOG
        break;
      default:
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "Unknown retval %d\n", (int) retVal);
        }
#endif // DO_LOG
        break;
    }
  }
  else
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "NO marker file %s\n", markerFile.c_str());
    }
#endif // DO_LOG

    http.end();
  }

  return ((int) retVal);
}

//
// ************************************************************************
//
// ---------- void check4ExtMode( void )
//
// ************************************************************************
//
void check4ExtMode( void )
{    
  int currLine;
  bool hasSwitched = false;

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "START EXT1 mode ...\n");
        
//    }
// #endif // DO_LOG

  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    if( tblEntry[currLine].extEnable_1 )
    {
      hasSwitched = true;
      switchRelais(currLine, RELAIS_ON);
      tblEntry[currLine].actionFlag |= ACTION_FLAG_MODE_EXT1;
    }

    if( tblEntry[currLine].extEnable_1 )
    {
      hasSwitched = true;
      switchRelais(currLine, RELAIS_ON);
      tblEntry[currLine].actionFlag |= ACTION_FLAG_MODE_EXT1;
    }


  }

  if( hasSwitched )
  {
    runMode = RUN_MODE_EXT1;
  }

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "has switched = %s, runMode = %d[%s]\n",
                   hasSwitched == true?"true":"false", runMode, 
                   runMode == RUN_MODE_EXT1?"EXT1":"EXT2" );
//      }
// #endif // DO_LOG

}

//
// ************************************************************************
//
// ---------- void startExt1Mode
//
// ************************************************************************
//
void startExt1Mode( void )
{    
  int currLine;
  bool hasSwitched = false;

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "START EXT1 mode ...\n");
        
//    }
// #endif // DO_LOG

  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    if( tblEntry[currLine].extEnable_1 )
    {
      hasSwitched = true;
      switchRelais(currLine, RELAIS_ON);
      tblEntry[currLine].actionFlag |= ACTION_FLAG_MODE_EXT1;
    }
  }

  if( hasSwitched )
  {
    runMode = RUN_MODE_EXT1;
  }

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) "has switched = %s, runMode = %d[%s]\n",
                     hasSwitched == true?"true":"false", runMode, 
                     runMode == RUN_MODE_EXT1?"EXT1":"EXT2" );                   
//      }
// #endif // DO_LOG

}

//
// ************************************************************************
//
// ---------- void startExt2Mode
//
// ************************************************************************
//
void startExt2Mode( void )
{
  int currLine;
  bool hasSwitched = false;

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "START EXT2 mode ...\n");
        
//    }
// #endif // DO_LOG

  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    if( tblEntry[currLine].extEnable_2 )
    {
      hasSwitched = true;
      switchRelais(currLine, RELAIS_ON);
      tblEntry[currLine].actionFlag |= ACTION_FLAG_MODE_EXT2;
    }
  }

  if( hasSwitched )
  {
    runMode = RUN_MODE_EXT2;
  }

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "has switched = %s, runMode = %d\n",
                   hasSwitched == true?"true":"false", runMode);
        
//    }
// #endif // DO_LOG

}

//
// ************************************************************************
//
// ---------- void stopExt1Mode
//
// ************************************************************************
//
void stopExt1Mode( void )
{
  int currLine;
  bool hasSwitched = false;
  
// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "STOP EXT1 mode ...\n");
        
//    }
// #endif // DO_LOG

  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    if( tblEntry[currLine].extEnable_1 )
    {
      hasSwitched = true;
      switchRelais(currLine, RELAIS_OFF);
      tblEntry[currLine].actionFlag &= ~ACTION_FLAG_MODE_EXT1;
    }
  }

  if( hasSwitched )
  {
    runMode = RUN_MODE_AUTO;
    startupActions();
  }

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "has switched = %s, runMode = %d\n",
                   hasSwitched == true?"true":"false", runMode);
        
//    }
// #endif // DO_LOG

}

//
// ************************************************************************
//
// ---------- void stopExt2Mode
//
// ************************************************************************
//
void stopExt2Mode( void )
{
  int currLine;
  bool hasSwitched = false;

// #ifdef DO_LOG
//   if ( !beQuiet )
//   {
       Logger.Log(LOGLEVEL_DEBUG, (const char*) "STOP EXT2 mode ...\n");
        
//   }
// #endif // DO_LOG

  for ( currLine = 0; currLine < CONNECTED_RELAIS; currLine++ )
  {
    if( tblEntry[currLine].extEnable_2 )
    {
      hasSwitched = true;
      switchRelais(currLine, RELAIS_OFF);
      tblEntry[currLine].actionFlag &= ~ACTION_FLAG_MODE_EXT2;
    }
  }

  if( hasSwitched )
  {
    runMode = RUN_MODE_AUTO;
    startupActions();
  }

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "has switched = %s, runMode = %d\n",
                   hasSwitched == true?"true":"false", runMode);
        
//    }
// #endif // DO_LOG

}

//
// ************************************************************************
//
// ---------- int processUDPRequest( const char* requestFrom, String &udpRequest, int payloadLength )
//
// ************************************************************************
//
int processUDPRequest( const char* requestFrom, String &udpRequest, int payloadLength )
{
  int retVal = E_SUCCESS;

  if( udpRequest.equalsIgnoreCase(UDP_REQUEST_EXT1_START) )
  {
    startExt1Mode();
  }
  else
  {
    if( udpRequest.equalsIgnoreCase(UDP_REQUEST_EXT2_START) )
    {
      startExt2Mode();
    }
    else
    {
      if( udpRequest.equalsIgnoreCase(UDP_REQUEST_EXT1_STOP) )
      {
        stopExt1Mode();
      }
      else
      {
        if( udpRequest.equalsIgnoreCase(UDP_REQUEST_EXT2_STOP) )
        {
          stopExt2Mode();
        }
        else
        {
          retVal = UDP_UNKNOWN_REQUEST;
        }
      }
    }
  }

  return( retVal );
}

//
// ************************************************************************
//
// ---------- void loop()
//
// ************************************************************************
//
#define HOUR_4_UPDATE_CHECK   9
#define MULTICAST_CHECK_INTERVAL_MS 1000

void loop()
{
  static short wdayLastUpdateCheck;
  static short hourLastUpdateCheck;
  static long lastMulticastCheck;
  time_t secsSinceEpoch;
  short nowMinutes;
  static long minutesLastCheck;
  static int noBytes, message_size;
  static char packetBuffer[128];
  time_t utc;
  String remoteIP;
  IPAddress requestFromIP;
  String dataString;

  server.handleClient();
  delay(2);
  
  if( millis() - lastMulticastCheck >= MULTICAST_CHECK_INTERVAL_MS )
  {

    noBytes= multicastInstance.parsePacket(); //// returns the size of the packet
    if ( noBytes>0 && noBytes<20)
    {
      requestFromIP = multicastInstance.remoteIP();
      remoteIP = requestFromIP.toString();

// #ifdef DO_LOG
//      if ( !beQuiet )
//      {
        Logger.Log(LOGLEVEL_DEBUG, (const char*) "Multicast request received, size = %d\n", noBytes );
        
//    }
// #endif // DO_LOG

      message_size=multicastInstance.read(packetBuffer,64); // read the packet into the buffer

      if (message_size>0)
      {
        packetBuffer[message_size]=0; //// null terminate
        dataString = String( (const char*) packetBuffer);

        processUDPRequest( remoteIP.c_str(), dataString, message_size );

// #ifdef DO_LOG
//         if ( !beQuiet )
//         {
          Logger.Log(LOGLEVEL_DEBUG, (const char*) " - data: %s\n", packetBuffer);
//         }
// #endif // DO_LOG
      }
    }
 
    lastMulticastCheck = millis();

  } // millis() - lastMulticastCheck >= MULTICAST_CHECK_INTERVAL_MS
  else
  {
    server.handleClient();
    delay(2);
  }
  
  utc = now();
  secsSinceEpoch = CE.toLocal(utc, &tcr);
  nowMinutes = ( hour(secsSinceEpoch) * 60 ) + minute(secsSinceEpoch);

  if ( nowMinutes != minutesLastCheck )
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "loop: hour is %d, minutes is %d\n", hour(secsSinceEpoch), minute(secsSinceEpoch) );
    }
#endif // DO_LOG

#ifdef NEVER_EVER_DEF

    if( minutesLastCheck == 0 )
    {
        check4Action(nowMinutes);
    }
    else
    {
        while( minutesLastCheck <= nowMinutes )
        {
            minutesLastCheck++;
            check4Action(minutesLastCheck);
        }

//        handleUpdate();  // NACH TEST WIEDER RAUS!!
    }

#else

    check4Action(nowMinutes);
    
#endif // NEVER_EVER_DEF
    
    minutesLastCheck = nowMinutes;
  }
  else
  {
    server.handleClient();
    delay(2);
  }

  // the weekday now (Sunday is day 1)
  if ( weekday(secsSinceEpoch) != wdayLastUpdateCheck )
  {
    if( hour(secsSinceEpoch) == HOUR_4_UPDATE_CHECK )
    {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "loop: day has changed from %d to %d and time(%d) has come\n",
                 wdayLastUpdateCheck, weekday(secsSinceEpoch), HOUR_4_UPDATE_CHECK);
    }
#endif // DO_LOG
      handleUpdate();
      wdayLastUpdateCheck = weekday(secsSinceEpoch);
    }
  }
  
  server.handleClient();
  delay(2);
  
}


/* ************** ++++++++++ ************* +++++++++++++++ */

void doTimeOnSelect( char *sTarget, char *sSwitch, char *sBgColor, int tmNum, char* sRow, const char *sHour, const char* sMinute )
{
  char sNum[10];
  sprintf(sNum, "%d", tmNum);

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log( LOGLEVEL_DEBUG, (const char*) "sMinute is %s\n", sMinute );
  }
#endif // DO_LOG

  pageContent += "<td align=\"center\" bgcolor=\"";
  // loght cyan #07F479  or grey #D4C9C9
  pageContent += sBgColor;
  pageContent += "\">";

  pageContent += "<label>";
  // einschalten/ausschalten
  pageContent += sSwitch;

  pageContent += "</label><br>";

  // Stunde

  pageContent += "<input type=\"textdate\" name=\"h";
  pageContent += sTarget;        // = "from" or "to"
  pageContent += String(sNum);   // = 1 or 2
  pageContent += String("_");    //
  pageContent += String(sRow);   // = 1 - 8


  pageContent += "\" value=\"";
  pageContent += String(sHour);
  pageContent += "\" size=\"2\" maxlength=\"2\" >";

  pageContent += " Uhr ";

  // Minute

  pageContent += "<input type=\"textdate\" name=\"m";
  pageContent += sTarget;        // = "from" or "to"
  pageContent += String(sNum);   // = 1 or 2
  pageContent += String("_");    //
  pageContent += String(sRow);   // = 1 - 8

  pageContent += "\" value=\"";
  pageContent += String(sMinute);
  pageContent += "\" size=\"2\" maxlength=\"2\" >";

  pageContent += "</td>";

}

void doTimeSelect( int tmNum, int tmRow, char* sRow )
{
  if ( tmNum == 1 )
  {
    doTimeOnSelect( "from", "einschalten", "#07F479", tmNum, sRow,
                    tblEntry[tmRow - 1].hourFrom_1.c_str(), tblEntry[tmRow - 1].minuteFrom_1.c_str() );

    doTimeOnSelect( "to", "ausschalten", "#D4C9C9" , tmNum, sRow,
                    tblEntry[tmRow - 1].hourTo_1.c_str(), tblEntry[tmRow - 1].minuteTo_1.c_str() );
  }
  else
  {
    if ( tmNum == 2 )
    {
      doTimeOnSelect( "from", "einschalten", "#07F479", tmNum, sRow,
                    tblEntry[tmRow - 1].hourFrom_2.c_str(), tblEntry[tmRow - 1].minuteFrom_2.c_str() );

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "doTimeSelect -> tblEntry[%d].minuteFrom_2.c_str(): %s\n",
               (tmRow - 1), tblEntry[tmRow - 1].minuteFrom_2.c_str() );
  }
#endif // DO_LOG

      doTimeOnSelect( "to", "ausschalten", "#D4C9C9" , tmNum, sRow,
                    tblEntry[tmRow - 1].hourTo_2.c_str(), tblEntry[tmRow - 1].minuteTo_2.c_str() );
#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "doTimeSelect -> tblEntry[%d].minuteTo_2.c_str(): %s\n",
               (tmRow - 1), tblEntry[tmRow - 1].minuteTo_2.c_str() );
  }
#endif // DO_LOG

    }
  }
}


void doCreateLine( int tmRow )
{
  char sRow[10];
  sprintf(sRow, "%d", tmRow);


  pageContent += "<td>";
  pageContent += "<input type=\"text\" name=\"bezeichner";
  pageContent += sRow;

  pageContent += "\" value=\"";
  pageContent += tblEntry[tmRow - 1].name;
  pageContent += "\" size=\"4\" maxlength=\"10\">";
  //
  pageContent += "</td>";

  // ---- ++++++++++ ZEIT 1 enable/disable ++++++++++ -----

  pageContent += "<td bgcolor=\"#09CEF3\">";
  //
  pageContent += "<input type=\"checkbox\" name=\"enabled1_";
  pageContent += sRow;
  pageContent += "\" value=\"aktiv\" ";
  if ( tblEntry[tmRow - 1].enabled_1 )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\" >";
  //
  pageContent += "<label>Zeit 1</label>";
  pageContent += "</td>";
  doTimeSelect( 1, tmRow, sRow);

  // ---- ++++++++++ ZEIT 2 enable/disable ++++++++++ -----

  pageContent += "<td bgcolor=\"#09CEF3\">";
  //
  pageContent += " <input type=\"checkbox\" name=\"enabled2_";
  pageContent += sRow;
  pageContent += "\" value=\"aktiv\" ";
  if ( tblEntry[tmRow - 1].enabled_2 )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\" >";
  //
  pageContent += "<label>Zeit 2</label>";
  pageContent += "</td>";
  doTimeSelect( 2, tmRow, sRow);

  pageContent += "<td>";

  // ---- ++++++++++ EXT 1 enable/disable ++++++++++ -----

  pageContent += "<input type=\"checkbox\" name=\"ext1_";
  pageContent += sRow;
  pageContent += "\" value=\"aktiv\" ";
  if ( tblEntry[tmRow - 1].extEnable_1 )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\">";
  //
  pageContent += "<label>Ext 1</label>";
  pageContent += "<br>";

  // ---- ++++++++++ EXT 2 enable/disable ++++++++++ -----

  pageContent += "<input type=\"checkbox\" name=\"ext2_";
  pageContent += sRow;
  pageContent += "\" value=\"aktiv\" ";
  if ( tblEntry[tmRow - 1].extEnable_2 )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\">";
  pageContent += "<label>Ext 2</label>";

  pageContent += "</td>";
  pageContent += "<td>";

  // ---- ++++++++++ MODE ON ++++++++++ -----

  pageContent += "<input type=\"radio\" name=\"mode";
  pageContent += sRow;
  pageContent += "\" value=\"on\" ";
  if ( tblEntry[tmRow - 1].mode.equalsIgnoreCase("on") )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\">";
  pageContent += "<label>Ein</label>";
  pageContent += "<br>";

  // ---- ++++++++++ MODE OFF ++++++++++ -----

  pageContent += "<input type=\"radio\" name=\"mode";
  pageContent += sRow;
  pageContent += "\" value=\"off\" ";
  if ( tblEntry[tmRow - 1].mode.equalsIgnoreCase("off") )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\">";
  pageContent += "<label>Aus</label>";
  pageContent += "<br>";

  // ---- ++++++++++ MODE AUTO ++++++++++ -----

  pageContent += "<input type=\"radio\" name=\"mode";
  pageContent += sRow;
  pageContent += "\" value=\"auto\" ";
  if ( tblEntry[tmRow - 1].mode.equalsIgnoreCase("auto") )
  {
    pageContent += " checked ";
  }
  pageContent += "size=\"1\">";
  pageContent += "<label>Auto</label>";
  // ---------
  pageContent += "</td>";

  // end Zeile ...

}


void setupPage()
{

  int i;
  IPAddress localIP;

  pageContent = "";
  localIP = WiFi.localIP();

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "handleIndexPage\n");
  }
#endif // DO_LOG

  for (i = 0; i < server.args(); i++ )
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "%s = %s\n", server.argName(i).c_str(), server.arg(i).c_str() );
    }
#endif // DO_LOG
  }

  if ( server.method() == SERVER_METHOD_POST )
    //        server.hasArg(INDEX_BUTTONNAME_ADMIN)  )
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "POST REQUEST\n");
    }
#endif // DO_LOG
  }
  else
  {
#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "GET REQUEST\n");
    }
#endif // DO_LOG
  }

  if ( server.hasArg("submit") && server.arg("submit").equalsIgnoreCase("speichern") )

  {
    // for( i = 0; i < MAX_ACTION_TABLE_LINES; i++ )
    for ( i = 0; i < CONNECTED_RELAIS; i++ )
    {

      formFieldName[KW_IDX_BEZEICHNER] = String("bezeichner") + String(i+1);
      formFieldName[KW_IDX_ENABLED_1]  = String("enabled1_")  + String(i+1);
      formFieldName[KW_IDX_HFROM_1]    = String("hfrom1_")    + String(i+1);
      formFieldName[KW_IDX_MFROM_1]    = String("mfrom1_")    + String(i+1);
      formFieldName[KW_IDX_HTO_1]      = String("hto1_")      + String(i+1);
      formFieldName[KW_IDX_MTO_1]      = String("mto1_")      + String(i+1);
      formFieldName[KW_IDX_ENABLED_2]  = String("enabled2_")  + String(i+1);
      formFieldName[KW_IDX_HFROM_2]    = String("hfrom2_")    + String(i+1);
      formFieldName[KW_IDX_MFROM_2]    = String("mfrom2_")    + String(i+1);
      formFieldName[KW_IDX_HTO_2]      = String("hto2_")      + String(i+1);
      formFieldName[KW_IDX_MTO_2]      = String("mto2_")      + String(i+1);
      formFieldName[KW_IDX_EXT_1]      = String("ext1_")      + String(i+1);
      formFieldName[KW_IDX_EXT_2]      = String("ext2_")      + String(i+1);
      formFieldName[KW_IDX_MODE]       = String("mode")       + String(i+1);


      tblEntry[i].name         = server.arg(formFieldName[KW_IDX_BEZEICHNER]);

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].name         = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_BEZEICHNER].c_str(), server.arg(formFieldName[KW_IDX_BEZEICHNER]).c_str());
    }
#endif // DO_LOG

      tblEntry[i].mode         = server.arg(formFieldName[KW_IDX_MODE]);

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].mode         = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_MODE].c_str(), server.arg(formFieldName[KW_IDX_MODE]).c_str());
    }
#endif // DO_LOG

      if( isValidHour( server.arg(formFieldName[KW_IDX_HFROM_1]).c_str()) )
      {
          tblEntry[i].hourFrom_1   = server.arg(formFieldName[KW_IDX_HFROM_1]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*)"tblEntry[%d].hourFrom_1    = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_HFROM_1].c_str(), server.arg(formFieldName[KW_IDX_HFROM_1]).c_str());
    }
#endif // DO_LOG

      if( isValidMinute(server.arg(formFieldName[KW_IDX_MFROM_1]).c_str()) )
      {
          tblEntry[i].minuteFrom_1 = server.arg(formFieldName[KW_IDX_MFROM_1]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].minuteFrom_1 = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_MFROM_1].c_str(), server.arg(formFieldName[KW_IDX_MFROM_1]).c_str());
    }
#endif // DO_LOG

      if( isValidHour( server.arg(formFieldName[KW_IDX_HTO_1]).c_str()) )
      {
          tblEntry[i].hourTo_1     = server.arg(formFieldName[KW_IDX_HTO_1]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*)"tblEntry[%d].hourTo_1      = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_HTO_1].c_str(), server.arg(formFieldName[KW_IDX_HTO_1]).c_str());
    }
#endif // DO_LOG

      if( isValidMinute( server.arg(formFieldName[KW_IDX_MTO_1]).c_str()) )
      {
          tblEntry[i].minuteTo_1   = server.arg(formFieldName[KW_IDX_MTO_1]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].minuteTo_1   = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_MTO_1].c_str(), server.arg(formFieldName[KW_IDX_MTO_1]).c_str());
    }
#endif // DO_LOG

      if( isValidHour( server.arg(formFieldName[KW_IDX_HFROM_2]).c_str()) )
      {
          tblEntry[i].hourFrom_2   = server.arg(formFieldName[KW_IDX_HFROM_2]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].hourFrom_2   = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_HFROM_2].c_str(), server.arg(formFieldName[KW_IDX_HFROM_2]).c_str());
    }
#endif // DO_LOG

      if( isValidMinute( server.arg(formFieldName[KW_IDX_MFROM_2]).c_str()) )
      {
          tblEntry[i].minuteFrom_2 = server.arg(formFieldName[KW_IDX_MFROM_2]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].minuteFrom_2 = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_MFROM_2].c_str(), server.arg(formFieldName[KW_IDX_MFROM_2]).c_str());
    }
#endif // DO_LOG

      if( isValidHour( server.arg(formFieldName[KW_IDX_HTO_2]).c_str()) )
      {
          tblEntry[i].hourTo_2     = server.arg(formFieldName[KW_IDX_HTO_2]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].hourTo_2     = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_HTO_2].c_str(), server.arg(formFieldName[KW_IDX_HTO_2]).c_str());
    }
#endif // DO_LOG

      if( isValidMinute( server.arg(formFieldName[KW_IDX_MTO_2]).c_str()) )
      {
          tblEntry[i].minuteTo_2   = server.arg(formFieldName[KW_IDX_MTO_2]);
      }

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].minuteTo_2   = server.arg(\"%s\") ->%s\n", 
                  i, formFieldName[KW_IDX_MTO_2].c_str(), server.arg(formFieldName[KW_IDX_MTO_2]).c_str());
    }
#endif // DO_LOG


      if( isValidHour( server.arg(formFieldName[KW_IDX_HFROM_1]).c_str()) &&
          isValidMinute( server.arg(formFieldName[KW_IDX_MFROM_1]).c_str()) &&
          isValidHour( server.arg(formFieldName[KW_IDX_HTO_1]).c_str()) &&
          isValidMinute( server.arg(formFieldName[KW_IDX_MTO_1]).c_str()) )
      {
          tblEntry[i].enabled_1   = 
                      server.arg(formFieldName[KW_IDX_ENABLED_1]).equalsIgnoreCase("aktiv") ? true : false;

#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].enabled_1   = server.arg(\"%s\") -> %d\n", 
                      i, formFieldName[KW_IDX_ENABLED_1].c_str(), tblEntry[i].enabled_1);
        }
#endif // DO_LOG
      }
      else
      {
          tblEntry[i].enabled_1   = false;
#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].enabled_1   = FALSE - error in time spec\n", i);
        }
#endif // DO_LOG
      }
      
      tblEntry[i].extEnable_1 = server.arg(formFieldName[KW_IDX_EXT_1]).equalsIgnoreCase("aktiv") ? true : false;

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].extEnable_1 = server.arg(\"%s\") -> %d\n", 
                  i, formFieldName[KW_IDX_EXT_1].c_str(), tblEntry[i].extEnable_1);
    }
#endif // DO_LOG
            

      if( isValidHour( server.arg(formFieldName[KW_IDX_HFROM_2]).c_str()) &&
          isValidMinute( server.arg(formFieldName[KW_IDX_MFROM_2]).c_str()) &&
          isValidHour( server.arg(formFieldName[KW_IDX_HTO_2]).c_str()) &&
          isValidMinute( server.arg(formFieldName[KW_IDX_MTO_2]).c_str()) )
      {
          tblEntry[i].enabled_2   = 
                    server.arg(formFieldName[KW_IDX_ENABLED_2]).equalsIgnoreCase("aktiv") ? true : false;

#ifdef DO_LOG
        if ( !beQuiet )
        {
          Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].enabled_2   = server.arg(\"%s\") -> %d\n", 
                      i, formFieldName[KW_IDX_ENABLED_2].c_str(), tblEntry[i].enabled_2);
        }
#endif // DO_LOG
      }
      else
      {
          Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].enabled_2   = FALSE - error in time spec\n", i);
      }
      
      tblEntry[i].extEnable_2 = server.arg(formFieldName[KW_IDX_EXT_2]).equalsIgnoreCase("aktiv") ? true : false;

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log( LOGLEVEL_DEBUG, (const char*) "tblEntry[%d].extEnable_2 = server.arg(\"%s\") -> %d\n", 
                  i, formFieldName[KW_IDX_EXT_2].c_str(), tblEntry[i].extEnable_2);
    }
#endif // DO_LOG
      

    }

    storeActionTable();
    eeprom.validate();

    startupActions();
    // return;

  }

  pageContent += "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">";
  pageContent += "<html>";
  pageContent += "<head>";
  pageContent += "<meta charset=\"utf-8\">";
  pageContent += "<title>Home</title>";

  pageContent += "<style> ";
  pageContent += "input[type=textdate] { width: 1.5em; }  ";
  pageContent += "</style>";

  pageContent += "</head>";

  pageContent += "<body bgcolor=\"#D4C9C9\" text=\"#000000\" link=\"#1E90FF\" vlink=\"#0000FF\">";
  pageContent += "<div align=\"center\"><strong><h1>WiFi Relais</h1></strong></div>";


  pageContent += "<div align=\"center\"><strong><h1>" + String(localIP.toString()) + "</h1></strong></div>";

  pageContent += "<form action=\"/\" method=\"get\">";
  pageContent += "<hr align=\"center\"><br>";
  pageContent += "<table border align=\"center\">";

  // Zeile 1
  pageContent += "<tr>";
  doCreateLine( 1 );
  pageContent += "</tr>";

  // Zeile 2
  pageContent += "<tr>";
  doCreateLine( 2 );
  pageContent += "</tr>";

  // Zeile 3
  pageContent += "<tr>";
  doCreateLine( 3 );
  pageContent += "</tr>";

  // Zeile 4
  pageContent += "<tr>";
  doCreateLine( 4 );
  pageContent += "</tr>";

  // Zeile 5
  pageContent += "<tr>";
  doCreateLine( 5 );
  pageContent += "</tr>";

  // Zeile 6
  pageContent += "<tr>";
  doCreateLine( 6 );
  pageContent += "</tr>";

  // Zeile 7
  pageContent += "<tr>";
  doCreateLine( 7 );
  pageContent += "</tr>";

  // Zeile 8
  pageContent += "<tr>";
  doCreateLine( 8 );
  pageContent += "</tr>";

  pageContent += "</table>";
  pageContent += "<hr align=\"center\">";
  pageContent += "<div align=\"center\">";
  pageContent += "<input type=\"submit\" name=\"submit\" value=\"speichern\">";
  pageContent += "<input type=\"reset\" name=\"reset\" value=\"reset\">";
  pageContent += "<input type=\"submit\" name=\"ESP\" value=\"reboot\">";
  pageContent += "</div>";
  pageContent += "<hr align=\"center\"><br>";
  pageContent += "</form>";
  pageContent += "</body>";
  pageContent += "</html>";

  server.send(200, "text/html", pageContent);

  //  Serial.print(pageContent.c_str());

}

//
// ************************************************************************
//
// ---------- Web API processing
//
// ************************************************************************
//
//
// ------------------------------------------------------------------------
//
// ---------- several global information needed to handle
//            calls of the Web-API
//
// ------------------------------------------------------------------------
//
// valid values for some table fields
//
#define API_VALUE_ENABLE   "enable"
#define API_VALUE_DISABLE  "disable"
#define API_VALUE_ON       "on"
#define API_VALUE_OFF      "off"
#define API_VALUE_AUTO     "auto"

//
// valid api keys that may be sent 
//
#define NUM_API_KEYWORDS    13

char *_apiKeywords[NUM_API_KEYWORDS] = {
  "update",
  "apikey",
  "port",
  "mode",
  "from1",
  "to1",
  "time1",
  "from2",
  "to2",
  "time2",
  "ext1",
  "ext2",
  NULL
};

//
// index for each keyword
//
#define API_KEYWORD_UPDATE    0
#define API_KEYWORD_APIKEY    1
#define API_KEYWORD_PORT      2
#define API_KEYWORD_MODE      3
#define API_KEYWORD_FROM1     4
#define API_KEYWORD_TO1       5
#define API_KEYWORD_TIME1     6
#define API_KEYWORD_FROM2     7
#define API_KEYWORD_TO2       8
#define API_KEYWORD_TIME2     9
#define API_KEYWORD_EXT1     10
#define API_KEYWORD_EXT2     11

//
// update[=http://url:port/file}
// apikey=xyz
// port=1-8
// mode[=on/off/auto]
// from1[=12:12]
// to1[=12:12]
// time1[=enable/disable]
// from2[=14:14]
// to2[=14:14]
// time2[=enable/disable]
// ext1[=on/off/enable/disable]
// ext2[=on/off/enable/disable]
//


//
// resultcodes of api-calls
//
#define API_RESULT_SUCCESS              0
#define API_MSG_SUCCESS               ":OK"
#define API_RESULT_MISSING_PORT       119
#define API_MSG_MISSING_PORT          ":MISSING ARGUMENT"
#define API_RESULT_INVALID_MODE       120
#define API_MSG_INVALID_MODE          ":INVALID MODE"
#define API_RESULT_INVALID_TIMEVALUE  121
#define API_MSG_INVALID_TIMEVALUE     ":INVALID TIME"
#define API_RESULT_INVALID_MODE_TIME1 122
#define API_MSG_INVALID_MODE_TIME1     ":INVALID MODE FOR TIME #1"
#define API_RESULT_INVALID_MODE_TIME2 123
#define API_MSG_INVALID_MODE_TIME2    ":INVALID MODE FOR TIME #2"
#define API_RESULT_INVALID_MODE_EXT1  124
#define API_MSG_INVALID_MODE_EXT1     "INVALID MODE FOR EXT #1"
#define API_RESULT_INVALID_MODE_EXT2  125
#define API_MSG_INVALID_MODE_EXT2     "INVALID MODE FOR EXT #2"
#define API_RESULT_UNKNOWN_RESULTCODE 99
#define API_MSG_UNKNOWN_RESULTCODE    ":UNKNOWN"




//
// ************************************************************************
//
// ---------- int postTime1Mode( int port )
//
// ************************************************************************
//
int postTime1Mode( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";
    pageContent += _apiKeywords[API_KEYWORD_TIME1];
    pageContent += "=";

    if( tblEntry[port].enabled_1 )
    {
        pageContent += API_VALUE_ENABLE;
    }
    else
    {
        pageContent += API_VALUE_DISABLE;
    }
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postTime2Mode( int port )
//
// ************************************************************************
//
int postTime2Mode( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";
    pageContent += _apiKeywords[API_KEYWORD_TIME2];
    pageContent += "=";

    if( tblEntry[port].enabled_2 )
    {
        pageContent += API_VALUE_ENABLE;
    }
    else
    {
        pageContent += API_VALUE_DISABLE;
    }
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postTimeFrom1( int port )
//
// ************************************************************************
//
int postTimeFrom1( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";
    pageContent += _apiKeywords[API_KEYWORD_FROM1];
    pageContent += "=";
    pageContent += tblEntry[port].hourFrom_1;
    pageContent += ":";
    pageContent += tblEntry[port].minuteFrom_1;
    pageContent += "<br>";

    return( retVal );
}
                                        
//
// ************************************************************************
//
// ---------- int postTimeTo1( int port )
//
// ************************************************************************
//
int postTimeTo1( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";

    pageContent += _apiKeywords[API_KEYWORD_TO1];
    pageContent += "=";
    pageContent += tblEntry[port].hourTo_1;
    pageContent += ":";
    pageContent += tblEntry[port].minuteTo_1;
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postTimeFrom2( int port )
//
// ****************pageContent********************************************************
//
int postTimeFrom2( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";

    pageContent += _apiKeywords[API_KEYWORD_FROM2];
    pageContent += "=";
    pageContent += tblEntry[port].hourFrom_2;
    pageContent += ":";
    pageContent += tblEntry[port].minuteFrom_2;
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//pageContent
// ---------- int postTimeTo2( int port )
//
// ************************************************************************
//
int postTimeTo2( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";

    pageContent += _apiKeywords[API_KEYWORD_TO2];
    pageContent += "=";
    pageContent += tblEntry[port].hourTo_2;
    pageContent += ":";
    pageContent += tblEntry[port].minuteTo_2;
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postExt1( int port )
//
// ************************************************************************
//
int postExt1( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";

    pageContent += _apiKeywords[API_KEYWORD_EXT1];
    pageContent += "=";

    if( tblEntry[port].extEnable_1 )
    {
        pageContent += API_VALUE_ENABLE;
    }
    else
    {
        pageContent += API_VALUE_DISABLE;
    }
    pageContent += "<br>";

    return( retVal );
}
                                        
//
// ************************************************************************
//
// ---------- int postExt2( int port )
//
// ************************************************************************
//
int postExt2( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";

    pageContent += _apiKeywords[API_KEYWORD_EXT2];
    pageContent += "=";

    if( tblEntry[port].extEnable_2 )
    {
        pageContent += API_VALUE_ENABLE;
    }
    else
    {
        pageContent += API_VALUE_DISABLE;
    }
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postMode( int port )
//
// ************************************************************************
//
int postMode( int port )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";

    pageContent += _apiKeywords[API_KEYWORD_MODE];
    pageContent += "=";

    pageContent += tblEntry[port].mode;
    pageContent += "<br>";

    return( retVal );
}


//
// ************************************************************************
//
// ---------- int checkApiKey( const char* apiKey )
//
// ************************************************************************
//
int checkApiKey( const char* apiKey )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";
    pageContent += apiKey;
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postApiKey( const char* apiKey )
//
// ************************************************************************
//
int postApiKey( const char* apiKey )
{
    int retVal = API_RESULT_SUCCESS;

    postResult( retVal, false );
    pageContent += ":";
    pageContent += _apiKeywords[API_KEYWORD_APIKEY];
    pageContent += "=";
    pageContent +=  apiHashKey;
    pageContent += "<br>";

    return( retVal );
}

//
// ************************************************************************
//
// ---------- int postResult( int resultCode )
//
// ************************************************************************
//
int postResult( int resultCode, bool lineFeed )
{
    int retVal = API_RESULT_SUCCESS;

    pageContent += String(resultCode);

    switch( resultCode )
    {
        case API_RESULT_SUCCESS:
            pageContent += API_MSG_SUCCESS;
            break;
        case API_RESULT_MISSING_PORT:
            pageContent += API_MSG_MISSING_PORT;
            break;
        case API_RESULT_INVALID_MODE:
            pageContent += API_MSG_INVALID_MODE;
            break;
        case API_RESULT_INVALID_TIMEVALUE:
            pageContent += API_MSG_INVALID_TIMEVALUE;
            break;
        case API_RESULT_INVALID_MODE_TIME1:
            pageContent += API_MSG_INVALID_MODE_TIME1;
            break;
        case API_RESULT_INVALID_MODE_TIME2:
            pageContent += API_MSG_INVALID_MODE_TIME2;
            break;
        case API_RESULT_INVALID_MODE_EXT1:
            pageContent += API_MSG_INVALID_MODE_EXT1;
            break;
        case API_RESULT_INVALID_MODE_EXT2:
            pageContent += API_MSG_INVALID_MODE_EXT2;
            break;
        default:
            pageContent += API_MSG_UNKNOWN_RESULTCODE;
            break;
    }
    
    if( lineFeed )
    {
        pageContent += "<br>";
    }            

    return( retVal );
}







/*
 * Copyright (c) 2000-2002 Opsycon AB  (www.opsycon.se)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by Opsycon AB.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
 
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
 
#define MAXLN 200
#define ISSPACE " \t\n\r\f\v"
 
size_t
strcspn (const char *p, const char *s)
{
    int i, j;
 
    for (i = 0; p[i]; i++) {
        for (j = 0; s[j]; j++) {
            if (s[j] == p[i])
                break;
        }
        if (s[j])
            break;
    }
    return (i);
}
 
char *
_getbase(char *p, int *basep)
{
    if (p[0] == '0') {
        switch (p[1]) {
        case 'x':
            *basep = 16;
            break;
        case 't': case 'n':
            *basep = 10;
            break;
        case 'o':
            *basep = 8;
            break;
        default:
            *basep = 10;
            return (p);
        }
        return (p + 2);
    }
    *basep = 10;
    return (p);
}
 
/*
 *  _atob(vp,p,base)
 */
int
_atob (uint32_t *vp, char *p, int base)
{
    uint32_t value, v1, v2;
    char *q, tmp[20];
    int digit;
 
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
 
    if (base == 16 && (q = strchr (p, '.')) != 0) {
        if (q - p > sizeof(tmp) - 1)
            return (0);
 
        strncpy (tmp, p, q - p);
        tmp[q - p] = '\0';
        if (!_atob (&v1, tmp, 16))
            return (0);
 
        q++;
        if (strchr (q, '.'))
            return (0);
 
        if (!_atob (&v2, q, 16))
            return (0);
        *vp = (v1 << 16) + v2;
        return (1);
    }
 
    value = *vp = 0;
    for (; *p; p++) {
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f')
            digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F')
            digit = *p - 'A' + 10;
        else
            return (0);
 
        if (digit >= base)
            return (0);
        value *= base;
        value += digit;
    }
    *vp = value;
    return (1);
}
 
/*
 *  atob(vp,p,base)
 *      converts p to binary result in vp, rtn 1 on success
 */
int
atob(uint32_t *vp, char *p, int base)
{
    uint32_t v;
    if (base == 0)
        p = _getbase (p, &base);
    if (_atob (&v, p, base)) {
        *vp = v;
        return (1);
    }
    return (0);
}
 
/*
 *  vsscanf(buf,fmt,ap)
 */
 
int
vsscanf (const char *buf, const char *s, va_list ap)
{
    uint32_t             count, noassign, width, base, lflag;
    const char     *tc;
    char           *t, tmp[MAXLN];
 
    count = noassign = width = lflag = 0;
    while (*s && *buf) {
    while (isspace (*s))
        s++;
    if (*s == '%') {
        s++;
        for (; *s; s++) {
        if (strchr ("dibouxcsefg%", *s))
            break;
        if (*s == '*')
            noassign = 1;
        else if (*s == 'l' || *s == 'L')
            lflag = 1;
        else if (*s >= '1' && *s <= '9') {
            for (tc = s; isdigit (*s); s++);
            strncpy (tmp, tc, s - tc);
            tmp[s - tc] = '\0';
            atob (&width, tmp, 10);
            s--;
        }
        }
        if (*s == 's') {
        while (isspace (*buf))
            buf++;
        if (!width)
            width = strcspn (buf, ISSPACE);
        if (!noassign) {
            strncpy (t = va_arg (ap, char *), buf, width);
            t[width] = '\0';
        }
        buf += width;
        } else if (*s == 'c') {
        if (!width)
            width = 1;
        if (!noassign) {
            strncpy (t = va_arg (ap, char *), buf, width);
            t[width] = '\0';
        }
        buf += width;
        } else if (strchr ("dobxu", *s)) {
        while (isspace (*buf))
            buf++;
        if (*s == 'd' || *s == 'u')
            base = 10;
        else if (*s == 'x')
            base = 16;
        else if (*s == 'o')
            base = 8;
        else if (*s == 'b')
            base = 2;
        if (!width) {
            if (isspace (*(s + 1)) || *(s + 1) == 0)
            width = strcspn (buf, ISSPACE);
            else
            width = strchr (buf, *(s + 1)) - buf;
        }
        strncpy (tmp, buf, width);
        tmp[width] = '\0';
        buf += width;
        if (!noassign)
            atob (va_arg (ap, uint32_t *), tmp, base);
        }
        if (!noassign)
        count++;
        width = noassign = lflag = 0;
        s++;
    } else {
        while (isspace (*buf))
        buf++;
        if (*s != *buf)
        break;
        else
        s++, buf++;
    }
    }
    return (count);
}
 
 
int
sscanf (const char *buf, const char *fmt, ...)
{
    int             count;
    va_list ap;
   
    va_start (ap, fmt);
    count = vsscanf (buf, fmt, ap);
    va_end (ap);
    return (count);
}
 






















 
//
// ************************************************************************
//
// ---------- void processApiCall( int keyword, const char* arg )
//
// ************************************************************************
//
void processApiCall( int keyword, const char* arg )
{         
    // String apiKey ( around line #260 ) contains a hash code
    // that will be used as Web-api-key

    int retVal;
    char hourScan[3];
    char minScan[3];
     

    if( arg != NULL )
    {
        switch(keyword)
        {
            case API_KEYWORD_UPDATE:
                if( strlen(arg) > 0 )
                {
                    // extended update - arg is the name of
                    //                   update file to use
                    retVal = handleUpdateEx(arg);
                    postResult( retVal, true );
                }
                else
                {
                    // regular update - just call file check
                    retVal = handleUpdate();
                    postResult( retVal, true );
                }
                break;
            case API_KEYWORD_APIKEY:                            
                if( strlen(arg) > 0 )
                {
                    // check whether the argument is a valid
                    // hash-code
                    checkApiKey( arg );    
                }
                else
                {
                    // if no argument is given this is a
                    // request for the api-key, so send it back.
                    postApiKey( arg );    
                }
                break;
            case API_KEYWORD_PORT:
                if( strlen(arg) <= 0 )
                {        
                    postResult( API_RESULT_MISSING_PORT, true );
                }
                break;
            case API_KEYWORD_MODE:
                if( server.arg( _apiKeywords[API_KEYWORD_PORT] ).length() )
                {
                    int port = server.arg( _apiKeywords[API_KEYWORD_PORT] ).toInt() - 1;
                    
                    if(server.arg( _apiKeywords[API_KEYWORD_MODE] ).equalsIgnoreCase(API_VALUE_AUTO))
                    {
                        tblEntry[port].mode = API_VALUE_AUTO;
                        retVal = E_SUCCESS;
                        postResult( retVal, true );
                    }
                    else
                    {
                        if(server.arg( _apiKeywords[API_KEYWORD_MODE] ).equalsIgnoreCase(API_VALUE_OFF))
                        {
                            tblEntry[port].mode = API_VALUE_OFF;
                            retVal = E_SUCCESS;
                            postResult( retVal, true );
                        }
                        else
                        {
                            if(server.arg( _apiKeywords[API_KEYWORD_MODE] ).equalsIgnoreCase(API_VALUE_ON))
                            {
                                tblEntry[port].mode = API_VALUE_ON;
                                retVal = E_SUCCESS;
                                postResult( retVal, true );
                            }
                            else
                            {
                                if(server.arg( _apiKeywords[API_KEYWORD_MODE] ).length() )
                                {
                                    postResult( API_RESULT_INVALID_MODE, true );
                                }
                                else
                                {
                                    postMode( port );    
                                }
                            }
                        }
                    }
                }
                else
                {
                    postResult( API_RESULT_MISSING_PORT, true );
                }
                break;
            case API_KEYWORD_FROM1:
            case API_KEYWORD_TO1:
            case API_KEYWORD_FROM2:
            case API_KEYWORD_TO2:
                if( server.arg( _apiKeywords[API_KEYWORD_PORT] ).length() )
                {
                    int port = server.arg( _apiKeywords[API_KEYWORD_PORT] ).toInt() - 1;
                    
                    if( strlen(arg) > 0 )
                    {  
                        switch(keyword)
                        {
                            case API_KEYWORD_FROM1:
                                if( server.arg( _apiKeywords[API_KEYWORD_FROM1] ))
                                {
                                    retVal = sscanf(arg, "%2s:%2s",hourScan, minScan);
                                    if( isValidHour(hourScan) && isValidMinute(minScan) )
                                    {
                                        tblEntry[port].hourFrom_1 = hourScan;
                                        tblEntry[port].minuteFrom_1 = minScan;

                                        retVal = E_SUCCESS;
                                        postResult( retVal, true );                                      
                                    }
                                    else
                                    {
                                        retVal = API_RESULT_INVALID_TIMEVALUE;
                                        postResult( retVal, true );
                                    }
                                }
                                break;
                            case API_KEYWORD_TO1:
                                if( server.arg( _apiKeywords[API_KEYWORD_TO1] ))
                                {
                                    retVal = sscanf(arg, "%2s:%2s",hourScan, minScan);
                                    if( isValidHour(hourScan) && isValidMinute(minScan) )
                                    {
                                        tblEntry[port].hourTo_1 = hourScan;
                                        tblEntry[port].minuteTo_1 = minScan;

                                        retVal = E_SUCCESS;
                                        postResult( retVal, true );                                      
                                    }
                                    else
                                    {
                                        retVal = API_RESULT_INVALID_TIMEVALUE;
                                        postResult( retVal, true );
                                    }
                                }
                                break;                                
                            case API_KEYWORD_FROM2:
                                if( server.arg( _apiKeywords[API_KEYWORD_FROM2] ))
                                {
                                    retVal = sscanf(arg, "%2s:%2s",hourScan, minScan);
                                    if( isValidHour(hourScan) && isValidMinute(minScan) )
                                    {
                                        tblEntry[port].hourFrom_2 = hourScan;
                                        tblEntry[port].minuteFrom_2 = minScan;

                                        retVal = E_SUCCESS;
                                        postResult( retVal, true );                                      
                                    }
                                    else
                                    {
                                        retVal = API_RESULT_INVALID_TIMEVALUE;
                                        postResult( retVal, true );
                                    }
                                }
                                break;
                            case API_KEYWORD_TO2:
                                if( server.arg( _apiKeywords[API_KEYWORD_TO2] ))
                                {
                                    retVal = sscanf(arg, "%2s:%2s",hourScan, minScan);
                                    if( isValidHour(hourScan) && isValidMinute(minScan) )
                                    {
                                        tblEntry[port].hourTo_2 = hourScan;
                                        tblEntry[port].minuteTo_2 = minScan;

                                        retVal = E_SUCCESS;
                                        postResult( retVal, true );                                      
                                    }
                                    else
                                    {
                                        retVal = API_RESULT_INVALID_TIMEVALUE;
                                        postResult( retVal, true );
                                    }
                                }
                                break;
                            }
                    }
                    else
                    {
                        switch(keyword)
                        {
                            case API_KEYWORD_FROM1:
                                postTimeFrom1( port );
                                break;
                            case API_KEYWORD_TO1:
                                postTimeTo1( port );
                                break;
                            case API_KEYWORD_FROM2:
                                postTimeFrom2( port );
                                break;
                            case API_KEYWORD_TO2:    
                                postTimeTo2( port );
                                break;                            
                        }      
                    }
                }
                else
                {
                    postResult( API_RESULT_MISSING_PORT, true );
                }
                break;
            case API_KEYWORD_TIME1:
            case API_KEYWORD_TIME2:
                if( server.arg( _apiKeywords[API_KEYWORD_PORT] ).length() )
                {    
                    int port = server.arg( _apiKeywords[API_KEYWORD_PORT] ).toInt() - 1;

                    if(keyword == API_KEYWORD_TIME1)
                    {
                        if(server.arg( _apiKeywords[API_KEYWORD_TIME1] ).equalsIgnoreCase(API_VALUE_ENABLE))
                        {
                            tblEntry[port].enabled_1 = true;
                            retVal = E_SUCCESS;
                            postResult( retVal, true );
                        }
                        else
                        {
                            if(server.arg( _apiKeywords[API_KEYWORD_TIME1] ).equalsIgnoreCase(API_VALUE_DISABLE))
                            {
                                tblEntry[port].enabled_1 = false;
                                retVal = E_SUCCESS;
                                postResult( retVal, true );
                            }
                            else
                            {
                                if(server.arg(_apiKeywords[API_KEYWORD_TIME1]).length())
                                {
                                    postResult( API_RESULT_INVALID_MODE_TIME1, true );
                                }
                                else
                                {
                                    postTime1Mode( port );
                                }
                            }                            
                        }
                    }
                    else
                    {
                        if(keyword == API_KEYWORD_TIME2)
                        {
                            if(server.arg( _apiKeywords[API_KEYWORD_TIME2] ).equalsIgnoreCase(API_VALUE_ENABLE))
                            {
                                tblEntry[port].enabled_2 = true;
                                retVal = E_SUCCESS;
                                postResult( retVal, true );
                            }
                            else
                            {

                                if(server.arg( _apiKeywords[API_KEYWORD_TIME2] ).equalsIgnoreCase(API_VALUE_DISABLE))
                                {
                                    tblEntry[port].enabled_2 = false;
                                    retVal = E_SUCCESS;
                                    postResult( retVal, true );
                                }
                                else
                                {
                                    if(server.arg(_apiKeywords[API_KEYWORD_TIME2]).length())
                                    {
                                        postResult( API_RESULT_INVALID_MODE_TIME2, true );
                                    }
                                    else
                                    {
                                        postTime2Mode( port );
                                    }
                                }

                            }
                        }
                    }
                }
                else
                {
                    postResult( API_RESULT_MISSING_PORT, true );
                }
                break;
            case API_KEYWORD_EXT1:
                if( server.arg( _apiKeywords[API_KEYWORD_PORT] ).length() )
                {
                    int port = server.arg( _apiKeywords[API_KEYWORD_PORT] ).toInt() - 1;
                  
                    if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).equalsIgnoreCase(API_VALUE_ENABLE))
                    {

                        tblEntry[port].extEnable_1 = true;
                        retVal = E_SUCCESS;
                        postResult( retVal, true );
                    }
                    else
                    {
                        if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).equalsIgnoreCase(API_VALUE_DISABLE))
                        {

                            tblEntry[port].extEnable_1 = false;
                            retVal = E_SUCCESS;
                            postResult( retVal, true );
                        }
                        else
                        { 
                            if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).equalsIgnoreCase(API_VALUE_ON))
                            {
// trigger ext1                       tblEntry[port]
                                startExt1Mode();
                                retVal = E_SUCCESS;
                                postResult( retVal, true );
                            }
                            else
                            {
                                if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).equalsIgnoreCase(API_VALUE_OFF))
                                {                              
// trigger ext1                       tblEntry[port]

                                    stopExt1Mode();
                                    retVal = E_SUCCESS;
                                    postResult( retVal, true );
                                }
                                else
                                {                          
                                    if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).length() )
                                    {
                                        postResult( API_RESULT_INVALID_MODE_EXT1, true );
                                    }
                                    else
                                    {
                                        postExt1( port );
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).equalsIgnoreCase(API_VALUE_ON))
                    {
                        startExt1Mode();
                        retVal = E_SUCCESS;
                        postResult( retVal, true );
                     }
                     else
                     {
                        if(server.arg( _apiKeywords[API_KEYWORD_EXT1] ).equalsIgnoreCase(API_VALUE_OFF))
                        {                              
                            stopExt1Mode();
                            retVal = E_SUCCESS;
                            postResult( retVal, true );
                        }
                        else
                        {
                            postResult( API_RESULT_MISSING_PORT, true );
                        }
                    }                  
                }
                break;   
            case API_KEYWORD_EXT2:
                if( server.arg( _apiKeywords[API_KEYWORD_PORT] ).length() )
                {
                    int port = server.arg( _apiKeywords[API_KEYWORD_PORT] ).toInt() - 1;
                  
                    if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).equalsIgnoreCase(API_VALUE_ENABLE))
                    {

                        tblEntry[port].extEnable_2 = true;
                        retVal = E_SUCCESS;
                        postResult( retVal, true );
                        
                    }
                    else
                    {
                        if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).equalsIgnoreCase(API_VALUE_DISABLE))
                        {

                            tblEntry[port].extEnable_2 = false;
                            retVal = E_SUCCESS;
                            postResult( retVal, true );

                        }
                        else
                        {
                            if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).equalsIgnoreCase(API_VALUE_ON))
                            {
// trigger ext1                       tblEntry[port]

                                startExt2Mode();
                                retVal = E_SUCCESS;
                                postResult( retVal, true );
                            }
                            else
                            {
                                if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).equalsIgnoreCase(API_VALUE_OFF))
                                {                              
// trigger ext1                       tblEntry[port]

                                    stopExt2Mode();
                                    retVal = E_SUCCESS;
                                    postResult( retVal, true );
                                }
                                else
                                {      
                                    if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).length() )
                                    {
                                        postResult( API_RESULT_INVALID_MODE_EXT2, true );
                                    }
                                    else
                                    {
                                        postExt2( port );
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).equalsIgnoreCase(API_VALUE_ON))
                    {
// trigger ext1                       tblEntry[port]

                        startExt2Mode();
                        retVal = E_SUCCESS;
                        postResult( retVal, true );
                     }
                     else
                     {
                         if(server.arg( _apiKeywords[API_KEYWORD_EXT2] ).equalsIgnoreCase(API_VALUE_OFF))
                         {                              
// trigger ext1                       tblEntry[port]

                             stopExt2Mode();
                             retVal = E_SUCCESS;
                             postResult( retVal, true );
                         }
                         else
                         {
                             postResult( API_RESULT_MISSING_PORT, true );
                         }
                     }
                }
                break;   
            default:
                pageContent += "120:ERR: UNKNOWN OPTION";
                pageContent += "<br>";            
                break;
        }
    }
}

//
// ************************************************************************
//
// ---------- void apiPage()
//
// ************************************************************************
//
void apiPage()
{

  int i;
  pageContent = "";

  pageContent  = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">";
  pageContent += "<html>";
  pageContent += "<head>";
  pageContent += "<meta charset=\"utf-8\">";
  pageContent += "<title>Web-API</title>";
  pageContent += "</head>";
  pageContent += "<body bgcolor=\"#D4C9C9\" text=\"#000000\" link=\"#1E90FF\" vlink=\"#0000FF\">";

#ifdef DO_LOG
  if ( !beQuiet )
  {
    Logger.Log(LOGLEVEL_DEBUG, (const char*) "apiPage<br>");
  }
#endif // DO_LOG

  pageContent += String("apiPage<br>");

  if ( server.method() == SERVER_METHOD_POST )
  {
    LEDRed();
#ifdef DO_LOG
    if ( !beQuiet )
    {
      pageContent += String("POST REQUEST - NO MORE OUTPUT!<br>");
    }
#endif // DO_LOG
  }
  else
  {
    LEDBlue();
#ifdef DO_LOG
    if ( !beQuiet )
    {
      pageContent += String("GET REQUEST<br>");
    }
#endif // DO_LOG

#ifdef DO_LOG
    if ( !beQuiet )
    {
      Logger.Log(LOGLEVEL_DEBUG, (const char*) "GET REQUEST<br>");
    }
#endif // DO_LOG


    // loop over NUM_API_KEYWORDS-1 items only because of NULL at the end    
    for( i = 0; i < (NUM_API_KEYWORDS-1); i++ )
    {

#ifdef DO_LOG
      if ( !beQuiet )
      {
        pageContent += "server.argName[" + String(i) + "] = " + server.argName(i);
        pageContent += " -> server.hasArg[" + String(_apiKeywords[i]) + "] = " + server.hasArg( _apiKeywords[i] );
        pageContent += " -> server.arg("    + String(_apiKeywords[i]) + ") = " + server.arg( _apiKeywords[i] );
        pageContent += "<br>";
      }
#endif // DO_LOG

      if( server.hasArg( _apiKeywords[i] ) )
      {
        if( server.arg( _apiKeywords[i] ) )
        {
          processApiCall( i, server.arg(_apiKeywords[i]).c_str()) ;
          // this seems always been called
        }
        else
        {
          processApiCall( i, NULL );
        }
      }
    }

    pageContent += "</body>";
    pageContent += "</html>";

    server.send(200, "text/html", pageContent);
  }
}

/* ************** nothing behind this line ************** */

