### ESP8266_WIFI_TimerRelais:
The module connects to the specified WLAN as a WiFi-client. After that, it requests the current date an time via network time protocol from a specific NTP-server.
Last step in starting up is to create a http-server listening to a specified port for requests.
The web server handles a configuration page for up to eight channels. Each channel may be switched on and off on up to two user defined times.
In addition for each channel there are two flags to allow switching on or off using the http-API.
There are three modes for each channel. ON and OFF override the user defined times if their check-boxes are active.  AUTO activates the user-defined times for each channel and time that is activated ( checked ).
These setting information are stored in the EEPROM and restored on every reboot of the module.
NOTE: don't forget to #define DEFAULT_WLAN_SSID and #define DEFAULT_WLAN_PASSPHRASE to values matching your local WLAN (lines about 109 ff.)!
Around line 203 you have to change DEFAULT_UPDATE_URL.
CAUTION! After flashing the sketch using the Arduino IDE you MUST hard reset the ESP-Module because of update and restart don't work if you don't reset the ESP.
Now you may copy an .bin-file to the location where DEFAULT_UPDATE_URL points to. It's name has to be "firmware.bin0" ( FIRMWARE_DOT_BIN_NULL ).
Update starts when a marker file "firnware.0" ( FIRMWARE_DOT_NULL ) is present in the above directory.

**NOTE: BECAUSE OF SOME SMALL FIXES YOU HAVE TO UPDATE MY EEPROM-LIBRARY, too.**



