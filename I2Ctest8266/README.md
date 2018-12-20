# I2Ctest8266
I2C (clock stretch) test for ESP8266.


## Introduction
One of my projects used an I2C slave device that performs clock stretching 
(it was the [CCS811](https://github.com/maarten-pennings/CCS811)).
As master I used an ESP8266. I had used other slaves that require clock stretching,
but somehow the CCS811 somehow did not wokr well with the ESP8266.


