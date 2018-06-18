/*
A: B01110111
B: B01111100
C: B00111001
D: B01011110
E: B01111001
F: B01110001
G: B01101111
H: B01110110
I: B00110000
J: B00011110
K: B01110110
L: B00111000
M: B00010101
N: B01010100
O: B00111111
P: B01110011
Q: B01100111
R: B01010000
S: B01101101
T: B01111000
U: B00111110
V: B00011100
W: B00101010
X: B01110110
Y: B01101110
Z: B01011011
*/

#define USE_SOFTWARE_SERIAL
//#undef USE_SOFTWARE_SERIAL

#include <TM1637.h>
#include <Pushbutton.h>
#include <EEPROM.h>
#ifdef USE_SOFTWARE_SERIAL
#include <SoftwareSerial.h>
#define SERIAL_OBJECT hc06
#else
#define SERIAL_OBJECT Serial
#endif
#include <Cmd.h>

//#define TESTING
#define MULTIMODE_BUTTON
//#define EEPROM_INITIALIZATION

#define EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADDRESS 0
#define EEPROM_VOLTAGE_PROTECTION_ALERT_ADDRESS 4
#define EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADDRESS 8
#define EEPROM_FIREMODE_1_CYCLE_ADDRESS 9
#define EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADDRESS 10
#define EEPROM_FIREMODE_2_CYCLE_ADDRESS 11
#define EEPROM_PRECOCK_AFFINE_OFFSET_MS_ADDRESS 12
#define EEPROM_PRECOK_AFFINE_SLOPE_ADDRESS 16

#define STATE_IDLE_HOT 0
#define STATE_IDLE_SAFE 1
#define STATE_DISPLAYING_VOLTAGE 2
#define STATE_FIRING 3

#define CURRENT_FIREMODE_1 1
#define CURRENT_FIREMODE_2 2
#ifdef MULTIMODE_BUTTON
#define CURRENT_FIREMODE_SAFE 3
#endif

#define DISPLAYMODE_FIREMODE 0
#define DISPLAYMODE_VOLTAGE 1

#define FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE 0
#define FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE 1
#define FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE_AND_KEEP_ON_PRESS 2

#define TRIGGER_BUTTON_PIN 2
#define TAPPET_PLATE_SENSOR_PIN 3

#ifdef MULTIMODE_BUTTON
#define MULTIMODE_BUTTON_PIN 5
#else
#define SAFETY_SENSOR_PIN
#define MODE_1_SENSOR_PIN
#define MODE_2_SENSOR_PIN
#endif

#define DISPLAYMODE_BUTTON_PIN 9
#define DISPLAY_DIO_PIN 6
#define DISPLAY_CLK_PIN 7
#define BLUETOOTH_RX_PIN 11
#define BLUETOOTH_TX_PIN 12

#define MOTOR_PIN 10
#define VOLTAGE_SENSOR_PIN A0

#define VOLTAGE_CORRECTION_FACTOR 1.0
#define DISPLAY_REFRESH_TIME_MILLISECONDS 100
#define DISPLAY_GLOBAL_DEBOUNCE_TIMER 300
#define PRECOCK_DELAY 40

uint8_t state;
uint8_t displaymode;
boolean eventTestingMode;
uint8_t currentFiremode;
uint8_t *currentFiremodeRoundsNumberConfig;
uint8_t *currentFiremodeCycleConfig;
uint8_t shotsLeft;
boolean fullAuto;
boolean cocked;
uint8_t precockAffineOffsetMs;

TM1637 display(DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);

unsigned long timeOnLastDisplay;

Pushbutton triggerButton(TRIGGER_BUTTON_PIN);
Pushbutton displaymodeButton(DISPLAYMODE_BUTTON_PIN);

#ifdef MULTIMODE_BUTTON
Pushbutton multimodeButton(MULTIMODE_BUTTON_PIN);
#else
Pushbutton mode1SensorButton(MODE_1_SENSOR_PIN);
Pushbutton mode2SensorButton(MODE_2_SENSOR_PIN);
Pushbutton safetySensorButton(SAFETY_SENSOR_PIN);
#endif
Pushbutton tappetPlateSensorButton(TAPPET_PLATE_SENSOR_PIN, PULL_UP_ENABLED, DEFAULT_STATE_LOW);

//config variables
float voltageProtectionAlertConfig;
float voltageProtectionCutoffConfig;
uint8_t fireMode1RoundsNumberConfig;
uint8_t fireMode2RoundsNumberConfig;
uint8_t fireMode1CycleConfig;
uint8_t fireMode2CycleConfig;

// ENTER STATES
void enterSTATE_IDLE_HOT()
{
#ifdef TESTING
  Serial.println("STATE_IDLE_HOT");
#endif

  state = STATE_IDLE_HOT;
}

void enterSTATE_IDLE_SAFE()
{
#ifdef TESTING
  Serial.println("STATE_IDLE_SAFE");
#endif

  if (triggerButton.isPressed()) //uncock the piston
  {
    uncock();
  }

  state = STATE_IDLE_SAFE;

#ifdef MULTIMODE_BUTTON
  currentFiremode = CURRENT_FIREMODE_SAFE;
#endif

  displayFiremodeIf();
}

void enterSTATE_FIRING()
{
  shotsLeft = *currentFiremodeRoundsNumberConfig;

  if (shotsLeft > 0 || fullAuto)
  {
#ifdef TESTING
    Serial.println("STATE_FIRING");
#endif

    digitalWrite(MOTOR_PIN, HIGH);
    state = STATE_FIRING;
  }
}

void enterSTATE_DISPLAYING_VOLTAGE()
{
#ifdef TESTING
  Serial.println("STATE_DISPLAYING_VOLTAGE");
#endif

  displayVoltage();
  state = STATE_DISPLAYING_VOLTAGE;
}
// END ENTER STATES

#ifdef USE_SOFTWARE_SERIAL
SoftwareSerial hc06(BLUETOOTH_RX_PIN, BLUETOOTH_TX_PIN);
#endif

void setup()
{
#ifdef USE_SOFTWARE_SERIAL
  cmdInit(9600, &hc06);
#else
  cmdInit(9600);
#endif

  cmdAdd("cfg", commandSetConfig);
  cmdAdd("testing", commandTestingMode);
  cmdAdd("uncock", commandUncock);

// START EEPROM INITILIZATION
#ifdef EEPROM_INITIALIZATION
#warning "Warning: EEPROM_INITIALIZATION is defined, arduino will overwrite default config on each startup, this is used to store reasonable default config values, it should be used only once and then recompiled without !"

  float defaultVoltageProtectionAlertConfig = 3.5f;
  float defaultVoltageProtectionCutoffConfig = 3.0f;
  uint8_t defaultFiremode1RoundsNumberConfig = 1;
  uint8_t defaultFiremode1CycleConfig = FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE;

  uint8_t defaultFiremode2RoundsNumberConfig = 0;
  uint8_t defaultFiremode2CycleConfig = FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE; //no effect in full auto

  uint8_t defaultPrecockAffineOffsetMs = 35;

  EEPROM.put(EEPROM_VOLTAGE_PROTECTION_ALERT_ADDRESS, defaultVoltageProtectionAlertConfig);
  EEPROM.put(EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADDRESS, defaultVoltageProtectionCutoffConfig);
  EEPROM.put(EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADDRESS, defaultFiremode1RoundsNumberConfig);
  EEPROM.put(EEPROM_FIREMODE_1_CYCLE_ADDRESS, defaultFiremode1CycleConfig);
  EEPROM.put(EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADDRESS, defaultFiremode2RoundsNumberConfig);
  EEPROM.put(EEPROM_FIREMODE_2_CYCLE_ADDRESS, defaultFiremode2CycleConfig);
  EEPROM.put(EEPROM_PRECOCK_AFFINE_OFFSET_MS_ADDRESS, defaultPrecockAffineOffsetMs);
#endif
  // END EEPROM INITIALIZATION

  pinMode(MOTOR_PIN, OUTPUT);
  display.init();
  display.set(BRIGHTEST);
  timeOnLastDisplay = millis();
  cocked = false;

  // read settings
  EEPROM.get(EEPROM_VOLTAGE_PROTECTION_ALERT_ADDRESS, voltageProtectionAlertConfig);
  EEPROM.get(EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADDRESS, voltageProtectionCutoffConfig);
  EEPROM.get(EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADDRESS, fireMode1RoundsNumberConfig);
  EEPROM.get(EEPROM_FIREMODE_1_CYCLE_ADDRESS, fireMode1CycleConfig);
  EEPROM.get(EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADDRESS, fireMode2RoundsNumberConfig);
  EEPROM.get(EEPROM_FIREMODE_2_CYCLE_ADDRESS, fireMode2CycleConfig);
  EEPROM.get(EEPROM_PRECOCK_AFFINE_OFFSET_MS_ADDRESS, precockAffineOffsetMs);

#ifdef TESTING
  Serial.println("niveau protection alerte batterie :");
  Serial.println(voltageProtectionAlertConfig);

  Serial.println("niveau protection coupure batterie :");
  Serial.println(voltageProtectionCutoffConfig);

  Serial.println("mode de tir 1 (roundsNumber, cycleConfig) :");
  Serial.println(fireMode1RoundsNumberConfig);
  Serial.println(fireMode1CycleConfig);

  Serial.println("mode de tir 2 (roundsNumber, cycleConfig) :");
  Serial.println(fireMode2RoundsNumberConfig);
  Serial.println(fireMode2CycleConfig);
#endif
  // read settings

  enterSTATE_IDLE_SAFE();
}

void loop()
{

  displayVoltageIf();

  switch (state)
  {

  case STATE_IDLE_HOT:
  case STATE_IDLE_SAFE:
    cmdPoll();

    if (displaymodeButton.getSingleDebouncedPress())
    {
      switch (displaymode)
      {
      case DISPLAYMODE_FIREMODE:
        displaymode = DISPLAYMODE_VOLTAGE;
        break;
      case DISPLAYMODE_VOLTAGE:
        displaymode = DISPLAYMODE_FIREMODE;
        displayFiremode();
        break;
      }
    }

    switch (state)
    {
    case STATE_IDLE_HOT:
      // behavior

#ifdef MULTIMODE_BUTTON
      if (currentFiremode == CURRENT_FIREMODE_SAFE && currentFiremode != CURRENT_FIREMODE_2)
#else
      if (mode1SensorButton.isPressed() && currentFiremode != CURRENT_FIREMODE_1)
#endif
      {
        currentFiremodeRoundsNumberConfig = &fireMode1RoundsNumberConfig;
        currentFiremodeCycleConfig = &fireMode1CycleConfig;
        currentFiremode = CURRENT_FIREMODE_1;

        displayFiremode();
#ifdef TESTING
        Serial.println("CURRENT_FIREMODE_1");
#endif
      }

#ifdef MULTIMODE_BUTTON
      if (multimodeButton.getSingleDebouncedPress() && currentFiremode == CURRENT_FIREMODE_1)
#else
      if (mode2SensorButton.isPressed() && currentFiremode != CURRENT_FIREMODE_2)
#endif
      {
        currentFiremodeRoundsNumberConfig = &fireMode2RoundsNumberConfig;
        currentFiremodeCycleConfig = &fireMode2CycleConfig;
        currentFiremode = CURRENT_FIREMODE_2;

        displayFiremodeIf();

#ifdef TESTING
        Serial.println("CURRENT_FIREMODE_2");
#endif
      }
// events
#ifdef MULTIMODE_BUTTON
      if (multimodeButton.getSingleDebouncedPress() && currentFiremode == CURRENT_FIREMODE_2)
#else
      if (safetySensorButton.isPressed())
#endif
      {
        enterSTATE_IDLE_SAFE();
      }
      if (triggerButton.getSingleDebouncedPress())
      {
        enterSTATE_FIRING();
      }
      break;

    case STATE_IDLE_SAFE:
// behavior
// events
#ifdef MULTIMODE_BUTTON
      if (multimodeButton.getSingleDebouncedPress())
#else
      if (!safetySensorButton.isPressed())
#endif
      {
        /*
        if (cocked == false && tappetPlateSensorButton.isPressed())
        {
          digitalWrite(MOTOR_PIN, HIGH);
          tappetPlateSensorButton.waitForRelease();
          tappetPlateSensorButton.waitForPress();
          delay(PRECOCK_DELAY);
          digitalWrite(MOTOR_PIN, LOW);
          cocked = true;
        }*/
        // TOO DANGEROUS
        enterSTATE_IDLE_HOT();
      }
      break;
    }

    break;

  case STATE_FIRING:
    if (tappetPlateSensorButton.getSingleDebouncedRelease()) // a bb has been fired and the sring is uncompressed
    {
      if (shotsLeft > 0)
      {
        shotsLeft -= 1;
      }

      cocked = false;
      //TODO : remove one BB from magazine

#ifdef MULTIMODE_BUTTON
      if (multimodeButton.isPressed() && currentFiremode == CURRENT_FIREMODE_2)
      {
        digitalWrite(MOTOR_PIN, LOW);
        enterSTATE_IDLE_SAFE();
      }
#else
      if (safetySensorButton.isPressed())
      {
        digitalWrite(MOTOR_PIN, LOW);
        enterSTATE_IDLE_SAFE();
      }
#endif
      else // we want to precock the piston
      {
        while (tappetPlateSensorButton.getSingleDebouncedPress())
        {
        }
        //tappetPlateSensorButton.waitForPress();
        cocked = true;
        if (
            !(
                (triggerButton.isPressed() && (fullAuto || (shotsLeft > 0 && *currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE) || *currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE_AND_KEEP_ON_PRESS)) ||
                (shotsLeft > 0 && (*currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE || *currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE_AND_KEEP_ON_PRESS))))
        {
          delay(precockAffineOffsetMs);
          digitalWrite(MOTOR_PIN, LOW);
          enterSTATE_IDLE_HOT();
        }
      }
    }
    // events
    break;

  default:
    break;
  }
}

void commandTestingMode(int nbArgs, char **args)
{
  if (nbArgs == 1)
  {
    SERIAL_OBJECT.print("voltage: ");
    SERIAL_OBJECT.println(getCurrentVoltage());
    
    SERIAL_OBJECT.print("precock delay: ");
    SERIAL_OBJECT.println(precockAffineOffsetMs);

    SERIAL_OBJECT.print("trigger button: ");
    SERIAL_OBJECT.println(triggerButton.isPressed());

    SERIAL_OBJECT.print("tappet plate sensor: ");
    SERIAL_OBJECT.println(tappetPlateSensorButton.isPressed());

#ifdef MULTIMODE_BUTTON
    SERIAL_OBJECT.print("multimode button: ");
    SERIAL_OBJECT.println(multimodeButton.isPressed());
#else
    SERIAL_OBJECT.print("safety sensor: ");
    SERIAL_OBJECT.println(safetySensorButton.isPressed());

    SERIAL_OBJECT.print("mode 1 Sensor: ");
    SERIAL_OBJECT.println(mode1SensorButton.isPressed());

    SERIAL_OBJECT.print("mode 2 Sensor: ");
    SERIAL_OBJECT.println(mode2SensorButton.isPressed());
#endif

    serialReturnSuccess();
  }
  else
  {
    serialReturnFailure();
  }
}

void commandSetConfig(int nbArgs, char **args)
{
  if (nbArgs == 3)
  {
    if (strcmp(args[1], "vpa") == 0)
    {
      voltageProtectionAlertConfig = atof(args[2]);
      EEPROM.put(EEPROM_VOLTAGE_PROTECTION_ALERT_ADDRESS, voltageProtectionAlertConfig);
      serialReturnSuccess();
    }
    else if (strcmp(args[1], "vpc") == 0)
    {
      voltageProtectionCutoffConfig = atof(args[2]);
      EEPROM.put(EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADDRESS, voltageProtectionCutoffConfig);
      serialReturnSuccess();
    }
    else if (strcmp(args[1], "fm1rn") == 0)
    {
      fireMode1RoundsNumberConfig = atoi(args[2]);
      EEPROM.put(EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADDRESS, fireMode1RoundsNumberConfig);
      serialReturnSuccess();
    }
    else if (strcmp(args[1], "fm2rn") == 0)
    {
      fireMode2RoundsNumberConfig = atoi(args[2]);
      EEPROM.put(EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADDRESS, fireMode2RoundsNumberConfig);
      serialReturnSuccess();
    }
    else if (strcmp(args[1], "fm1c") == 0)
    {
      fireMode1CycleConfig = atoi(args[2]);
      EEPROM.put(EEPROM_FIREMODE_1_CYCLE_ADDRESS, fireMode1CycleConfig);
      serialReturnSuccess();
    }
    else if (strcmp(args[1], "fm2c") == 0)
    {
      fireMode2CycleConfig = atoi(args[2]);
      EEPROM.put(EEPROM_FIREMODE_2_CYCLE_ADDRESS, fireMode2CycleConfig);
      serialReturnSuccess();
    }
    else if (strcmp(args[1], "pcdelay") == 0)
    {
      precockAffineOffsetMs = atoi(args[2]);
      EEPROM.put(EEPROM_PRECOCK_AFFINE_OFFSET_MS_ADDRESS, precockAffineOffsetMs);
      serialReturnSuccess();
    }
    else
    {
      serialReturnFailure();
    }
  }
  else
  {
    serialReturnFailure();
  }
}
void commandUncock(int nbArgs, char **args)
{
  if (nbArgs == 1)
  {
    uncock();
    serialReturnSuccess();
  }else{
    serialReturnFailure();
  }
}
void serialReturnFailure()
{
  SERIAL_OBJECT.println("KO");
}

void serialReturnSuccess()
{
  SERIAL_OBJECT.println("OK");
}

void displayVoltageIf()
{
  if (displaymode == DISPLAYMODE_VOLTAGE)
  {
    if (updateDisplay())
    {
      displayVoltage();
    }
  }
}
void displayVoltage()
{
  display.displayDecimal(getCurrentVoltage(), 2);
}

void displayFiremodeIf()
{

  if (displaymode == DISPLAYMODE_FIREMODE)
  {
    displayFiremode();
  }
}

void displayFiremode()
{
  switch (currentFiremode)
  {
  case CURRENT_FIREMODE_1:
    display.displayRaw((const uint8_t[]){B00010101, B00000000, B00110000, B00000000});
    break;
  case CURRENT_FIREMODE_2:
    display.displayRaw((const uint8_t[]){B00010101, B00000000, B01011011, B00000000});
    break;
  case CURRENT_FIREMODE_SAFE:
    display.displayRaw((const uint8_t[]){B01101101, B01110111, B01110001, B01111001});
    break;
  }
}

void uncock()
{
    digitalWrite(MOTOR_PIN, HIGH);
    tappetPlateSensorButton.waitForPress();
    tappetPlateSensorButton.waitForRelease();
    digitalWrite(MOTOR_PIN, LOW);
}

float getCurrentVoltage()
{
  return (((float)analogRead(VOLTAGE_SENSOR_PIN) / 1024.0) * 5) / (100.0 / (160.0 + 100.0)) * VOLTAGE_CORRECTION_FACTOR;
}

boolean updateDisplay()
{
  if (millis() - timeOnLastDisplay > DISPLAY_REFRESH_TIME_MILLISECONDS)
  {
    timeOnLastDisplay = millis();
    return true;
  }
  else
  {
    return false;
  }
}

/*


  triggerState = digitalRead(TRIGGER_BUTTON_PIN);
  delay(10); // Delay a little bit to improve simulation performance
  if(triggerState == FIRESTATE)
  {
    digitalWrite(motorPin, HIGH);
    display.displayRaw(0,B01110011);
    display.displayRaw(1,B01111011);
    display.displayRaw(2,B01011110);
    display.displayRaw(3,B01111011);
    display.displayDecimal(15.456,(uint8_t)2);
  } else {
    digitalWrite(motorPin, LOW);
  }
  Serial.println("triggerState");
  Serial.println(triggerState);
  Serial.println(B10000000);
  */
/*
  directSensorVoltage = (((float)analogRead(voltageSensorPin) / 1024.0) * 5);
    
  batteryVoltage = directSensorVoltage / (100.0/(160.0+100.0));
  
  Serial.println("------------------");
  Serial.println(analogRead(voltageSensorPin));
  Serial.println(directSensorVoltage,2);
  Serial.println(batteryVoltage,2);*/
//delay(50);
