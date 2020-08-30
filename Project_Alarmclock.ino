#include <SPI.h>
#include <TimerOne.h>
#include <Wire.h>
#include "RTClib.h"
RTC_DS1307 RTC;

#define NO_RTC 0 // for developing, when no RTC board is present (1=development, 0=production) -> will use millis as semi-RTC

/*
  Otto, March - September 2017, August 2020

  Shift registers & display
    setup using two shift registers to drive 4-segment LED
    using
    - 2 x 74HC595
    - 1 x YS common anode 4-digit 7-segment display
    - 8 x 440 Ohm resistors (7 segements + colon)
    - 4 x BC557 PNP
    - 4 x 1 kOhm base resistor for BC557s

    Shift register outputs:
    #1
    A - digital point (on each digit)
    B - colon (with digit3)
    C - L3 (with digit4)
    D - not used
    E - digit1
    F - digit2
    G - digit3 (plus colon)
    H - digit4 (plus L3)
    #2
    A-G - segments A-G
    H - colon

    both registers -> low = active (digits because of BC557s, rest because of common anode display)
    -> define constants as positive and use NOT operator in shifting

    parts of code from shiftOutFast, Joseph Francis
    see also: http://arduino.stackexchange.com/questions/16348/how-do-you-use-spi-on-an-arduino

  RTC
    RTC connected to I2C pins A4 and A5
    see http://www.elecrow.com/wiki/index.php?title=Tiny_RTC

  Rotary Encoder
    encoder connected to (digital) pins 2, 3 and 4
    code from http://playground.arduino.cc/Main/RotaryEncoders, the rafbuff part (adapted to get rid of delay in interupt routine)

  Sound
   Arduino tone library documentation (http://arduino.cc/en/Tutorial/Tone)
   songs from https://github.com/robsoncouto/arduino-songs!
*/

#include "melody.h"

// Pin assignements for shift registers
#define LATCH 10 // pin 12 on HC595
#define CLK 13   // pin 11 on HC595
#define DATA 11  // pin 14 on HC595

// Pin assignements for RotaryEncoder & speaker. Do not change 2 & 3, these are used for the interupt routines!
enum PinAssignments {
  encoderPinA = 2,   // right (labeled CLK on Keyes decoder)
  encoderPinB = 3,   // left (labeled DT on Keyes decoder)
  pushButton = 4,    // switch (labeled SW on Keyes decoder)
  speakerPin = 8        // speaker (via 100 Ohm or more resistor)
};

//--- Important: All shift register pins must be 8 or higher (in PORTB range)
const byte latchPinPORTB = LATCH - 8;
const byte clockPinPORTB = CLK - 8;
const byte dataPinPORTB = DATA - 8;

// This is the hex value of each number stored in an array by index num
// were using ascii equivalent (48=0 ... 57=0, 65=A .. 90=Z. signs in between all blanks
// use one of these (eg colon) for space
const byte symbol[43] = {
  0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xF6, // 0-9
  0x00, 0x00, 0x02, 0x12, 0x02, 0x00, 0x00,                   // ascii 58-64, all blanks here (apart from =, > and < )
  0xEE, 0x3E, 0x9C, 0x7A, 0x9E, 0x8E, 0xF6, 0x2E,             // A-H
  0x60, 0x70, 0x02, 0x1C, 0x02, 0xEC, 0xFC, 0xCE,             // I-P
  0xE6, 0x8C, 0xB6, 0x1E, 0x38, 0x7C, 0x02, 0x6E,             // Q-X
  0x66, 0xDA
};                                                // Y and Z

const byte digitCode[4] = {0x08, 0x04, 0x02, 0x01};
const byte dPointCode = 0x80; // add to output for register #1 to switch on digital point
const byte colonCode = 0x40; // same for colon
const byte led3Code = 0x20; // same for LED 3
const int timerDelay = 200; // speed of timer in micros -> display routine will be called every X micros

// menu variables
const String menuText[6] = {"CNDY", "PAPA", "TIJD", "ALAR", "AL>?"}; // 4 characters!
const int menuItemCount = 5;
enum displayStatus {time, menu, setTime, setAlarm, playingMelody};
const int menuDelay = 9000; // delay in millis to keep menu visible, before falling back to time

// for display routine
short int intensity = 1; // light intensity, 0-10, higher = brighter
volatile byte drawDigit = 0; // 0=digit1, .. 3=digit4
volatile short int stepCounter = 0;
bool colonOn = false;
bool led3On = false;
displayStatus currentDisplayStatus = time;
int activeMenuOption = 0;
unsigned long menuStartMillis;
unsigned long blinkMillis = 0;
byte digitValue[4] = {0x0, 0x0, 0x0, 0x0}; // start with empty digits

// for sound / melody
unsigned long noteMillis;
unsigned long soundMillis;
const long maxPlayDuration = 60000; // repeat song for max 1 minute
int *melody;
int currentNote = 0;
int divider;
int noteDuration;
int tempo;
int wholenote;

// timekeeping variables
DateTime now;
#if NO_RTC == 1
unsigned long startMillis;
#endif
int toDisplay = 0;
int alarmHour = 7;
int alarmMinute = 0;
bool alarmActive = false;

// for encoder routines
volatile int encoderPos = 0;   // a counter for the dial
volatile boolean rotating = false;      // debounce management

// interrupt service routine vars
volatile boolean A_set = false;
volatile boolean B_set = false;

void setup() {

  // setup for shift registers (LED display)
  pinMode(LATCH, OUTPUT);  // RCLK,  pin 12 on HC595
  pinMode(CLK, OUTPUT);    // SRCLK, pin 11 on HC595
  pinMode(DATA, OUTPUT);   // SER,   pin 14 on HC595

  digitalWrite(LATCH, LOW);
  digitalWrite(DATA, LOW);
  digitalWrite(CLK, LOW);

  pinMode(encoderPinA, INPUT_PULLUP); // new method of enabling pullups
  pinMode(encoderPinB, INPUT_PULLUP);
  pinMode(pushButton, INPUT_PULLUP);

  setupSPI();

  Timer1.initialize(timerDelay);
  Timer1.attachInterrupt(iProcess);

  // RTC code
#if NO_RTC == 0
  Wire.begin();
  RTC.begin();

  // following line sets the RTC to the date & time this sketch was compiled
  // use this to set the clock, don't forget to comment & re-upload again
  // RTC.adjust(DateTime(__DATE__, __TIME__));
#else
  now = DateTime(__DATE__, __TIME__);
  startMillis = millis();
#endif

  // rotary encoder setup
  attachInterrupt(0, doEncoderA, CHANGE);  // encoder pin on interrupt 0 (pin 2)
  attachInterrupt(1, doEncoderB, CHANGE);  // encoder pin on interrupt 1 (pin 3)
}

void loop() {

#if NO_RTC == 0
  now = RTC.now();
#else
  if ((millis() - startMillis) > 999) {
    now = DateTime(now.unixtime() + 1);
    startMillis = millis();
  }
#endif

  // show time (or alarm time)
  if (currentDisplayStatus == time || currentDisplayStatus == setTime || currentDisplayStatus == playingMelody) {
    toDisplay = (now.hour() * 100 + now.minute());
  }
  if (currentDisplayStatus == setAlarm) {
    toDisplay = (alarmHour * 100 + alarmMinute);
  }

  // show value & handle rotation
  // set values for digitValue[] here
  // actual display itself will be taken care of by timer
  switch (currentDisplayStatus) {
    case displayStatus::time:
      led3On = alarmActive;
      for (int digit = 3 ; digit >= 0 ; digit--)
      {
        if (toDisplay > 0)
        {
          digitValue[digit] = symbol[toDisplay % 10];
          toDisplay /= 10;
        }
        else
        {
          digitValue[digit] = (digit == 3) ? symbol[0] : 0x0;
        }
      }
      // blink colon every other second
      colonOn  = (now.second() % 2 == 0) ? 0 : 1;

      // handle encoder rotation to set intensity
      if (encoderPos > 0)
      {
        intensity ++;
        if (intensity > 10 ) {
          intensity = 10;
        }
        encoderPos = 0;
      }
      if (encoderPos < 0)
      {
        intensity --;
        if (intensity < 0 ) {
          intensity = 0;
        }
        encoderPos = 0;
      }

      // check if alarm must go off
      if (alarmActive && now.hour() == alarmHour && now.minute() == alarmMinute && now.second() == 0)
      {
          /* Go play alarm! */
          currentDisplayStatus = playingMelody;
    
          // select melody
          melody = melody3;
          tempo = tempo3;
    
          // setup melody parameters, rest is taken care of in the loop
          wholenote = (60000 * 4) / tempo;
          currentNote = 0;
          noteDuration = 0;
          soundMillis = millis();
      }
      break;
    case displayStatus::setTime:
      led3On = alarmActive;
      // show time with blink (800ms on, 200ms off)
      if (millis() - blinkMillis > 800) {
        for (byte i = 0; i < 4; i++)
        {
          digitValue[i] = 0x0;
        }
        colonOn = false;
        if (millis() - blinkMillis > 1000) {
          blinkMillis = millis();
          colonOn = true;
        }
      }
      else {
        for (int digit = 3 ; digit >= 0 ; digit--)
        {
          if (toDisplay > 0)
          {
            digitValue[digit] = symbol[toDisplay % 10];
            toDisplay /= 10;
          }
          else
          {
            digitValue[digit] = (digit == 3) ? symbol[0] : 0x0;
          }
        }
      }

      // handle encoder rotation to change time by 1 minute
      if (encoderPos > 0)
      {
#if NO_RTC == 0
        RTC.adjust(DateTime(now.unixtime() + 60));
#else
        now = DateTime(now.unixtime() + 60);
#endif
        encoderPos = 0;
        menuStartMillis = millis();
      }
      if (encoderPos < 0)
      {
#if NO_RTC == 0
        RTC.adjust(DateTime(now.unixtime() - 60));
#else
        now = DateTime(now.unixtime() - 60);
#endif
        encoderPos = 0;
        menuStartMillis = millis();
      }
      if (millis() - menuStartMillis > menuDelay) {
        currentDisplayStatus = time;
      }
      break;
    case displayStatus::menu:
      for (int digit = 0 ; digit < 4 ; digit++)
      {
        digitValue[digit] = getSymbol(menuText[activeMenuOption][digit]);
      }
      if(activeMenuOption == 4)
      {
        // toggle alarm -> show current status as 4th digit
        digitValue[3] = alarmActive ? symbol[1] : symbol [0];
      }
      // handle encoder rotation to cycle through menus
      if (encoderPos > 0)
      {
        activeMenuOption ++;
        if (activeMenuOption > (menuItemCount - 1) ) {
          activeMenuOption = 0;
        }
        encoderPos = 0;
        menuStartMillis = millis();
      }
      if (encoderPos < 0)
      {
        activeMenuOption --;
        if (activeMenuOption < 0 ) {
          activeMenuOption = menuItemCount - 1;
        }
        encoderPos = 0;
        menuStartMillis = millis();
      }
      if (millis() - menuStartMillis > menuDelay) {
        currentDisplayStatus = time;
      }
      break;
    case displayStatus::setAlarm:
      led3On = alarmActive;
      // show time with blink (800ms on, 200ms off)
      if (millis() - blinkMillis > 800) {
        for (byte i = 0; i < 4; i++)
        {
          digitValue[i] = 0x0;
        }
        colonOn = false;
        if (millis() - blinkMillis > 1000) {
          blinkMillis = millis();
          colonOn = true;
        }
      }
      else {
        for (int digit = 3 ; digit >= 0 ; digit--)
        {
          if (toDisplay > 0)
          {
            digitValue[digit] = symbol[toDisplay % 10];
            toDisplay /= 10;
          }
          else
          {
            digitValue[digit] = (digit == 3) ? symbol[0] : 0x0;
          }
        }
      }

      // handle encoder rotation to change alarmtime by 1 minute
      if (encoderPos > 0)
      {
        alarmMinute++;
        if(alarmMinute>=60)
        {
          alarmMinute = 0;
          alarmHour++;
          if(alarmHour>=24)
          {
            alarmHour = 0;
          }
        }
        encoderPos = 0;
        menuStartMillis = millis();
      }
      if (encoderPos < 0)
      {
        alarmMinute--;
        if(alarmMinute<0)
        {
          alarmMinute = 59;
          alarmHour--;
          if(alarmHour<0)
          {
            alarmHour = 23;
          }
        }
        encoderPos = 0;
        menuStartMillis = millis();
      }
      if (millis() - menuStartMillis > menuDelay) {
        currentDisplayStatus = time;
      }
      break;
    case displayStatus::playingMelody:
      // show time
      for (int digit = 3 ; digit >= 0 ; digit--)
      {
        if (toDisplay > 0)
        {
          digitValue[digit] = symbol[toDisplay % 10];
          toDisplay /= 10;
        }
        else
        {
          digitValue[digit] = (digit == 3) ? symbol[0] : 0x0;
        }
      }
      // blink led3 & colon every other second
      led3On  = (now.second() % 2 == 0) ? 0 : 1;
      colonOn  = !led3On;

      // check if we need to change note
      if ((millis() - noteMillis) > noteDuration)
      {
        // determine note duration
        divider = *(melody + currentNote + 1);
        if (divider > 0) {
          // regular note, just proceed
          noteDuration = (wholenote) / divider;
        } else if (divider < 0) {
          // dotted notes are represented with negative durations!!
          noteDuration = (wholenote) / abs(divider);
          noteDuration *= 1.5; // increases the duration in half for dotted notes
        }
        noteMillis = millis();
        tone(speakerPin, *(melody + currentNote), noteDuration*0.9);

        currentNote += 2;
        if (*(melody + currentNote) == END) {
          currentNote = 0;
        }
      }

      // check if max song duration reached
      if((millis() - soundMillis) > maxPlayDuration)
      {
          // stop playing melody
          noTone(speakerPin);
          currentDisplayStatus = time;       
      }
      break;
  }

  rotating = true;  // reset the debouncer

  // handle push-button of the encoder
  if (digitalRead(pushButton) == LOW )  {
    if (millis() - menuStartMillis > 500)
    {
      menuStartMillis = millis();
      switch (currentDisplayStatus)
      {
        case displayStatus::time:
          currentDisplayStatus = menu;
          colonOn = false;
          led3On = false;
          break;
        case displayStatus::setAlarm:
        case displayStatus::setTime:
          currentDisplayStatus = time;
          break;
        case displayStatus::playingMelody:
          // stop playing melody
          noTone(speakerPin);
          currentDisplayStatus = time;
          break;
        case displayStatus::menu:
          switch (activeMenuOption)
          {
            case 0:
              /* Name 1 */
              currentDisplayStatus = displayStatus::playingMelody;

              // select melody
              melody = melody2;
              tempo = tempo2;

              // setup melody parameters, rest is taken care of in the loop
              wholenote = (60000 * 4) / tempo;
              currentNote = 0;
              noteDuration = 0;
              soundMillis = millis();
              colonOn = true;
              break;
            case 1:
              /* Name 2 */
              currentDisplayStatus = displayStatus::playingMelody;

              // select melody
              melody = melody1;
              tempo = tempo1;

              // setup melody parameters, rest is taken care of in the loop
              wholenote = (60000 * 4) / tempo;
              currentNote = 0;
              noteDuration = 0;
              soundMillis = millis();              
              colonOn = true;
              break;
            case 2:
              /* set time */
              currentDisplayStatus = displayStatus::setTime;
              colonOn = true;
              break;
            case 3:
              /* set alarm */
              currentDisplayStatus = displayStatus::setAlarm;
              colonOn = true;
              break;
            case 4:
              /* toggle alarm */
              currentDisplayStatus = displayStatus::time;
              alarmActive = !alarmActive;
              colonOn = true;
              break;              
            default:
              break;
          }
          break;
      }
    }
  }
}

void iProcess()
{
  // called from timer

  if (stepCounter <= intensity)
  {
    // send out values to shift registers. Use NOT since low=ON (see top of file)
    latchOff();
    spiTransfer(~digitValue[drawDigit]); // digitvalue for current digit -> goes to register #2
    spiTransfer(~(digitCode[drawDigit] | (colonOn ? colonCode : 0x0) | (led3On ? led3Code : 0x0)))  ; // position (register #1) for current digit & colon
    latchOn(); // update display register from shift register
  }
  else
  {
    // blank digit
    latchOff();
    spiTransfer(~0x0); // data for current digit
    spiTransfer(~digitCode[drawDigit]); // digitcode for current digit
    latchOn(); // update display register from shift register
  }

  // next digit?
  stepCounter++;
  if (stepCounter > 10)
  {
    drawDigit = (drawDigit == 3) ? 0 : drawDigit + 1;
    stepCounter = 0;
  }
}

void setupSPI()
{
  byte clr;
  SPCR |= ( (1 << SPE) | (1 << MSTR) | (1 << DORD) ); // enable SPI as master, LSB first (DORD)
  SPCR &= ~( (1 << SPR1) | (1 << SPR0) ); // clear prescaler bits
  clr = SPSR; // clear SPI status reg
  clr = SPDR; // clear SPI data reg
  SPSR |= (1 << SPI2X); // set prescaler bits
  delay(10);
}

byte getSymbol(char character)
{
  return symbol[char(character) - 48];
}

byte spiTransfer(byte data)
{
  // not the best speed, but should be fine
  SPDR = data;                    // Start the transmission
  while (!(SPSR & (1 << SPIF)));  // Wait the end of the transmission
  return SPDR;                    // return the received byte, we don't need that
}

void latchOn() {
  bitSet(PORTB, latchPinPORTB);
}

void latchOff() {
  bitClear(PORTB, latchPinPORTB);
}

// encoder routines

// Interrupt on A changing state
void doEncoderA() {
  // debounce
  if ( rotating ) delay (1);  // wait a little until the bouncing is done

  // Test transition, did things really change?
  if ( digitalRead(encoderPinA) != A_set ) { // debounce once more
    A_set = !A_set;

    // adjust counter + if A leads B
    if ( A_set && !B_set )
      encoderPos += 1;

    rotating = false;  // no more debouncing until loop() hits again
  }
}

// Interrupt on B changing state, same as A above
void doEncoderB() {
  if ( rotating ) delay (1);
  if ( digitalRead(encoderPinB) != B_set ) {
    B_set = !B_set;
    //  adjust counter - 1 if B leads A
    if ( B_set && !A_set )
      encoderPos -= 1;

    rotating = false;
  }
}
