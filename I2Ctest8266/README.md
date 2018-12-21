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

My hypothesis was: clock stretching is accepted in some _positions_ within a transaction and not at other positions.
I could image that a stretch would be ok right after sending a complete register address (after bit 8), 
but maybe not in the middle of sending a register value (between bit 3 and 4).

I decided to write a tool that could stretch any clock pulse in an I2C transaction.
You find that tool as a companion to this sketch: [I2C-tool](../I2C-tool). 
That tool is actually an I2C slave that can be configured to do a clock stretch of 
x us at clock pulse number y for the coming z transactions. 
The tool also has a loop-back feature: you can write bytes to it, and read them back.

This sketch is the actual test program.
It assumes that an ESP8266 (number 1) runs it, using the standard I2C bit bang driver.
It assumes this first ESP8266 is connected to a second ESP8266, which runs the I2C-tool.

![testsetup](../pics/testsetup.png)


## Simple test case (run1/loopback1)
I started with a simple test case. It uses the function `loopback1()`. 
This function writes 4 bytes to the loopback device (the MSG register), and reads them back.

Basically, it sends these two transactions

```
s44a 10a      00a FFa 55a 02a p
s44a 10a s45a 00a FFa 55a 02n p
```

Note that they payload bytes are chosen all 0 bits (0x00), all 1 bits (0xFF) and mixed (0x55 or 0b01010101).
The last byte is a `tag` byte, in practice the test case number.

Since the MSG register is persistent, a missing bit in writing will go unnoticed.
So I decided to do a dual write/read cycle, the second time with most data bits flipped.

```
s44a 10a      00a FFa 55a 02a p
s44a 10a s45a 00a FFa 55a 02n p
s44a 10a      FFa 00a AAa 02a p
s44a 10a s45a FFa 00a AAa 02n p
```

The write transaction contain 55 clock pulses (valeys) and the read transactions 65.
A test run executes the double loop-back (the above 4 transactions) 65 times, 
each time stretching another clock pulse. In the last 10 double loop-backs (from 56 to 65), 
the stretch does not occur for the writes, since they only have 55 pulses. But the reads
are impacted.

The function `clock_stretch_inject()` configures the I2C-tool to inject clock stretches,
and the functun `run1()` performs the 65 double loop-backs with walking clock stretch.


