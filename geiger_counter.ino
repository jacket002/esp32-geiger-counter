/*
SBM-20 tube Geiger counter firmware & protocols
                   ***    ☢☢☢    ***
AD8561 comparator output -> arduino interrupt service pin
GPIO pin -> mosfet -> buzzer 
SSD1306 0.96" OLED shows total CPS / CPM

 - make sure to validate that selected GPIO pin assignments are seamless and don't overlap  ESP32 functions.
 12/30/2025
                                                            */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


//Global variable declarations

static const int PULSE_PIN = 2;    // AD8561 comparator digital output -> ESP32 interrupt pin (A2)
static const int BUZZER_PIN = 4;   // ESP32 output (D4) -> MOSFET gate -> buzzer 

static const int PULSE_EDGE = RISING;    // comparator output HIGH on rising edge (+ OUT)
static const uint32_t TICK_MS = 1;


// OLED 
static const int OLED_ADDR = 0x3C;   // or 0x3D depending
static const int SCREEN_W = 128; 
static const int SCREEN_H = 64;
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// isr counts must be volatile to avoid compiler errors
volatile uint32_t isrTotalCount = 0;     // uin32_t must be used to store large values to avoid overflow.
volatile uint32_t isrCurrentCount = 0; 
volatile uint32_t prevUs = 0; 

// Timing 
uint32_t prevSecondMs = 0;
uint32_t prevOledMs = 0; 
// -----------------------------------

uint16_t cpsHistory[60] = {0};
uint16_t cpsIndex = 0;

bool tickActive = false; 
uint32_t tickStart = 0; 

// ISR protocol with firmware-defined deadtime (SBM-20 has deadtime length around 200us)
void IRAM_ATTR onPulse() { 
  uint32_t currentUs = micros();
  uint32_t deadTime = currentUs - prevUs;
  if (deadTime < 150) {
    return;   // ignore pulses less than the deadtime
  }

  prevUs = currentUs;   // update timestamp
  isrTotalCount++;
  isrCurrentCount++;
  
}

void setup() {
  Serial.begin(115200);
  pinMode(PULSE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), onPulse, PULSE_EDGE);
  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Geiger starting...");
  display.display();

  prevSecondMs = millis(); 
  prevOledMs = prevSecondMs;
}

void loop() {
  uint32_t currentMs = millis();
  
  if (tickActive && (currentMs - tickStart) >= TICK_MS) {
    digitalWrite(BUZZER_PIN, LOW);
    tickActive = false; 
  }

  // Snapshots counts once per second
  if (currentMs - prevSecondMs >= 1000) {
    prevSecondMs += 1000; // after every current second in milliseconds (1000), the previous second is stored.

    uint32_t cps;
    noInterrupts();   // any pulse that arrives during this short time frame is ignored so that cps is read accurately. 
    cps =  isrCurrentCount;  // atomically records cps as current isr count.
    isrCurrentCount = 0;
    interrupts();

    cpsHistory[cpsIndex] = cps;
    cpsIndex += 1; 
    if (cpsIndex >= 60) {
      cpsIndex = 0;
    }
  }

   // Read total counts
  uint32_t total; 
  noInterrupts();
  total = isrTotalCount;
  interrupts(); 
  

  static uint32_t prevTotal = 0;
  bool newPulse = (total != prevTotal); // evaluates if a new pulse occured since the last loop
  prevTotal = total;                  // always sync to avoid backlog

  if (!tickActive && newPulse) {     // 'if buzzer is inactive and a new pulse occured'
    digitalWrite(BUZZER_PIN, HIGH);         
    tickActive = true;  
    tickStart = currentMs;                // set tickStart to current ms at new pulse
  }

  // Compute CPM from history
  uint32_t cpm = 0;
  for (int i = 0; i < 60; i++) { 
    cpm += cpsHistory[i]; 
  }
  // previous second CPS is the entry we just wrote most recently
  uint16_t prevCps = cpsHistory[(cpsIndex + 59) % 60];

  // Display, refreshed 5 times every second (200ms)
  if (currentMs - prevOledMs >= 200) {
    prevOledMs = currentMs;

    if (display.width() > 0) {
      display.clearDisplay();
      display.setCursor(0,0);
      display.print("Counts: "); display.println(total);
      display.print("CPS: ");          display.println(prevCps);
      display.print("CPM: ");            display.println(cpm);
      display.display();
    
    }
  }


}
