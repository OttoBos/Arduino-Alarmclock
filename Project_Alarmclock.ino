#include <SPI.h>
#include <TimerOne.h>
#include <Wire.h>
#include "RTClib.h"
RTC_DS1307 RTC;

/*
Otto, March - September 2017

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
    encoder connected to:
    - --> not implemented yet
    code from http://playground.arduino.cc/Main/RotaryEncoders, the rafbuff part (adapted to get rid of delay in interupt routine)

*/

#define LATCH 10 // pin 12 on HC595
#define CLK 13   // pin 11 on HC595
#define DATA 11  // pin 14 on HC595

//--- Important: All Pins must be 8 or higher (in PORTB range)
const byte latchPinPORTB = LATCH - 8;
const byte clockPinPORTB = CLK - 8;
const byte dataPinPORTB = DATA - 8;

// This is the hex value of each number stored in an array by index num
// were using ascii equivalent (48=0 ... 57=0, 65=A .. 90=Z. signs in between all blanks 
// use one of these (eg colon) for space
const byte symbol[43] = {
0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xF6, // 0-9
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                   // ascii 58-64, all blanks here
0xEC, 0x3E, 0x9C, 0x7A, 0x9E, 0x8E, 0xF6, 0x2E,             // A-H
0x60, 0x70, 0x02, 0x1C, 0x02, 0xEC, 0xFC, 0xCE,             // I-P
0xE6, 0x8C, 0xB6, 0x1E, 0x38, 0x7C, 0x02, 0x6E,             // Q-X
0x66, 0xDA};                                                // Y and Z

const byte digitCode[4] = {0x08, 0x04, 0x02, 0x01};
const byte dPointCode = 0x80; // add to output for register #1 to switch on digital point
const byte colonCode = 0x40; // same for colon
const byte L3Code = 0x20; // same for LED 3
const int timerDelay = 200; // speed of timer in micros

// for display routine
byte intensity = 1; // light intensity, 0-10, higher = brighter
byte blinkSpeed = 255; // speed of blinking colon, 0-255, higher=slower
volatile byte drawDigit = 0; // 0=digit1, .. 3=digit4
volatile byte stepCounter = 0;
volatile bool blinkOn = false;

DateTime now;
int toDisplay = 0;
uint8_t seconds;

byte digitValue[4] = {0x0, 0x0, 0x0, 0x0}; // start with empty digits

void setup(){
 
  pinMode(LATCH, OUTPUT);  // RCLK,  pin 12 on HC595
  pinMode(CLK, OUTPUT);    // SRCLK, pin 11 on HC595
  pinMode(DATA, OUTPUT);   // SER,   pin 14 on HC595

  digitalWrite(LATCH,LOW);
  digitalWrite(DATA,LOW);
  digitalWrite(CLK,LOW);

  // setup SPI
  setupSPI();

  // setup timer
  Timer1.initialize(timerDelay);
  Timer1.attachInterrupt(iProcess);

  // RTC code
  Wire.begin();
  RTC.begin();
  
  // following line sets the RTC to the date & time this sketch was compiled
  //RTC.adjust(DateTime(__DATE__, __TIME__));
  //RTC.adjust(DateTime(__DATE__, "20:29:00"));
}

void loop(){
  
  // set values for digitValue[] here
  // display will be taken care of by timer

  now = RTC.now(); 
  toDisplay = (now.hour()*100+now.minute());
  seconds = now.second();
  blinkOn = (seconds % 2 == 0) ? 0 : 1;

  for(int digit = 3 ; digit >= 0 ; digit--)
  {
    if(toDisplay>0)
    {
      digitValue[digit] = symbol[toDisplay % 10];
      toDisplay /= 10;
    }
    else
    {
      digitValue[digit] = (digit==3) ? symbol[0] : 0x0;
    }
  }

}

void iProcess()
{
  // called from timer
  
  if(stepCounter<=intensity)
  {  
    // send out values to shift registers
    latchOff();
    spiTransfer(~digitValue[drawDigit]); // digitvalue for current digit -> goes to register #2
    spiTransfer(~(digitCode[drawDigit] | (blinkOn ? colonCode : 0x0))); // position (register #1) for current digit. Use NOT since low=ON (see top of file)
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
  if(stepCounter>10)
  {
    drawDigit = (drawDigit==3) ? 0 : drawDigit+1;
    stepCounter = 0;
  }

}

void setupSPI()
{
 byte clr;
 SPCR |= ( (1<<SPE) | (1<<MSTR) | (1<<DORD) ); // enable SPI as master, LSB first (DORD)
 SPCR &= ~( (1<<SPR1) | (1<<SPR0) ); // clear prescaler bits
 clr=SPSR; // clear SPI status reg
 clr=SPDR; // clear SPI data reg
 SPSR |= (1<<SPI2X); // set prescaler bits
 delay(10);
}

byte getSymbol(char character)
{
 return symbol[char(character)-48];
}

byte spiTransfer(byte data)
{
 // not the best speed, but should be fine 
 SPDR = data;                    // Start the transmission
 while (!(SPSR & (1<<SPIF)));    // Wait the end of the transmission
 return SPDR;                    // return the received byte, we don't need that
}

void latchOn(){
bitSet(PORTB,latchPinPORTB);
}

void latchOff(){
bitClear(PORTB,latchPinPORTB);
}
