/*
 * 
 * ACL_Client_ESP32
 * 
 * This is a stand-along Arduino sketch for an ESP32 based Adafruit
 * Feather. This sketch may work on other ESP32 boards. It uses the
 * second UART for communication with a Seeed Studio Grove RFID reader
 * I was unsuccessful geting SoftSerial to work on an ESP board, so I
 * decided to use an ESP32.
 * 
 * This sketch will wait for an RFID to be sent by the reader and then
 * validate it with the Creator Hub's ACL server API via WiFi
 * 
 * Depending on whether this is a door controller or not, it will
 * power down the RFID reader (via a transistor) to allow a stationary
 * RFID card or fob to be reread repeatedly.
 * 
 * This sketch may cache a copy of the current list of valid RFIDs for 
 * this machine/door (configurable).
 * 
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>


//#define DEBUG


////////////////// Change to correct values for application ////////////////

#define DOOR
//#define ACL       "medium-laser-cutter"
#define ACL       "door"

#define CACHE_ACL

//#define AP_NAME_0 "FCCH_WIFI_5GHz"
#define AP_NAME_0 "carriad"
#define AP_NAME_1 "FCCH_WIFI"
#define AP_NAME_2 "FCCreatorHub-5"
#define AP_NAME_3 "FCCreatorHub-2.4"
//#define AP_PASSWD "makerspace"
#define AP_PASSWD "noregrets"
//#define API_HOST  "10.1.10.145"
#define API_HOST "192.168.1.3"
#define API_PORT  8080

#ifndef DOOR
const int READER_POWER  = 12;
#endif
const int RELAY_POWER   = 14;
const int ACCESS_LED    = 32;
const int CONNECTED_LED = 21;

const unsigned long MAX_READ_TIME = 500;
#ifdef DOOR
const int DOOR_UNLOCK_TIME = 5; // seconds
#else
const int MAX_TRIES = 3;
#endif DOOR

//HardwareSerial Serial1(1); // no longer needed with latest ESP32 Arduino library
WiFiMulti wifiMulti;

void setup()
{
  Serial1.begin(9600, SERIAL_8N1, 16, 17);     // the hw Serial baud rate
  Serial.begin(9600);         // the Serial port of Arduino baud rate.
  Serial.println ("Hello!");
    
  //pinMode(LED_BUILTIN, OUTPUT);
  //digitalWrite(LED_BUILTIN, LOW);
#ifndef DOOR
  pinMode(READER_POWER, OUTPUT);
#endif
  pinMode(RELAY_POWER, OUTPUT);
  pinMode(ACCESS_LED, OUTPUT);
  pinMode(CONNECTED_LED, OUTPUT);


#ifndef DOOR
  // power up RFID reader
  digitalWrite(READER_POWER, HIGH);
#endif
  digitalWrite(ACCESS_LED, LOW);
  digitalWrite(CONNECTED_LED, LOW);
  digitalWrite(RELAY_POWER, LOW);

  wifiMulti.addAP(AP_NAME_0, AP_PASSWD);
  wifiMulti.addAP(AP_NAME_1, AP_PASSWD);
  wifiMulti.addAP(AP_NAME_2, AP_PASSWD);
  wifiMulti.addAP(AP_NAME_3, AP_PASSWD);
  
}

// Diagnostic routine that returns approximate free memory
//
int freeRAM () {
  return (ESP.getFreeHeap()); 
}

struct rfid_s {
  unsigned long value;
  bool valid;
};

// Validate data from RFID reader against CRC data
//
bool check_crc (unsigned long data, unsigned long crc) {
  unsigned long res = 0;
#ifdef DEBUG
  Serial.printf ("CRC check: data=%lx, crc=%lx", data, crc);
#endif  
  for (int i = 0; i < 2; i++) {
    res ^= (crc & 0xff);
    crc = crc >> 8;
  }
  for (int i = 0; i < 4; i++) { 
    res ^= (data & 0xff);
    data = data >> 8;
  }

#ifdef DEBUG
  Serial.printf (", res=%lx\n", res);
#endif
  return (res == 0);
}

// get RFID from reader (via hw serial port)
//
struct rfid_s get_RFID () {
  unsigned long rfid = 0;
  unsigned long crc = 0;
  byte in_byte = 0;
  int count = 0;
  bool reading = false;
  bool found = false;
  
  
#ifdef DEBUG
    Serial.println("Found Data!");
#endif

  unsigned long end_time = millis() + MAX_READ_TIME;
  while (millis() < end_time) {
    if (Serial1.available())  {     
      in_byte = Serial1.read();
#ifdef DEBUG
      Serial.print ("in_byte = '");
      Serial.write (in_byte);
      Serial.print ("' = 0x");
      Serial.println (in_byte, HEX);
#endif
      if (in_byte == 0x2) {
        // Detected the "start" char
        //
        //digitalWrite(LED_BUILTIN, HIGH);
        reading = true;
        count = 0;
        rfid = 0x0;
        crc = 0x0;
#ifdef DEBUG
        Serial.println();
#endif
      } else if (reading && (count < 3)) {
        // First two nibbles in hex chars are not part of the card number, but do take part in CRC
        //
        unsigned char hex_digit = (in_byte < 'A') ? (in_byte - '0') : (in_byte - 'A' + 0xa);
        crc = (crc << 4) | hex_digit;
      
      } else if (reading && (count >= 3) && (count <= 10)) {
        // Next 8 nibbles are the card number in hex chars
        //
        unsigned char hex_digit = (in_byte < 'A') ? (in_byte - '0') : (in_byte - 'A' + 0xa);
        rfid = (rfid << 4) | hex_digit;

      } else if (reading && (count > 10) && (count < 13)) {
        // Last 2 nibbles are the CRC in hex chars
        //
        unsigned char hex_digit = (in_byte < 'A') ? (in_byte - '0') : (in_byte - 'A' + 0xa);
        crc = (crc << 4) | hex_digit;
        
      } else if (reading && (count == 13)) {
        // Last char should be the "stop" char, but we don't actually care what it
        // is, it merely has to be the 13th char after the start char
        //
        reading = false;
        found = true;
        //digitalWrite(LED_BUILTIN, LOW);
        break;

#ifdef DEBUG
      } else if (reading) {
        Serial.println();
#endif
      }
      
      if (reading)
        count++;
    }
  }

  // if there is anything after the 13th characther, eat it
  //
  while (Serial1.available()) {
    Serial1.read();
  }

  struct rfid_s retval;
  retval.value = rfid;
  retval.valid = found && check_crc(rfid, crc);
  return (retval);
}

bool link_up = false;     // are we connected to WiFi?
bool OK_to_query = false; // can we query RFID?

#ifdef CACHE_ACL
bool acl_loaded = false;
char *acl_cache = NULL;
#endif

struct rfid_s last_rfid = {0, false};
bool acl_ok = false;
int attempts = 0;

void blinkit_high(int pin, int count, int wait){
  for (int i=count; i>0; i--) {
    digitalWrite(pin, HIGH);
    delay(wait);
    digitalWrite(pin, LOW);
    if (i > 1)
      delay(wait);
  }
}

void blinkit_low(int pin, int count, int wait){
  for (int i=count; i>0; i--) {
    digitalWrite(pin, LOW);
    delay(wait);
    digitalWrite(pin, HIGH);
    if (i > 1)
      delay(wait);
  }
}


void good_rfid_sequence() {
  // turn relay on
  digitalWrite (RELAY_POWER, HIGH);
  digitalWrite (ACCESS_LED, HIGH);
}

void bad_rfid_sequence () {
   // turn relay off and flash LED
  digitalWrite (RELAY_POWER, LOW);
  blinkit_high (ACCESS_LED, 5, 250);
}

void no_rfid_sequence () {
  // turn relay off
  digitalWrite (RELAY_POWER, LOW);
  digitalWrite (ACCESS_LED, LOW);
}

#ifdef CACHE_ACL

// Load ACL from server into a cache
//
bool read_acl () {
  char query[128];
  sprintf(query, "/api/get-acl-0/%s", ACL);

  HTTPClient http;
  http.begin(API_HOST, API_PORT, query);
  http.setTimeout(5000); // set timeout to 5 seconds

  int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      int payload_size = http.getSize();
      if (payload_size > 0) {
        if (acl_loaded && (acl_cache != NULL)) {
#ifdef DEBUG
          Serial.print("Before free(): freeRam = ");
          Serial.println(freeRAM());
#endif DEBUG
          free (acl_cache);
        }
#ifdef DEBUG
        Serial.print("Before malloc(): freeRam =");
        Serial.println(freeRAM());
#endif DEBUG
        acl_cache = (char *) malloc (payload_size+2);
#ifdef DEBUG
        Serial.print("After malloc(): freeRam =");
        Serial.println(freeRAM());
#endif DEBUG
        strcpy (acl_cache, http.getString().c_str());
        acl_cache[payload_size] = '\n';
        acl_cache[payload_size+1] = '\0';
        acl_loaded = true;
//#ifdef DEBUG
//        Serial.print("acl_cache='");
//        Serial.print(acl_cache);
//        Serial.println("'");
//#endif DEBUG
        return (true);
      } else {
        Serial.print("http GET not OK, size = ");
        Serial.println(payload_size);
        blinkit_low(CONNECTED_LED, 5, 250);
        return (false);
      }
      
    } else {
      Serial.print("http GET not OK, httpCode = ");
      Serial.println(httpCode);
      blinkit_low(CONNECTED_LED, 6, 250);
      return (false);
    }
  } else {
    Serial.printf ("http GET failed, httpCode = %s\n", http.errorToString(httpCode).c_str());
    blinkit_low(CONNECTED_LED, 7, 250);
    return (false);
  }
  return (false);
}

// Check RFID against cached ACL
//
bool query_rfid_cache (unsigned long rfid) {
  char rfid_str[16];
  sprintf (rfid_str, "\n%ld\n", rfid);
#ifdef DEBUG
  Serial.print ("rfid string = ");
  Serial.println (rfid_str);
#endif
  if (strstr(acl_cache, rfid_str) != NULL) {
    Serial.println("CACHE: ACL OK");
    return (true);
  } else {
    Serial.println("CACHE: ACL Invalid");
    return (false);
  }
  return (false);
}

#endif CACHE_ACL

// Validate the RFID with the server
//
// Return 1 if good
//        0 if bad
//       -1 if error on request
int query_rfid (unsigned long rfid) {

#ifdef CACHE_ACL
  if (link_up) {
    // always load ACL cache when we can since we don't know when the list will change
    // 
    read_acl();
  }

#ifdef DEBUG
  if (link_up && acl_loaded) {
    Serial.print ("Link is up, but checking cache anyway: ");
    query_rfid_cache (rfid);
  }
#endif
  
  if (!link_up) {
    if (acl_loaded) {
      return (query_rfid_cache (rfid));
    } else {
      // we shouldn't be here if link is down and acl hasn't been loaded
      return (-1);
    }
  }
#endif

  // Even if we cache acl, we always query the server (if link is up)
  // This guarantees that the access will be logged.
  
  HTTPClient http;
  
  char query[128];
  sprintf(query, "/api/check-access-0/%s/%lu", ACL, rfid);

  http.begin(API_HOST, API_PORT, query);
  http.setTimeout(20000); // set timeout to 20 seconds

  int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      if (http.getString() == "True") {
        Serial.println("ACL OK");
        return (1);
      } else {
        Serial.println("ACL Invalid");
        return (0);
      }
      
    } else {
      Serial.print("http GET not OK, httpCode = ");
      Serial.println(httpCode);
      blinkit_low(CONNECTED_LED, 2, 250);
      return (-1);
    }
  } else {
    Serial.printf ("http GET failed, httpCode = %s\n", http.errorToString(httpCode).c_str());
    blinkit_low(CONNECTED_LED, 3, 250);
    return (-1);
  }
  return (-1);
}


void loop()
{

  if (freeRAM() < 100000) {
    Serial.println("WARNING! WARNING! freeRAM has fallen below 100000. Resetting!");
    ESP.restart();
  }
  
  if((wifiMulti.run() == WL_CONNECTED)) {
    if (!link_up) {
      link_up = true;

      digitalWrite (CONNECTED_LED, HIGH);
      Serial.println("Connected");

      OK_to_query = true;
#ifdef CACHE_ACL
      read_acl();
#endif
      
      delay (500);
    }
  } else {
    if (link_up) {
      link_up = false;
      digitalWrite (CONNECTED_LED, LOW);
      Serial.println("Disonnected!");

#ifndef CACHE_ACL
      OK_to_query = false;
#endif
      
      delay (500);
    }
  }

#ifdef DOOR

  // see if data is coming from RFID reader
  if (Serial1.available())  {
    struct rfid_s cardno = get_RFID();
    Serial.printf ("cardno = %lu (%d)\n", cardno.value, cardno.valid);

    if (OK_to_query & cardno.valid) {
      int result = query_rfid (cardno.value);

      if (result == 1) {
        good_rfid_sequence();
        delay (DOOR_UNLOCK_TIME * 1000);
        no_rfid_sequence();

        // eat any card reads that happened when door was unlocked
        //
        while (Serial1.available()) {
          Serial1.read();
        }
        
      
      } else if (result == 0) {
        bad_rfid_sequence();
      } else {
        no_rfid_sequence();
      }
    } else {
      // link down or corrupted card
      no_rfid_sequence();
    }
  }

#else
  // see if data is coming from RFID reader
  if (Serial1.available())  {
    struct rfid_s cardno = get_RFID();
    Serial.printf ("cardno = %lu (%d)\n", cardno.value, cardno.valid);
    
    if (last_rfid.valid && cardno.valid && (last_rfid.value == cardno.value)) {
      // don't bother looking anything up
      // pass
      
    } else if (last_rfid.valid && (attempts > 0)) {
      // don't turn off output yet, reset reader and try again
      if (cardno.valid) {
        Serial.printf ("Different card sensed, %d tries left\n", attempts);
      } else {
        Serial.printf ("Invalid card sensed, %d tries left\n", attempts);
      }
      attempts--;
      blinkit_low (ACCESS_LED, 10, 125);
      
    } else if (OK_to_query & cardno.valid) {

      int result = query_rfid (cardno.value);

      if (result == 1) {
        good_rfid_sequence();
        last_rfid = cardno;
        acl_ok = true;

        attempts = MAX_TRIES;

      } else if (result == 0) {
        bad_rfid_sequence();
        last_rfid = cardno;
        attempts = 0;
        acl_ok = false;
      } else {
        no_rfid_sequence();
        last_rfid.valid = false;
        acl_ok = false;
        attempts = 0;
      }
    } else {
      // link down or corrupted card
      no_rfid_sequence();
      last_rfid.valid = false;
      acl_ok = false;
    }
  } else {
    // no card sensed by reader

    if (last_rfid.valid && (attempts > 0)) {
      // don't turn off output yet, reset reader and try again
        Serial.printf ("No card sensed, %d tries left\n", attempts);
        attempts--;
        blinkit_low (ACCESS_LED, 5, 125);
    } else {
      no_rfid_sequence();

      // log an invalid card number so log will show how long the machine was in use
      if (acl_ok && last_rfid.valid && link_up) {
        query_rfid(0);
      }
      last_rfid.valid = false;
      acl_ok = false;
    }
    
  }

  // since the RFID reader cannot guarantee that a card
  // held in front of it will trigger constantly while the card
  // is there, we cut power when we sense the card and restore
  // it, thereby causing it to sense the card again
  if (last_rfid.valid) {
    // cut power on RFID reader, then restore
    digitalWrite(READER_POWER, LOW);
    delay(2500);
    digitalWrite(READER_POWER, HIGH);
    delay(2500);
  }

#endif DOOR
  
}
