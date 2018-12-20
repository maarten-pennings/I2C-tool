// I2Ctest8266.ino - I2C (clock stretch) test for ESP8266


#include <Wire.h>


const uint8_t SLAVE_ADDR= 0x44/2; // I2C slave address of I2C-tool


// Writes `count0` bytes from `buf0` to the I2C-tool. 
// Returns 0 iff communication successful.
int i2cwrite(uint8_t * buf0, size_t count0) {
  Wire.beginTransmission(SLAVE_ADDR);               // START, SLAVE_ADDR
  for( int i=0; i<count0; i++) Wire.write(buf0[i]); // Write bytes
  int res0= Wire.endTransmission(true);             // STOP
  delay(25); // Give I2c-tool some time to print this transaction
  return res0!=0;
}


// Writes `count0` bytes from `buf0` to the I2C-tool. 
// Then reads `count1` bytes from the I2C-tool into `buf1`. 
// The write and the read are mastered as one I2C transaction.
// Returns 0 iff communication successful.
int i2cwriteread(uint8_t * buf0, size_t count0, uint8_t * buf1, size_t count1 ) {
  Wire.beginTransmission(SLAVE_ADDR);                // START, SLAVE_ADDR
  for( int i=0; i<count0; i++) Wire.write(buf0[i]);  // Write bytes
  int res0= Wire.endTransmission(false);             // Repeated START
  int res1=Wire.requestFrom(SLAVE_ADDR,count1);      // From slave, read bytes, STOP
  for( int i=0; i<count1; i++ ) buf1[i]=Wire.read();
  delay(25); // Give I2c-tool some time to print
  return (res0!=0) || (res1!=count1);
}


// Writes `count0` bytes from `buf0` to the I2C-tool. 
// Then write `count1` bytes from `buf1` to the I2C-tool. 
// The two writes are mastered as one I2C transaction.
// Returns 0 iff communication successful.
int i2cwritewrite(uint8_t * buf0, size_t count0, uint8_t * buf1, size_t count1 ) {
  Wire.beginTransmission(SLAVE_ADDR);                // START, SLAVE_ADDR
  for( int i=0; i<count0; i++) Wire.write(buf0[i]);  // Write bytes
  int res0= Wire.endTransmission(false);             // Repeated START
  Wire.beginTransmission(SLAVE_ADDR);                // START, SLAVE_ADDR
  for( int i=0; i<count1; i++) Wire.write(buf1[i]);  // Write bytes
  int res1= Wire.endTransmission(true);              // STOP
  delay(25); // Give I2c-tool some time to print
  return (res0!=0) || (res1!=0);
}


// Writes `count0` bytes from `buf0` to the I2C-tool. 
// Then reads `count1` bytes from the I2C-tool into `buf1`. 
// Finally reads `count2` bytes from the I2C-tool into `buf2`. 
// The write, read and read are mastered as one I2C transaction.
// Returns 0 iff communication successful.
int i2cwritereadread(uint8_t * buf0, size_t count0, uint8_t * buf1, size_t count1, uint8_t * buf2, size_t count2 ) {
  Wire.beginTransmission(SLAVE_ADDR);                // START, SLAVE_ADDR
  for( int i=0; i<count0; i++) Wire.write(buf0[i]);  // Write bytes
  int res0= Wire.endTransmission(false);             // Repeated START
  int res1=Wire.requestFrom(SLAVE_ADDR,count1,false);// From slave, read bytes, no STOP
  for( int i=0; i<count1; i++ ) buf1[i]=Wire.read();
  int res2=Wire.requestFrom(SLAVE_ADDR,count2);      // From slave, read bytes, STOP
  for( int i=0; i<count2; i++ ) buf2[i]=Wire.read();
  delay(25); // Give I2c-tool some time to print
  return (res0!=0) || (res1!=count1) || (res2!=count2);
}


// Simple I2C loop-back test: 
//   step 1: in one transaction write 4 bytes,
//   step 2: in one transaction read 4 bytes,
//   step 3: compare if read bytes match written bytes.
// The 4 test bytes are auto generated (and change each call), one is the passed `tag`.
// Returns 0 iff no problems; for non-zero value, flags have this meaning: 
//   bit  4:indicates write transaction failed
//   bit  5:indicates read transaction failed
//   bits i:0..3 indicate failure of loop-back for byte i.
int loopback1(int tag) {
  int err;
  int result=0;
  // Auto generate test bytes
  static bool odd;
  odd=!odd; // (flipping `odd` ensures new message every cycle)
  uint8_t bytes[]= { odd?0x00:0xff, odd?0xff:0x00, odd?0x55:0xAA, tag }; 
  // Step 1: Setup write buffer
  uint8_t wbuf[]= { 0x10, bytes[0], bytes[1], bytes[2], bytes[3] };
  // Step 1: Actually write
  err= i2cwrite(wbuf, sizeof wbuf);
  if( err ) {
    result|=0x10;
    Serial.printf("  loopback1 %d: FAILED (write)\n", tag);
  }
  // Step 2: Setup read buffer
  uint8_t rbuf0[]= { 0x10 };
  uint8_t rbuf1[]= { 0x33, 0x33, 0x33, 0x33 }; // prefill in case read fails
  // Step 2: Actually read
  err= i2cwriteread(rbuf0, sizeof rbuf0, rbuf1, sizeof rbuf1);
  if( err ) {
    result|=0x20;
    Serial.printf("  loopback1 %d: FAILED (read)\n", tag);
  }
  // Step 3: compare
  for(int i=0; i<sizeof bytes; i++ ) {
    err= bytes[i]!=rbuf1[i];
    if( err ) {
      result |= 1<<i;
      Serial.printf("  loopback1 %d: FAILED (byte %d written 0x%02x read 0x%02x)\n", tag, i, bytes[i], rbuf1[i] );
    }
  }
  return result;
}


// Multi segment I2C loop-back test: 
//   step 1: in one transaction write 2 bytes and another 2 bytes, 
//   step 2: in one transaction read 2 bytes and another 2 bytes, 
//   step 3: finally compare if read bytes match written bytes.
// The 4 test bytes are auto generated (and change each call), one is the passed `tag`.
// Returns 0 iff no problems; for non-zero value, flags have this meaning: 
//   bit  4:indicates write transaction failed
//   bit  5:indicates read transaction failed
//   bits i:0..3 indicate failure of loop-back for byte i.
int loopback2(int tag) {
  int err;
  int result=0;
  // Auto generate test bytes
  static bool odd;
  odd=!odd; // (flipping `odd` ensures new message every cycle)
  uint8_t bytes[]= { odd?0x00:0xff, odd?0xff:0x00, odd?0x55:0xAA, tag }; 
  // Step 1: Setup write buffers
  uint8_t wbuf0[]= { 0x10, bytes[0], bytes[1] };
  uint8_t wbuf1[]= { 0x12, bytes[2], bytes[3] };
  // Step 1: Actually write
  err= i2cwritewrite(wbuf0, sizeof wbuf0, wbuf1, sizeof wbuf1);
  if( err ) {
    result|=0x10;
    Serial.printf("  loopback2 %d: FAILED (write)\n", tag);
  }
  // Step 2: Setup read buffers
  uint8_t rbuf0[]= { 0x10 };
  uint8_t rbuf1[]= { 0x33, 0x33, 0x33, 0x33 }; // prefill in case read fails
  // Step 2: Actually read
  err= i2cwritereadread(rbuf0, sizeof rbuf0, rbuf1, 2, rbuf1+2, 2);
  if( err ) {
    result|=0x20;
    Serial.printf("  loopback2 %d: FAILED (read)\n", tag);
  }
  // Step 3: compare
  for(int i=0; i<sizeof bytes; i++ ) {
    err= bytes[i]!=rbuf1[i];
    if( err ) {
      result |= 1<<i;
      Serial.printf("  loopback2 %d: FAILED (byte %d written 0x%02x read 0x%02x)\n", tag, i, bytes[i], rbuf1[i] );
    }
  }
  return result;
}


// Configure the I2C-tool to inject a clock stretch at pulse `pulse`,
// duration `us`, and enabled for `enable` transactions.
// Returns 0 iff communication successful.
int clock_stretch_inject(int enable, int pulse, int us) {
  uint8_t buf[]= { 
    0x00, // register address (ENABLE)
    (enable>>8)&0xff, (enable>>0)&0xff, 
    (pulse >>8)&0xff, (pulse >>0)&0xff,
    (us    >>8)&0xff, (us    >>0)&0xff
  };
  
  int err= i2cwrite(buf, sizeof buf);
  if( err ) {
    Serial.printf("  clock_stretch_inject %d %d %d: FAILED\n", enable, pulse, us);
  }
  return err;
}


// Execute loopback1, with a clock stretch at every clock valey
int errors1= 0;
void run1() {
  Serial.printf("\nTest run 1 (write and writeread) with injected clock stretch\n");
  for( int pulse=1; pulse<0x50; pulse++ ) { // write in loopback1 is 0x37 pulses, read is 0x41 pulses
    int r0= clock_stretch_inject(5,pulse,100); 
    int r1= loopback1(pulse);
    int r2= loopback1(pulse);
    if( r0!=0 || r1!=0 || r2!=0 ) {
      Serial.printf("Case %d FAIL\n",pulse );
      errors1++;
    } else {
      Serial.printf("Case %d PASS\n",pulse);
    }
  }
}


// Execute loopback2, with a clock stretch at every clock valey
int errors2= 0;
void run2() {
  Serial.printf("\nTest run 2 (writewrite and writereadread) with injected clock stretch\n");
  for( int pulse=1; pulse<0x50; pulse++ ) { // write in loopback1 is 0x4A pulses, read is 0x4b pulses
    int r0= clock_stretch_inject(5,pulse,100); 
    int r1= loopback2(pulse);
    int r2= loopback2(pulse);
    if( r0!=0 || r1!=0 || r2!=0 ) {
      Serial.printf("Case %d FAIL\n",pulse );
      errors2++;
    } else {
      Serial.printf("Case %d PASS\n",pulse);
    }
  }
}


void setup() {
  // Enable serial
  Serial.begin(115200);
  Serial.printf("\n\nStarting ESP8266 (clock stretch) test\n");

  // Enable I2C
  Wire.setClock(50000L); // I2C-tool can not keep up with 100 kHz
  Wire.begin(); 
  int ping=i2cwrite(0,0);
  if( ping ) { 
    Serial.printf("ERROR: could not ping I2C-tool\n");
  } else {
    Serial.printf("Assumption: I2C-tool is hooked to I2C bus\n");
  }

  // Run the tests
  run1();
  run2();
}


void loop() {
  Serial.printf("\nCompleted run (errors1 %d, errors2 %d)\n", errors1, errors2 );
  delay(10000);
}
