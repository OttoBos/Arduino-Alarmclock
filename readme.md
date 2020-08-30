# Arduino alarm clock

## Description

This is a complete alarm clock that can be controlled by a rotating encoder with push button. It has a menu structure and can plays some songs too.

I built it in a few phases and the code ended up being a bit of a scope soup - but it works.

## Inspiration

I borrowed a lot of code from tutorials sites:

- shiftOutFast, Joseph Francis (https://forum.arduino.cc/index.php?topic=37304.0)
- http://arduino.stackexchange.com/questions/16348/how-do-you-use-spi-on-an-arduino
- http://www.elecrow.com/wiki/index.php?title=Tiny_RTC
- http://playground.arduino.cc/Main/RotaryEncoders
- https://github.com/robsoncouto/arduino-songs 

## Hardware

### Display unit

The display is a common anode 4-digit 7-segment display, driven by two shift registers and some resistors and transistors.

Shopping list:
- 2 x 74HC595 shift register
- 1 x YS common anode 4-digit 7-segment display
- 8 x 440 Ohm resistors (7 segements + colon)
- 4 x BC557 PNP transistir
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

The registers are linked (arduino - #1 - #2) so we push the bits for #2 first, then the bits for #1. See also comments in code. 

### Real time clock

To keep track of time we're using a RTC_DS1307 RTC, connected to the Arduino over I2C.

### Rotating encoder

The rotating encoder is a Keyes, with push button. I added a few small capacitors (10 nF) for hardware debouncing

### Speaker

The speaker is a simple 8 Ohm 0.25W speaker I scavenged from a broken toy. Plus a 100 Ohm resistor.
