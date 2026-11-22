#include <Arduino.h>
#include <TinyGPS++.h> // SoftwareSerial.h completely removed for maximum processor efficiency

// Shift Register Interface Control Pins (Moved to D2, D3, D4 to avoid jumper conflicts)
const int latchPin = 2;  // Net "RCLK"  -> Arduino D2 (Physical Header Pin 20)
const int clockPin = 3;  // Net "SRCLK" -> Arduino D3 (Physical Header Pin 21)
const int dataPin  = 4;  // Net "SR-1"  -> Arduino D4 (Physical Header Pin 22)

// Configuration Jumper Input Pins (GND active - using Arduino D-numbers, NOT header numbers)
const int pinEST  = 7;   // Arduino D7  (Physical Header Pin 25) -> Jumper to GND for Eastern Time
const int pinCST  = 8;   // Arduino D8  (Physical Header Pin 26) -> Jumper to GND for Central Time
const int pinMST  = 9;   // Arduino D9  (Physical Header Pin 27) -> Jumper to GND for Mountain Time
const int pinPST  = 10;  // Arduino D10 (Physical Header Pin 28) -> Jumper to GND for Pacific Time
const int pinDST  = 11;  // Arduino D11 (Physical Header Pin 29) -> Jumper to GND to enable Daylight Saving (+1 Hr)

// Hardware Serial Configuration
// Connect the GT-U7 TX Pin directly to the Arduino Nano R4 Physical Header Pin 17 (RX0)
#define gpsSerial Serial1

TinyGPSPlus gps;

// Startup Diagnostic Variables
unsigned long lastBlinkTime = 0;
bool startupBlinkState = false;

// Function to calculate active UTC offset dynamically based on hardware configuration jumpers
int getLocalTimeOffset() {
  int offset = 0; // Default to UTC if no zone jumpers are installed

  // Read zone selection jumpers (Active LOW due to INPUT_PULLUP)
  if (digitalRead(pinEST) == LOW) {
    offset = -5; // Eastern Standard Time
  } else if (digitalRead(pinCST) == LOW) {
    offset = -6; // Central Standard Time
  } else if (digitalRead(pinMST) == LOW) {
    offset = -7; // Mountain Standard Time
  } else if (digitalRead(pinPST) == LOW) {
    offset = -8; // Pacific Standard Time
  }

  // Read Daylight Saving Time correction switch override
  if (digitalRead(pinDST) == LOW) {
    offset += 1; // Advance clock by 1 hour if DST jumper is connected
  }

  return offset;
}

void setup() {
  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  
  // Initialize config pins using internal pullups so they float safe at HIGH until jumped to GND
  pinMode(pinEST, INPUT_PULLUP);
  pinMode(pinCST, INPUT_PULLUP);
  pinMode(pinMST, INPUT_PULLUP);
  pinMode(pinPST, INPUT_PULLUP);
  pinMode(pinDST, INPUT_PULLUP);
  
  // Initialize dedicated internal hardware serial port 1 at 9600 baud for the GT-U7 module
  gpsSerial.begin(9600);
}

void loop() {
  // Continuously feed serial bytes from the background hardware UART buffers into the parser
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Check if the GPS module has achieved a valid satellite time fix
  if (gps.time.isValid()) {
    
    // Read the hardware selection lines to fetch active offset bounds
    int timeZoneOffset = getLocalTimeOffset();
    
    // Convert incoming UTC time to local hours
    int localHour = gps.time.hour() + timeZoneOffset;
    
    // Handle midnight and day boundary rollover bounds
    if (localHour < 0) {
      localHour += 24;
    } else if (localHour >= 24) {
      localHour -= 24;
    }

    // Extract precise BCD decimal digits
    byte hTens  = localHour / 10;
    byte hUnits = localHour % 10;
    
    byte mTens  = gps.time.minute() / 10;
    byte mUnits = gps.time.minute() % 10;
    
    byte sTens  = gps.time.second() / 10;
    byte sUnits = gps.time.second() % 10;

    // Pack bits cleanly to isolate QA-QH pins on each chip
    // SR1 (Hours): QA=H10, QB=H20, QC-QF=H1,H2,H4,H8
    byte reg1_hours = (hTens & 0x03) | ((hUnits & 0x0F) << 2);

    // SR2 (Minutes): QA-QD=M1,M2,M4,M8, QE-QG=M10,M20,M40
    byte reg2_minutes = (mUnits & 0x0F) | ((mTens & 0x07) << 4);

    // SR3 (Seconds): QA-QD=S1,S2,S4,S8, QE-QG=S10,S20,S40
    byte reg3_seconds = (sUnits & 0x0F) | ((sTens & 0x07) << 4);

    // Write array registers out to physical chips (Last register sent first)
    digitalWrite(latchPin, LOW);
    shiftOut(dataPin, clockPin, MSBFIRST, reg3_seconds); // Floods downstream to SR3
    shiftOut(dataPin, clockPin, MSBFIRST, reg2_minutes); // Lands on intermediate SR2
    shiftOut(dataPin, clockPin, MSBFIRST, reg1_hours);   // Stays anchored on SR1
    digitalWrite(latchPin, HIGH);

  } else {
    // STARTUP SEARCH MODE: Blink an LED to show the clock is active but searching for satellites
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastBlinkTime >= 500) { // Toggle every 500ms
      lastBlinkTime = currentMillis;
      startupBlinkState = !startupBlinkState;
      
      digitalWrite(latchPin, LOW);
      if (startupBlinkState) {
        // Light up a single LED (Seconds 1s unit LSB - Pin S1) to show search heartbeats
        shiftOut(dataPin, clockPin, MSBFIRST, 0x01); // Seconds Register
        shiftOut(dataPin, clockPin, MSBFIRST, 0x00); // Minutes Register
        shiftOut(dataPin, clockPin, MSBFIRST, 0x00); // Hours Register
      } else {
        // Clear all display registers during the off cycle
        shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
        shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
        shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
      }
      digitalWrite(latchPin, HIGH);
    }
  }
  
  // Kept minimal to prevent blocking background NMEA serial stream collection
  delay(5);
}