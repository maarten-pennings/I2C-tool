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
As a result it is recommended to use an I2C clock of e.g. 32kHz.


## Spy 
The slave device can be added to an I2C bus, and any transaction it sees is echoed over Serial.
This is the spy feature.

Find below an example where some master sends a ping to device with slave address 0x86.
This consists of a START condition `s`, the `86` slave address and a STOP `p`. 
Note the `a` after `86`, which is the acknowledge of the slave.

Next, the master writes 0x00 to address 0x10. Again we see `s` for START, `86` as slave address
with an `a` acknowledge of the slave, then `10` as register address with `00` as register data.
Both are acknowledged `a` by the slave. Then the final STOP `p`.

The third run is a two-byte read from register `00`. The transaction consists of a START `s`, 
slave addres `86` with acknowledge `a` (7-bit address 43 with direction bit 0 for _write_), 
and register address `00` with acknowledge `a`.
Then follows a (repeated) START `s` with slave addres `87` with acknowledge `a` 
(7-bit address 43 with direction bit 1 for _read_). Next the slaves sends the data:
`10` with `a` from the master (not really an acknowledge, but signals "extra read follows"), and
`02` with `n` from the master (not really a nack, but signals "no read follows"),
terminated by a STOP `p`.

```
i2c: [s86a p]

i2c: [s86a 10a 00a p]

i2c: [s86a 00a s87a 10a 02n p]
```

The following example is a ping to device `88`. There is no such device on the bus, so there is no acknowledge.
The final example is a ping to device `44`. 0x44 is the slave address of the device itself, so in this case the 
deive is not only spy-ing, but also generating the acknowlegde `a`.

```
i2c: [s88n p]

i2c: [s44a p]
```


## Loop-back

The device can also be used for loop-back. For this purpose the device has a loop-back buffer, 
a register called MSG. The MSG register is located at address 0x10 and is 16 bytes long, 
i.e. it runs up to address 0x1F. Arbitrary bytes can be written to MSG, and they can be read back.
The MSG data is retained as long as the device is not power-cycled or reset (or MSG is overwritten).

The device itself has 0x44 (writing) or 0x45 (reading) as I2C slave address.


## CLock stretch injector

| REGISTER | ACCESS | MSB/LSB | DESCRIPTION                                     |
|:--------:|:------:|:-------:|:-----------------------------------------------:|
| ENABLE   | w/r    | 00/01   | Number of transactions clock stretch is enabled |
| PULSE    | w/r    | 02/03   | The CLK pulse that is stretched (starts with 1) |
| US       | w/r    | 04/05   | Clock stretch time in us                        |
| QPULSE   | r      | 06/07   | Query: Number of CLK pulses in last transaction |
| QUS      | r      | 08-0B   | Query: Number of us of the last transaction     |
| RSVD     | w/r    | 0C-0F   | Reserved                                        |
| MSG      | w/r    | 10-1F   | Buffer for loop-back message                    |

