

/* Jim Sosnin (VK3JST)
   
   V1.1 20220709: Invert logic of sensing the radio's VFO enable (DC supply), now via an inverting buffer,
   (NPN transistor, with collector to pinVfoDC (D2) pin, which now has internal pullup enabled).
 
   V1.0 20211214: Original
   For external DDS-based VFO for Yaesu FT-101 or similar vintage radio.
   Some timer/interrupt ideas based on example code by Nick Gammon, except interrupts stay on now.
   (Gammon example had enable/disable interrupts for each gate period.  Gate period also different.)
*/ 

/* Counting and Gating scheme (VK3JST), using Arduino Timer 1 (counts f-in) and Timer 2 (gating):

   T1 (16-bit) set to external input, for f-in.  Intrupt on overflow, to count oFlows. (oFlow every ~13mS at 5MHz in)
   T2 (8-bit) set to count CPU clock (16MHz) via prescaler (1024), so ticks at 15625 Hz.
   T2 also set to intrupt on oFlow, to count oFlows, which happen every (256 / 15625) = 16.384 mS.
   Consider T2 oFlows as 'Gate Segments'.  Several are needed for typical gate duration of freq counter.
   For a freq resolution of, say, 10 Hz, we want a gate duration of around 100 mS, so we need to count 6 x T2 oFlows.
   This sets a gate time of 6 x 16.384 = 98.304 mS, which is close enough to 100 mS for our desired freq resolution.
   But to derive the actual freq in Hz, for the DDS setting, we use the more exact value of (1000 / 98.304) gates/sec:

   f(Hz) = gatedCount x (1000 / 98.304)

   A further. slight adjustment needs to be applied to the (1000 / 98.304) value before it can be used.
   The freqs of the xtals clocking the DDS chip and the arduino processor won't be exactly at their specified freqs,
   so the above value is adjusted by a calibration ratio.
   
   Self-calibration code down at the end. Embedded comments summarise how it works.
   Serial.prints instruct user if required, but the calibration can be done standalone (without computer connected). 
*/

#include <EEPROM.h>

#define pinVfoDC 2  // sense when Yaesu VFO is enabled (+5V -> 0V, via o'collector inverter from Yaesu 0V -> +6V)
#define pinSigIn 5  // Sig (from Yaesu VFO) whose freq to be tracked.  Connect ONLY via D5 (Timer1 external clock input)

#define pinDdsReset 3     //dds control and data pins
#define pinDdsData 4
#define pinDdsLoad 6
#define pinDdsClock 7

#define pinLockLed 8      //Status LEDs
#define pinTrackingLed 9

#define pinBtns A0        //one analog pin to sense several buttons via resistive divider

const int EE_ADDRESS = 0;      //EEPROM address to start reading/writing from

const long F_MIN = 4950000L;  //set limits, with 50kHz margins, to track FT-101 internal VFO (nominally 5.0 - 5.5MHz)
const long F_MAX = 5550000L;
const long F_MID = (F_MAX + F_MIN) / 2;

const double GATE_SEG_DUR = 0.016384; //see intro comments
const int SEGS_PER_GATE = 6;          //for gate duration of approx 100 mS, see intro comments
const double GATE_DUR = GATE_SEG_DUR * SEGS_PER_GATE;

volatile int vGateSegCountdown = SEGS_PER_GATE;
volatile byte vCtr1OvrFlows = 0;
volatile byte vCtr1OvrFlowsCopy = 0;
volatile uint16_t vCtr1Copy = 0;          //Final ticks in current gate period, counted after last o'flow

double scaleFactor = 1.0 / GATE_DUR;  //to derive f(Hz) from gated count (but recalculated before use, with calibRatio from EEPROM)
uint32_t fDDS = F_MID;                //fDDS, used to set the dds freq, is updated either via gated count or via user up/down btns

//In place of these 2 bools, we could just read the corresponding status LED pin bits, but these vars make code more readable
bool trackMode = false;    //Hi: tracking the VFO. Lo: 'hold' mode. Not the same as lockMode, ref comments in CheckCtrlPins()
bool lockMode = false;     //Hi: fDDS locked on previously stored fVFO, regardless of whether Int or Ext selected VFO on FT-101

const int NO_BUTS_THRESH = 853;   //these consts (halfway between the R-divider values) to determine which analog btn pressed
const int BTN_UP_THRESH = 511;
const int BTN_LOCK_THRESH = 171;
const int VFO_DC_THRESH = 511;

void CheckCtrlPins() {
  static bool oldLockBtnPressed = false;    //for toggle-check (needed only for lock btn, as we want repeat action on the others)

  trackMode = !digitalRead(pinVfoDC);       //Note for V1.1: inverted now, sensing via an inverting buffer
  digitalWrite(pinTrackingLed, trackMode);   //Indicator LED: Hi: tracking (FT-101 has 'VFO' btn pressed, user is tuning manually...
  //...Lo: hold mode (FT-101 has 'Ext' btn pressed, to use the DDS VFO, which will now be F-held/locked, apart from up/dpwn nudge btns)

  int valA = analogRead(pinBtns);

  if (valA > NO_BUTS_THRESH)
    oldLockBtnPressed = false;        //Reset for next time thru


  else if (valA < BTN_LOCK_THRESH) {     //means user is pressing the Lock btn to toggle the Lock mode...
    if (!oldLockBtnPressed) {           //....but toggle only if btn has been released since previous press
      lockMode = !lockMode;             //when lockMode true, the DDS retains previous freq, even during trackMode
      digitalWrite(pinLockLed, lockMode);
      oldLockBtnPressed = true;         //set for next time thru
    }
  }

  else if (!trackMode) {
    if (valA < BTN_UP_THRESH)
      fDDS += 5L;                 //Nudge down (note, tuning direction inverted, due to Yaesu LO mixer architecture)
    else
      fDDS -= 5L;                 //Nudge up

    ddsSetFreq(fDDS);
  }
}

void setupPins() {
  pinMode(pinVfoDC, INPUT_PULLUP); //Note for V1.1: internal pullup enabled now (collector load for inverting buffer).
  pinMode(pinSigIn, INPUT);        //driven by active source, so internal pullup not enabled
  pinMode(pinLockLed, OUTPUT);
  pinMode(pinTrackingLed, OUTPUT);

  pinMode(pinDdsLoad, OUTPUT);
  pinMode(pinDdsClock, OUTPUT);
  pinMode(pinDdsData, OUTPUT);
  pinMode(pinDdsReset, OUTPUT);

  pinMode(pinBtns, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);     // useful during debugging

  digitalWrite(pinTrackingLed, trackMode);
  digitalWrite(pinLockLed, lockMode);
}

/* Timer and intrupt control/status registers, also some individual bits:
   
   (Ref ATmega328P datasheet pages 74-134 for more details, and bit settings used here)

   TCCR1A, B, & TCCR2A, B: Timer counter control (each AB pair make a 16-bit reg.  Code can access them as such)
   TIMSK1, TIMSK2: Timer intrupt mask reg.  To set for intrupt on o'flow, set TOIE1, TOIE2
   TCNT1, 2: The timer counters.
*/

void setupTimers() {
  TCCR1A = 0;       // Reset Timers T1 and T2
  TCCR1B = 0;
  TCCR2A = 0;
  TCCR2B = 0;

  TCNT1 = 0;      // Clear both T1 and T2 counters
  TCNT2 = 0;

  TCCR1B =  (1 << CS10) | (1 << CS11) | (1 << CS12);  // T1 extern source (pin D5), clock on rising edge.
  TCCR2B =  (1 << CS22) | (1 << CS21) | (1 << CS20) ; // T2 intern src (CPU clock), via 1024 prescaler

  TIMSK1 = 1 << TOIE1;                                // Enable intrupts on T1 amd T2 counters' o'Flows
  TIMSK2 = 1 << TOIE2;
}

ISR (TIMER1_OVF_vect)
{
  ++vCtr1OvrFlows;               // Keep track of Counter1 overflows
}

ISR (TIMER2_OVF_vect)
{
  if (--vGateSegCountdown == 0) {       // Reached gate timeout yet?
    vCtr1Copy = TCNT1;                  // Snapshot of new counts since last T1 ctr o'flow.
    TCNT1 = 0;                          // Reset T1 ctr for next time
    vCtr1OvrFlowsCopy = vCtr1OvrFlows;  // Snapshot this also
    vCtr1OvrFlows = 0;
  }
}

uint32_t getGatedCount() {
  do {
  } while (vGateSegCountdown > 0);

  vGateSegCountdown = SEGS_PER_GATE;   // sets gate duration each time
  
  uint32_t gCount = vCtr1OvrFlowsCopy;          //now in 32 bits, allowing left shift...
  gCount = (gCount << 16) + vCtr1Copy;          //Final value
  
  return(gCount);
}

void setup() {
  setupPins();
  Serial.begin(9600);   //Serial monitor used mainly during code devel only, and to show warnings and status, where useful
  setupDds();
  ddsSetFreq(F_MID);
  setupTimers();
  
  //Remind user how to calibrate, but only if user has enabled serial monitor.  (All the calib code runs OK without it though)
  Serial.println("To calibrate, set calib jumper, then press arduino reset btn while holding either nudge btn pressed");

  if ((analogRead(pinBtns)) < NO_BUTS_THRESH)
    doCalib();
  
  getStoredCalib();
}

void loop() {
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  //Toggles on-board LED (approx 5 flashes/sec, if we're looping correctly)
 
  uint32_t gatedCount = getGatedCount();

  CheckCtrlPins();     //Updates Track/Hold/Lock modes.  Also, if not tracking, allows user to nudge fDDS up/down if required

  if (trackMode  && !lockMode) {
    fDDS = gatedCount * scaleFactor;
    ddsSetFreq(fDDS);
  }
}

// ---------------------------------------------------------------------------------------------------------------

//   Following code to control AD9850/AD9851 from Andrew Smallbone
//   Adapted with some changes by Jim Sosnin VK3JST 11/01/2022

/*
   A simple single freq AD9850 Arduino test script
   Original AD9851 DDS sketch by Andrew Smallbone at www.rocketnumbernine.com
   Modified for testing the inexpensive AD9850 ebay DDS modules
   Pictures and pinouts at nr8o.dhlpilotcentral.com
   9850 datasheet at http://www.analog.com/static/imported-files/data_sheets/AD9850.pdf
   9851 datasheet at http://www.analog.com/static/imported-files/data_sheets/AD9851.pdf
   Use freely
*/

#define TWO_POW_32 4294967295.0

#define pulseHigh(pin) {digitalWrite(pin, HIGH); digitalWrite(pin, LOW); }

// transfer a byte, a bit at a time, LSB first to the 9850/9851 via serial pinDdsData line
void tfr_byte(byte dataByte)
{
  for (int i = 0; i < 8; i++, dataByte >>= 1) {
    digitalWrite(pinDdsData, dataByte & 0x01);
    pulseHigh(pinDdsClock);   //after each bit sent, DdsClock is pulsed high
  }
}

// frequency calc from datasheet p8 (AD9850) or p12 (AD9851): <sys clock> * <frequency tuning word>/2^32.
// Uncomment relevant TUNING_FACTOR below. and relevant multiplier ctrl several lines further down:

#define TUNING_FACTOR TWO_POW_32 / 125000000.0  // note: this one for 125 MHz clock on AD9850
//#define TUNING_FACTOR TWO_POW_32 / 30000000.0   // note: this one for 30 MHz clock on AD9851
//#define TUNING_FACTOR TWO_POW_32 / 180000000.0  // note: this one for 6 * 30 MHz clock on AD9851

void ddsSetFreq(uint32_t fDDS) {
  if ((fDDS > F_MIN) && (fDDS < F_MAX)) {
    uint32_t tuningWord = fDDS * TUNING_FACTOR;
    
    for (int b = 0; b < 4; b++, tuningWord >>= 8) {
      tfr_byte(tuningWord & 0xFF);
    }
  
    // Uncomment one of the 2 following lines (See p16 of AD9851 datasheet re x6 clock multiplier control bit)
  
    tfr_byte(0x000);   // For AD9850: final control byte, all zeros (Also use for AD9851 at 30 MHz)
    //  tfr_byte(0x001);   // For AD9851: final control byte, LSB=1 to enable x6  clock multiplier, for 180 mHz
  
    pulseHigh(pinDdsLoad);  // Done!  Should see output
  }
  
  else {
    Serial.print("----- Warning, fDDS = ");
    Serial.print(fDDS);
    Serial.println(", out of range for Yaesu VFO.  Not setting -----");
  }
}

void setupDds() {
  pulseHigh(pinDdsReset); //Is this needed?  Hard-wired for serial data transfer, so should work OK with Reset pin simply grounded.
  pulseHigh(pinDdsClock);
  pulseHigh(pinDdsLoad);  //This pulse enables serial mode - AD9850 Datasheet p12 fig 10 (or AD9851 p15 fig 17)
}

// ---------------------------------------------------------------------------------------------------------------

// --------------- Calibration ---------------

void doCalib() {    //we arrive here only if user has held a front panel button pressed during startup (ie reset or power cycle)
  double calRatio = 1.0;
  const int NUM_ACCUM_LOOPS = 10;        //for averaging
  uint32_t countAccum = 0;

  Serial.println("Calibrating...");
  ddsSetFreq(F_MID);

  for (int j = 0; j < 8; j++) {        //Repeat outer loop few times to allow clocks to settle, in case just powered up

    countAccum = 0;
    
    for (int i = 0; i < NUM_ACCUM_LOOPS; i++) {          //Inner loop to accumulate a few readings to obtain average value
      digitalWrite(pinLockLed, !digitalRead(pinLockLed));  //User indication: toggles Lock LED (approx 5 flashes/sec), during calib
      countAccum += getGatedCount();
    }
    
    double fAv = (countAccum * scaleFactor) / NUM_ACCUM_LOOPS;    //We get here from setup(), so scaleFactor still as initialised
    calRatio = (double)F_MID / fAv;   
    
    Serial.print("fDDS = ");
    Serial.print(fDDS);
    Serial.print(", fMeasured = ");
    Serial.print(fAv);
    Serial.print(", calRatio = ");
    Serial.println(calRatio, 7);      
  }
   
  if ((calRatio > 0.95) && (calRatio < 1.05)) {          //check it's not too far off unity
    EEPROM.put(EE_ADDRESS, calRatio);
    lockMode = true;                              //Lock to prevent DDS tracking itself (as the jumper is still on the 'cal' link) 
    digitalWrite(pinLockLed, lockMode);           //And the LED also indicates to user that calib finished

    Serial.println("Writing calRatio to EEPROM...");
    Serial.println("Set calib jumper back to normal position");
  }
  else
    Serial.print("calRatio bad value.  Not writing to EEPROM.");
}

void getStoredCalib() {    //Called from setup(), so we do this at each startup.     
  double calRatio = 1.0;
  EEPROM.get(EE_ADDRESS, calRatio);
  
  if ((calRatio > 0.95) && (calRatio < 1.05)) {          //to do: further error checks in case garbage read from EEPROM
    scaleFactor = calRatio / GATE_DUR;                  //adjust scaleFactor for subsequent use in ddsSetFreq()

    Serial.print("Read calRatio from EEPROM: ");
    Serial.println(calRatio, 7);
    Serial.println("======= Calibrated =======");
  }
  else
    Serial.print("Unable to read calRatio from EEPROM.  Not calibrated yet.");
}
