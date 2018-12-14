# I2C-tool
ESP8266 firmware implementing an I2C slave with spy, loop-back and configurable clock stretch

## Introduction
This ESP8266 project implements an I2C slave with the following features
 - spy: echos all traffic to UART
 - loop-back: write bytes, read back
 - clock stretch injector: can stretch any clock pulse

The firmware implements this as bit bang on two pins, using interrupts.
Unfortunately, the ESP8266 has high interrupt latency: on 160MHz, the interrupt latency is ~4.3us. 
Note that slowest I2C has a clock of 100kHz, so interrupts come every 5us, which is tight.
As a result it is recommended to run at e.g. 32kHz.

## Registers

| REGISTER | ACCESS | MSB/LSB | DESCRIPTION                                     |
|:--------:|:------:|:-------:|:-----------------------------------------------:|
| ENABLE   | w/r    | 00/01   | Number of transactions clock stretch is enabled |
| PULSE    | w/r    | 02/03   | The CLK pulse that is stretched (starts with 1) |
| US       | w/r    | 04/05   | Clock stretch time in us                        |
| QPULSE   | r      | 06/07   | Query: Number of CLK pulses in last transaction |
| QUS      | r      | 08-11   | Query: Number of us of the last transaction     |
| RSVD     | w/r    | 12-15   | Reserved                                        |
| MSG      | w/r    | 16-31   | Buffer for loop-back message                    |

