# ACL_Client_ESP32

This is a stand-along Arduino sketch for an ESP32 based Adafruit Feather. This sketch may work on other ESP32 boards. It uses the second UART for communication with a Seeed Studio Grove RFID reader.

I was unsuccessful geting SoftSerial to work on an ESP board, so I decided to use an ESP32.

This sketch will wait for an RFID to be sent by the reader and then validate it with the Creator Hub's ACL server API via WiFi.  
In the future, I may expand this sketch to cache a copy of the current list of valid RFIDs for this machine.
