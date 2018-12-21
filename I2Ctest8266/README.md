# I2Ctest8266
I2C (clock stretch) test for ESP8266.


## Introduction
One of my projects uses an I2C slave device that performs clock stretching 
(it is the [CCS811](https://github.com/maarten-pennings/CCS811)).
As I2C master I use an ESP8266. I have used the ESP8266 with other slaves 
(e.g. [iAQ-Core](https://github.com/maarten-pennings/iAQcore)) 
that require clock stretching, but somehow the ESP8266-CCS811 combo did not workreliably.

I had a look at the [I2C bit bang driver](https://github.com/esp8266/Arduino/blob/master/cores/esp8266/core_esp8266_si2c.c).
In that driver, there are a couple of places with the statement `SCL_HIGH()`.
With that statement, the master releases the SCL line (i.e. no longer pulling to GND). 
SCL should go high, unless the _slave_ still pulls it low - this is clock stretching.
And indeed all those statements are followed with a clock stretch check:
```
  while (SCL_READ() == 0 && (i++) < twi_clockStretchLimit); // Clock stretching
```

All places in the driver had that check, except two.
I added the clock stretch check in those two places, and indeed, the CCS811 driver was working.
So I submitted an issue for: [5340](https://github.com/esp8266/Arduino/issues/5340).

Unfortunately, I found out my CCS811 was still suffering from I2C hick-ups.
I decide to test it more structurally.


## Plan
Since my problem seemed to revolve around clock stretching, I wanted to test that.
But I already knew that the ESP8266 does support clock stretching (e.g. iAQcore)
but also that it did not support this in all cases (fix I did for CCS811).
So apparently, clock stretching is sometines accepted by the ESP driver and sometimes not.

My hypothesis was: clock stretching is accepted in some _positions_ in a transaction and not at other positions.
I could image that a stretch would be ok after an sending a register address to be read, 
but maybe not in the middle of receiving and register value.

I decided to write a tool that can clock stretch any arbitrary clock pulse in an I2C transaction.
You find that tool as a companion to this sketch: [I2C-tool](../I2C-tool). That tool 
is actually an I2C slave that can be configured to do a clock strecth of x us at 
clock pulse y for the coming z transactions. The tool also has a loop-back feature: 
you can write bytes to it, and read them back.

This sketch is a test program.
It assumes that an ESP8266 ...







