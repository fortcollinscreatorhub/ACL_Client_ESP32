# ACL_Client_ESP32

This is a stand-along Arduino sketch for an ESP32 based Adafruit Feather. The purpose is to implement an RFID reading controller to limit access to Fort Collins Creator Hub equipment such laser cutters and machine tools. It uses the second microcontroller UART for communication with a Seeed Studio Grove RFID reader. This sketch will wait for an RFID to be sent by the reader and then validate it with the Creator Hub's ACL server API via WiFi. It assumes four outputs in addition to the Serial and Serial1 ports:
* "Connection" LED - indicates a valid WiFi connection when lit. It flashes when an HTTP error is detected.
* "Access" LED - indicates thati an RFID with the correct acccess rights for the equipment has been detected. It flashes when an RFID without correct permission has been detected.
* Relay output - connected to a transistor that controls a relay connected to the equipment - typically a low voltage signal.
* Reader output - connected to a transistor that controls the power to the RFID reader. This is needed for equipment that needs a "level triggered" control (as opposed to a door controller where "edge triggered" suffices). Most readers will not reliably keep sending data when an RFID is left in front of the reader. To get around this, when a valid RFID is detected (not necessarily with the correct permissions), the reader is powered down and then brought back up to force it to re-detect the card.

This sketch may work on other ESP32 boards. I was unsuccessful geting SoftSerial to work on an ESP8266 board, so I decided to use an ESP32 with it's additional UARTs.

In the future, I may expand this sketch to cache a copy of the current list of valid RFIDs for this machine when. This cached copy would be used if the WiFi link goes down or if there was an error on the HTTP get.
