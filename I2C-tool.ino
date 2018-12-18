// I2C-tool.ini - I2C slave device implementing spy, loopback and configurable clock stretch (ESP8266)
#define VERSION 1


// ===========================================================================================
// We can delay() in an ISR, so we do busy spin.
// The routine time_init() calibrates how many loop iterations are  
// required to spin one milliseconds. It stores this in time_lpms.
// The routine time_wait(us) does the actual wait.


// Calibrate timing
#define TIME_COUNT (1000*1000) // less then 2G/1000
int time_lpms;
void time_init() {
  delay(1); // Feed watchdog
  uint32_t t0=micros();
  for( volatile uint32_t i=0; i<TIME_COUNT; i++ ) /*skip*/ ;
  uint32_t t1=micros();
  uint32_t dt=t1-t0;
  time_lpms= 1000*(TIME_COUNT)/dt;
  Serial.printf("Time: %u loops in %u us: %d loops/ms\n",TIME_COUNT,dt,time_lpms);  
  delay(1); // Feed watchdog
#if 0  
  // Test
  for( int i=10; i<1000000; i*=10 ) {
    uint32_t t0=micros();
    time_wait_us(i);
    uint32_t t1=micros();
    uint32_t dt=t1-t0;
    Serial.printf("Time: %d ~ %u us\n",i,dt);  
    delay(1); // Feed watchdog
  }
#endif
}


// Busy wait - needs the time_init() once to set up time_lpms
void time_wait_us(int us) {
  uint32_t count= time_lpms*us/1000;
  for( volatile uint32_t i=0; i<count; i++ ) /*skip*/ ;  
}


// ===========================================================================================
// This firmware implements an I2C slave device


// The registers if the slave device
#define SLAVE_ADDR                  0x44 // I2C slave address
#define SLAVE_REGCOUNT              32   // Number of bytes exposed as registers over I2C
#define SLAVE_REG_ENABLE()          (slave_regs[0]*256+slave_regs[1]) // Register ENABLE: number of transaction the clock stretch enabled
#define SLAVE_REG_PULSE()           (slave_regs[2]*256+slave_regs[3]) // Register PULSE: which pulse to stretch
#define SLAVE_REG_US()              (slave_regs[4]*256+slave_regs[5]) // Register US: how many us to stretch
#define SLAVE_REG_QPULSE()          (slave_regs[6]*256+slave_regs[7]) // Register QPULSE: number of pulses in last transaction
#define SLAVE_REG_QUS()             (slave_regs[8]*256UL*256*256+slave_regs[9]*256UL*256+slave_regs[10]*256UL+slave_regs[11]) // Register QUS: duration in transaction in us
#define SLAVE_REG_RSVD()            (slave_regs[12]*256UL*256*256+slave_regs[13]*256UL*256+slave_regs[14]*256UL+slave_regs[15]) // Register RSVD: reserved
#define SLAVE_REG_SET_ENABLE(c)     do { slave_regs[0]=(c)>>8; slave_regs[1]=(c); } while(0)
#define SLAVE_REG_SET_QPULSE(c)     do { slave_regs[6]=(c)>>8; slave_regs[7]=(c); } while(0)
#define SLAVE_REG_SET_QUS(us)       do { slave_regs[8]=(us)>>24; slave_regs[9]=(us)>>16; slave_regs[10]=(us)>>8; slave_regs[11]=(us); } while(0)
uint8_t slave_regs[SLAVE_REGCOUNT]; // The backing memory
uint8_t slave_cra;                  // The CRA or Current Register Address 


// Computes a hash, if the hash changes the registers are printed.
int slave_hash() {
  int f=1;
  int hash= slave_cra*f++;
  for( int i=0; i<SLAVE_REGCOUNT; i++ ) hash+= slave_regs[i]*f++;
  return hash;
}


// ===========================================================================================
// The I2C slave function is implemented as bit-bang on two pins.


// Which pins are used for the I2C bus?
#define scl_pin D1
#define sda_pin D2
// Macros to write and read the I2C pins
#define SCL_LOW()    (GPES = (1 << scl_pin))        // Drive the SCL pin low
#define SCL_HIGH()   (GPEC = (1 << scl_pin))        // Leave the SCL pin high (pullup)
#define SCL_DRIVE()  ((GPE & (1 << scl_pin)) != 0)  // Are _we_ driving SCL pin (low)
#define SCL_READ()   ((GPI & (1 << scl_pin)) != 0)  // What is the status of the SCL pin (we or other)
#define SDA_LOW()    (GPES = (1 << sda_pin))        // Enable SDA (becomes output and since GPO is 0 for the pin, it will pull the line low)
#define SDA_HIGH()   (GPEC = (1 << sda_pin))        // Disable SDA (becomes input and since it has pullup it will go high)
#define SDA_DRIVE()  ((GPE & (1 << sda_pin)) != 0)
#define SDA_READ()   ((GPI & (1 << sda_pin)) != 0)


// Which pins are used for monitoring (with scope)?
#define sclmon_pin D5
#define sdamon_pin D6
#define SCLMON_LOW()    (GPES = (1 << sclmon_pin))
#define SCLMON_HIGH()   (GPEC = (1 << sclmon_pin))
#define SDAMON_LOW()    (GPES = (1 << sdamon_pin))
#define SDAMON_HIGH()   (GPEC = (1 << sdamon_pin))


// ===========================================================================================
// The state machine runs in the ISR, so "prints" are made to a log buffer


// Logging the trace of the statemachine
#define LOG_SIZE 1024
int     log_head=0;
int     log_tail=0;
char    log_data[LOG_SIZE];


void log_char(char ch) {
  int newhead= (log_head+1) % LOG_SIZE;
  if( newhead==log_tail ) {
    // overflow
  } else {
    log_data[log_head]= ch;
    log_head= newhead;
  }
}


void log_byte(int b) {
  #define HX(n) ( (n)<10 ? (n)+'0' : (n)-10+'A' )
  log_char(HX(b/16));
  log_char(HX(b%16));
}


char log_get(void) {
  if( log_tail==log_head ) {
    return 0;
  } else {
    // Get data
    char b=log_data[log_tail];
    // Step tail
    log_tail= (log_tail+1) % LOG_SIZE;
    return b;
  }
}


// ===========================================================================================
// The core function is a state machine that tracks the two I2C pins


// The states of the statemachine tracking the I2C lines
#define I2CSTATE_UNKNOWN       1
#define I2CSTATE_ERROR         2
#define I2CSTATE_IDLE          3
#define I2CSTATE_STARTED       4
#define I2CSTATE_ADDRBIT_SCLLO 5
#define I2CSTATE_ADDRBIT_SCLHI 6
#define I2CSTATE_ADDRACK_SCLLO 7
#define I2CSTATE_ADDRACK_SCLHI 8
#define I2CSTATE_DATABIT_SCLLO 9
#define I2CSTATE_DATABIT_SCLHI 10
#define I2CSTATE_DATAACK_SCLLO 11
#define I2CSTATE_DATAACK_SCLHI 12


// Recording the state
typedef struct i2c_s {
  // Signal level
  int      state;   // Status of I2C state machine (see I2CSTATE_XXX)
  int      pulse;   // The number of SCL pulses seen so far
  uint32_t qus;     // Transaction start, later length, in us
  int      sdacap;  // State of SDA captured on rising SCL
  int      data;    // Bits of ADDR or DATA
  int      bitcnt;  // Bit count 0..7 of bits in data
  int      logbits; // Should individual bits be logged
  // Segment level
  int      addr;    // Last slave address on the line
  int      bytecnt; // Byte count in segment
} i2c_t;
i2c_t i2c;


// The ISR coupled to the two I2C pins
void i2c_isr() {
  int scl= SCL_READ();
  int sda= SDA_READ();

  // START/STOP detector
  if( i2c.state==I2CSTATE_ADDRBIT_SCLHI 
   || i2c.state==I2CSTATE_ADDRACK_SCLHI 
   || i2c.state==I2CSTATE_DATABIT_SCLHI 
   || i2c.state==I2CSTATE_DATAACK_SCLHI 
    ) { // SCL was high, is it still? Did SDA change?
    if( scl==1 && sda!=i2c.sdacap ) { 
      // SDA changed while CLK was high
      if( sda==0 ) {
        // SDA went low during CLK high: (repeated) START
        i2c.state= I2CSTATE_STARTED;
        log_char('s'); 
      } else {
        // SDA went high during CLK high: STOP
        if( SLAVE_REG_ENABLE()>0 && SLAVE_REG_ENABLE()<0xFFFF ) SLAVE_REG_SET_ENABLE( SLAVE_REG_ENABLE()-1 );
        SLAVE_REG_SET_QPULSE(i2c.pulse);
        SLAVE_REG_SET_QUS(micros() - i2c.qus);
        i2c.state= I2CSTATE_IDLE;
        log_char('p');
        log_char(']');
      }
    }
  }

  // Normal processing
  switch( i2c.state ) {
    
    case I2CSTATE_UNKNOWN:
      if( scl==1 && sda==1 ) {
        i2c.state= I2CSTATE_IDLE; 
      } else {
        log_char('!'); // Log error state
        i2c.state= I2CSTATE_ERROR;
      }
      break;
      
    case I2CSTATE_ERROR:
      if( scl==1 && sda==1 ) {
        i2c.state= I2CSTATE_IDLE; 
      }
      break;
      
    case I2CSTATE_IDLE:
      // SCL and SDA both high ("idle state")
      if( scl==1 ) {
        if( sda==1 ) {
          // SCL=1, SDA=1: Stay in IDLE
        } else {
          // SDA went low, while SCL stays high
          log_char('['); // Absolute start (not repeated)
          log_char('s');
          i2c.pulse= 0;
          i2c.qus= micros();
          i2c.addr= 0;
          i2c.state= I2CSTATE_STARTED;
        }
      } else  {
        log_char('!'); // Log error state
        i2c.state= I2CSTATE_ERROR;
      } 
      break;

    case I2CSTATE_STARTED:
      // SCL was high, SDA was low; but SCL must go down and SDA must be first bit of addr
      if( scl==0 ) {
        i2c.pulse++; 
        if( i2c.pulse==SLAVE_REG_PULSE() && SLAVE_REG_ENABLE()>0 ) { SCL_LOW(); log_char('_'); time_wait_us(SLAVE_REG_US()); SCL_HIGH(); }
        i2c.data= 0;
        i2c.bitcnt= 0;
        i2c.logbits= i2c.pulse<SLAVE_REG_PULSE() && SLAVE_REG_PULSE()<i2c.pulse+8 && SLAVE_REG_ENABLE()>0;
        i2c.state= I2CSTATE_ADDRBIT_SCLLO; 
      }
      break;

    case I2CSTATE_ADDRBIT_SCLLO:
      // Reading address bits. SCL is low.
      if( scl==1 ) {
        // SCL went high, so SDA has data (if it stays like this the full CLK period)
        i2c.sdacap= sda;
        i2c.state= I2CSTATE_ADDRBIT_SCLHI;
      }
      break;
      
    case I2CSTATE_ADDRBIT_SCLHI:
      // Reading address bits. SCL is high.
      if( scl==0 ) {
        // SCL went low, so we have a complete address bit
        i2c.pulse++; 
        if( i2c.logbits ) log_char(i2c.sdacap+'0');
        if( i2c.sdacap ) i2c.data |= 1 << (7-i2c.bitcnt);
        i2c.bitcnt++;
        if( i2c.bitcnt<8 ) {
          i2c.state= I2CSTATE_ADDRBIT_SCLLO; 
        } else {
          // Received a complete address byte (but not yet the ack)
          i2c.addr= i2c.data; 
          if( i2c.addr==SLAVE_ADDR || i2c.addr==SLAVE_ADDR+1 ) SDA_LOW(); // Pull down to ACK the ADDR
          if( i2c.logbits ) log_char('/');
          log_byte(i2c.data);
          i2c.state= I2CSTATE_ADDRACK_SCLLO; 
        }
        if( i2c.pulse==SLAVE_REG_PULSE() && SLAVE_REG_ENABLE()>0 ) { SCL_LOW(); log_char('_'); time_wait_us(SLAVE_REG_US()); SCL_HIGH(); }
      } 
      break;
      
    case I2CSTATE_ADDRACK_SCLLO:
      // Reading address ack bit. SCL is low.
      if( scl==1 ) {
        // SCL went high, so SDA has data (if it stays like this the full CLK period)
        i2c.sdacap= sda;
        i2c.state= I2CSTATE_ADDRACK_SCLHI;
      }
      break;
      
    case I2CSTATE_ADDRACK_SCLHI:
      // Reading address ack bit. SCL is high.
      if( scl==0 ) {
        // SCL went low, so we have a complete address ack bit
        i2c.pulse++; 
        if( i2c.addr==SLAVE_ADDR ) {
          SDA_HIGH(); // Release ACK of ADDR
        } else if( i2c.addr==SLAVE_ADDR+1 ) { // We need to release ACK of ADDR, but also push out the first data bit
          uint8_t val= (0<=slave_cra && slave_cra<SLAVE_REGCOUNT) ? slave_regs[slave_cra] : 0x55;
          int mask= 1<<7;
          if( val&mask ) SDA_HIGH(); else SDA_LOW();
        }
        if( i2c.sdacap ) log_char('n'); else log_char('a');
        log_char(' '); // Space after address
        if( i2c.pulse==SLAVE_REG_PULSE() && SLAVE_REG_ENABLE()>0 ) { SCL_LOW(); log_char('_'); time_wait_us(SLAVE_REG_US()); SCL_HIGH(); }
        i2c.bitcnt= 0;
        i2c.logbits= i2c.pulse<SLAVE_REG_PULSE() && SLAVE_REG_PULSE()<i2c.pulse+8 && SLAVE_REG_ENABLE()>0;
        i2c.data= 0;
        i2c.bytecnt= 0;
        i2c.state= I2CSTATE_DATABIT_SCLLO; 
      }
      break;
      
    case I2CSTATE_DATABIT_SCLLO:
      // Reading data bits. SCL is low.
      if( scl==1 ) {
        // SCL went high, so SDA has data (if it stays like this the full CLK period)
        i2c.sdacap= sda;
        i2c.state= I2CSTATE_DATABIT_SCLHI;
      }
      break;
      
    case I2CSTATE_DATABIT_SCLHI:
      // Reading data bits. SCL is high.
      if( scl==0 ) {
        // SCL went low, so we have a complete data bit
        i2c.pulse++; 
        if( i2c.sdacap ) i2c.data |= 1 << (7-i2c.bitcnt);
        if( i2c.logbits ) log_char(i2c.sdacap+'0');
        i2c.bitcnt++;
        if( i2c.bitcnt<8 ) {
          // Push next bit out
          if( i2c.addr==SLAVE_ADDR+1 ) { 
            // Get bits from register pointed to by CRA
            uint8_t val= (0<=slave_cra && slave_cra<SLAVE_REGCOUNT) ? slave_regs[slave_cra] : 0x55;
            int mask= 1 << (7-i2c.bitcnt);
            if( val&mask ) SDA_HIGH(); else SDA_LOW();
          }  
          i2c.state= I2CSTATE_DATABIT_SCLLO; 
        } else {
          // Received a complete data byte (but not yet the ack)
          if( i2c.addr==SLAVE_ADDR ) {
            SDA_LOW(); // Pull down to ACK the ADDR
            if( i2c.bytecnt==0 ) {
              // First data byte goes to CRA ...
              slave_cra=i2c.data;
            } else {
              // ... all other data bytes go to the register the CRA points to and the CRA is stepped ("auto increment")
              if( 0<=slave_cra && slave_cra<SLAVE_REGCOUNT ) slave_regs[slave_cra]= i2c.data;
              slave_cra++;
            }
          } else if( i2c.addr==SLAVE_ADDR+1 ) { 
            SDA_HIGH();
          }
          if( i2c.logbits ) log_char('/');
          log_byte(i2c.data);
          i2c.state= I2CSTATE_DATAACK_SCLLO; 
        }
        if( i2c.pulse==SLAVE_REG_PULSE() && SLAVE_REG_ENABLE()>0 ) { SCL_LOW(); log_char('_'); time_wait_us(SLAVE_REG_US()); SCL_HIGH(); }
      } 
      break;
      
    case I2CSTATE_DATAACK_SCLLO:
      // Reading data ack bit. SCL is low.
      if( scl==1 ) {
        // SCL went high, so SDA has data (if it stays like this the full CLK period)
        i2c.sdacap= sda;
        i2c.state= I2CSTATE_DATAACK_SCLHI;
      }
      break;
      
    case I2CSTATE_DATAACK_SCLHI:
      // Reading data ack bit. SCL is high.
      if( scl==0 ) {
        // SCL went low, so we have a complete data ack bit
        i2c.pulse++; 
        if( i2c.addr==SLAVE_ADDR ) {
          SDA_HIGH(); // Release ACK of DATA
        } else if( i2c.sdacap ) {
          SDA_HIGH(); // Release ACK of DATA, and do not push out data bit
        } else if( i2c.addr==SLAVE_ADDR+1 ) { // We need to release ACK of DATA, but also push out the first data bit of the next byte
          slave_cra++;
          uint8_t val= (0<=slave_cra && slave_cra<SLAVE_REGCOUNT) ? slave_regs[slave_cra] : 0x55;
          int mask= 1<<7;
          if( val&mask ) SDA_HIGH(); else SDA_LOW();
        }
        if( i2c.sdacap ) log_char('n'); else log_char('a');
        log_char(' '); // Space after data byte
        if( i2c.pulse==SLAVE_REG_PULSE() && SLAVE_REG_ENABLE()>0 ) { SCL_LOW(); log_char('_'); time_wait_us(SLAVE_REG_US()); SCL_HIGH(); }
        i2c.bitcnt= 0;
        i2c.logbits= i2c.pulse<SLAVE_REG_PULSE() && SLAVE_REG_PULSE()<i2c.pulse+8 && SLAVE_REG_ENABLE()>0;
        i2c.data= 0;
        i2c.bytecnt++;
        i2c.state= I2CSTATE_DATABIT_SCLLO; 
      } 
      break;
      
    default:
      log_byte(i2c.state);
      log_char('x'); // Unknown state
      break;
  }

}


// Monitor the interrupt latency (SCL)
void i2c_scl_isr() {
  SCLMON_HIGH();
  i2c_isr();
  SCLMON_LOW();
}


// Monitor the interrupt latency (SDA)
void i2c_sda_isr() {
  SDAMON_HIGH();
  i2c_isr();
  SDAMON_LOW();
}


// Reset the I2C interface
void i2c_reset() {
  log_char('@'); // Log reset
  // Release I2C lines
  SCL_HIGH();
  SDA_HIGH();
  delay(1);
  // Reset state machine
  if( SCL_READ()==1 && SDA_READ()==1 ) {
    i2c.state= I2CSTATE_IDLE; 
  } else {
    i2c.state= I2CSTATE_ERROR;
  }
}


// Check if the I2C interface is too long in the same state, if so, reset it
void i2c_watchdog() {
  static int laststate;
  static uint32_t lastupdate= micros();
  if( laststate==i2c.state && i2c.state>I2CSTATE_IDLE ) {
    // At 100Hz, there is an event every 5us
    if( micros()-lastupdate > 50000 ) {
      i2c_reset();
      // In case of comms problems, decrease ENABLE (because comms problems might be caused by being enabled)
      if( SLAVE_REG_ENABLE()>0 && SLAVE_REG_ENABLE()<0xFFFF ) SLAVE_REG_SET_ENABLE( SLAVE_REG_ENABLE()-1 );
    }
  } else {
    laststate= i2c.state;
    lastupdate= micros();
  }
}


// Reset I2C interface and clear log
void i2c_init() {
  i2c_reset();
  // Somehow the following line ensures the first few interrupts are not missed (filling cache?)
  for( i2c.state=I2CSTATE_UNKNOWN; i2c.state<I2CSTATE_DATAACK_SCLHI; i2c.state++ ) { i2c_isr(); delay(1); }
  i2c_reset();
  while( log_get() ) /*skip*/ ;
}


// ===========================================================================================
// ESP8266 configuration


// Configures WiFi off and CPU speed 160MHz
extern "C" {
#include <user_interface.h>
}
#include "ESP8266WiFi.h"
#define ESP8266_WIFI   0
#define ESP8266_CPU    160
void esp8266_init(void) {
  #if ESP8266_WIFI==0
    // Turn off ESP8266 radio
    WiFi.forceSleepBegin();
    delay(1);       
  #endif
  // Set CPU clock frequency
  system_update_cpu_freq(ESP8266_CPU);
  // Feedback
  Serial.printf("ESP8266 at %dMHz, WiFi %s\n", ESP8266_CPU, ESP8266_WIFI?"on":"off");
}


// ===========================================================================================
// The main program


void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.printf("\n\n\nWelcome to I2C-tool v%d\n",VERSION);
  esp8266_init();
  time_init();
  Serial.printf("\n");

  Serial.printf("HELP:\n");
  Serial.printf("- This device implements an I2C spy (echos all traffic to UART)\n");
  Serial.printf("- It also implements configurable clock stretch (can stretch any clock pulse)\n");
  Serial.printf("- Finally it implements an I2C loop-back service (write bytes, read them back)\n");
  Serial.printf("- Hardware: SCL on pin %d, SDA on pin %d\n",scl_pin, sda_pin);
  Serial.printf("- Warning: cannot keep up with higher I2C clock, use 50kHz\n");
  Serial.printf("  On a 160MHz ESP8266, interrupt latency is ~4.3us. At 100kHz, interrupts come every 5us\n");
  Serial.printf("- Slave address is 0x%02x (write) and 0x%02x (read)\n", SLAVE_ADDR, SLAVE_ADDR+1);
  Serial.printf("- Register overview\n");
  Serial.printf("  REGISTER ACCESS MSB/LSB DESCRIPTION\n");
  Serial.printf(" [CRA      write  --      Current Register Address]\n");
  Serial.printf("  ENABLE   w/r    00/01   Number of transactions clock stretch is enabled\n");
  Serial.printf("  PULSE    w/r    02/03   The CLK pulse that is stretched (starts with 1)\n");
  Serial.printf("  US       w/r    04/05   Clock stretch time in us\n");
  Serial.printf("  QPULSE   r      06/07   Query: Number of CLK pulses in last transaction\n");
  Serial.printf("  QUS      r      08-0B   Query: Number of us of the last transaction\n");
  Serial.printf("  RSVD     w/r    0C-0F   Reserved\n");
  Serial.printf("  MSG      w/r    10-1F   Buffer for loop-back message\n");
  Serial.printf("\n");

  pinMode(scl_pin, INPUT_PULLUP);
  pinMode(sda_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(scl_pin), i2c_scl_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(sda_pin), i2c_sda_isr, CHANGE);

  pinMode(sclmon_pin, INPUT_PULLUP);
  pinMode(sdamon_pin, INPUT_PULLUP);
  SCLMON_LOW();
  SDAMON_LOW();

  Serial,printf("Waiting for i2C traffic...\n\n");
  i2c_init();
}


char buf_data[LOG_SIZE+2]; // one more for terminating '\0'

void loop() {
  char ch= log_get();
  if( ch!=0 ) {
    // There was a transaction, get the log
    int ix= 0;
    while( ch!=0 && ch!=']' && ix<LOG_SIZE ) {
      buf_data[ix++]= ch;
      ch= log_get();
      int timeout=50; while( ch==0 && timeout>0 ) { delay(1); timeout--; ch=log_get(); }
    }
    buf_data[ix++]= ch;
    buf_data[ix++]= '\0';
    // Print the log    
    Serial.printf("i2c: %s\n", buf_data);
  }
  
  // Use hash to determine if registers are changed - if so, print them.
  // This reduces serial output in spy mode.
  static int last_hash= -1;
  int hash= slave_hash();
  if( last_hash!=hash ) {
    Serial.printf("reg: CRA=%x ENABLE=%04x PULSE=%04x US=%04x QPULSE=%04x QUS=%08x (%.2f kHz) RSVD=%08x\n", 
      slave_cra, SLAVE_REG_ENABLE(), SLAVE_REG_PULSE(), SLAVE_REG_US(), SLAVE_REG_QPULSE(), SLAVE_REG_QUS(), 
      (SLAVE_REG_QPULSE()+0.5)*1000.0/SLAVE_REG_QUS(), // Count 1/2 clock pulse for initial start
      SLAVE_REG_RSVD() 
    );  
    Serial.printf("reg: MSG="); for( int i=16; i<SLAVE_REGCOUNT; i++ ) Serial.printf(" %02X", slave_regs[i]); Serial.printf("\n");
    Serial.printf("\n");
    last_hash=hash;
  }

  i2c_watchdog();
}
