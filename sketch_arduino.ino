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
#include <TM1637.h>
#include <Pushbutton.h>
#include <EEPROM.h>

#define TESTING
//#define EEPROM_INITIALIZATION

#define DISPLAY_CLK 12
#define DISPLAY_DIO 11

#define EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADRESS 0
#define EEPROM_VOLTAGE_PROTECTION_ALERT_ADRESS 4
#define EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADRESS 8
#define EEPROM_FIREMODE_1_CYCLE_ADRESS 9
#define EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADRESS 10
#define EEPROM_FIREMODE_2_CYCLE_ADRESS 11

#define STATE_IDLE_HOT 0
#define STATE_IDLE_SAFE 1
#define STATE_DISPLAYING_VOLTAGE 2
#define STATE_FIRING 3

#define CURRENT_FIREMODE_1 1
#define CURRENT_FIREMODE_2 2

#define FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE 0
#define FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE 1
#define FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE_AND_KEEP_ON_PRESS 2

#define TRIGGER_BUTTON_PIN 2
#define TAPPET_PLATE_SENSOR_PIN 3
#define SAFETY_SENSOR_PIN 5
#define MODE_1_SENSOR_PIN 6
#define MODE_2_SENSOR_PIN 7

#define MOTOR_PIN 10
#define VOLTAGE_SENSOR_PIN A0

#define VOLTAGE_CORRECTION_FACTOR 1.0
#define DISPLAY_REFRESH_TIME_MILLISECONDS 100
#define DISPLAY_GLOBAL_DEBOUNCE_TIMER 300

uint8_t state;
uint8_t currentFiremode;
uint8_t currentFiremodeRoundsNumberConfig;
uint8_t currentFiremodeCycleConfig;
uint8_t shotsLeft;
boolean fullAuto;
boolean cocked;

TM1637 display(DISPLAY_CLK, DISPLAY_DIO);

unsigned long timeOnLastDisplay;

Pushbutton triggerButton(TRIGGER_BUTTON_PIN);
Pushbutton mode1SensorButton(MODE_1_SENSOR_PIN);
Pushbutton mode2SensorButton(MODE_2_SENSOR_PIN);
Pushbutton safetySensorButton(SAFETY_SENSOR_PIN);
Pushbutton tappetPlateSensorButton(TAPPET_PLATE_SENSOR_PIN);

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

  display.displayRaw((const uint8_t[]){B01110110, B00111111, B01111000, B00000000});
  state = STATE_IDLE_HOT;
}

void enterSTATE_IDLE_SAFE()
{
#ifdef TESTING
  Serial.println("STATE_IDLE_SAFE");
#endif

  display.displayRaw((const uint8_t[]){B01101101, B01110111, B01110001, B01111001});
  state = STATE_IDLE_SAFE;
}

void enterSTATE_FIRING()
{
  shotsLeft = currentFiremodeRoundsNumberConfig;

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

void setup()
{
// START EEPROM INITILIZATION
#ifdef EEPROM_INITIALIZATION
#warning "Warning: EEPROM_INITIALIZATION is defined, arduino will overwrite default config on each startup, this is used to store reasonable default config values, it should be used only once and then recompiled without !"

  float defaultVoltageProtectionAlertConfig = 3.5f;
  float defaultVoltageProtectionCutoffConfig = 3.0f;
  uint8_t defaultFiremode1RoundsNumberConfig = 1;
  uint8_t defaultFiremode1CycleConfig = FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE;

  uint8_t defaultFiremode2RoundsNumberConfig = 0;
  uint8_t defaultFiremode2CycleConfig = FIREMODE_CYCLE_CONFIG_STOP_IMMEDIATELY_ON_RELEASE; //no effect in full auto

  EEPROM.put(EEPROM_VOLTAGE_PROTECTION_ALERT_ADRESS, defaultVoltageProtectionAlertConfig);
  EEPROM.put(EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADRESS, defaultVoltageProtectionCutoffConfig);
  EEPROM.put(EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADRESS, defaultFiremode1RoundsNumberConfig);
  EEPROM.put(EEPROM_FIREMODE_1_CYCLE_ADRESS, defaultFiremode1CycleConfig);
  EEPROM.put(EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADRESS, defaultFiremode2RoundsNumberConfig);
  EEPROM.put(EEPROM_FIREMODE_2_CYCLE_ADRESS, defaultFiremode2CycleConfig);
#endif
// END EEPROM INITIALIZATION
#ifdef TESTING
  Serial.begin(9600);
#endif

  pinMode(MOTOR_PIN, OUTPUT);
  display.init();
  display.set(BRIGHTEST);
  timeOnLastDisplay = millis();
  cocked = false;

  // read settings
  EEPROM.get(EEPROM_VOLTAGE_PROTECTION_ALERT_ADRESS, voltageProtectionAlertConfig);
  EEPROM.get(EEPROM_VOLTAGE_PROTECTION_CUTOFF_ADRESS, voltageProtectionCutoffConfig);
  EEPROM.get(EEPROM_FIREMODE_1_ROUNDS_NUMBER_ADRESS, fireMode1RoundsNumberConfig);
  EEPROM.get(EEPROM_FIREMODE_1_CYCLE_ADRESS, fireMode1CycleConfig);
  EEPROM.get(EEPROM_FIREMODE_2_ROUNDS_NUMBER_ADRESS, fireMode2RoundsNumberConfig);
  EEPROM.get(EEPROM_FIREMODE_2_CYCLE_ADRESS, fireMode2CycleConfig);
  // read settings

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

  enterSTATE_IDLE_HOT();
}

void loop()
{
  switch (state)
  {

  case STATE_IDLE_HOT:
  case STATE_IDLE_SAFE:

    switch (state)
    {
    case STATE_IDLE_HOT:
      // behavior
      if (mode1SensorButton.isPressed() && currentFiremode != CURRENT_FIREMODE_1)
      {
        currentFiremodeRoundsNumberConfig = fireMode1RoundsNumberConfig;
        currentFiremodeCycleConfig = fireMode1CycleConfig;
        currentFiremode = CURRENT_FIREMODE_1;

#ifdef TESTING
        Serial.println("CURRENT_FIREMODE_1");
#endif
      }
      if (mode2SensorButton.isPressed() && currentFiremode != CURRENT_FIREMODE_2)
      {
        currentFiremodeRoundsNumberConfig = fireMode2RoundsNumberConfig;
        currentFiremodeCycleConfig = fireMode2CycleConfig;
        currentFiremode = CURRENT_FIREMODE_2;

#ifdef TESTING
        Serial.println("CURRENT_FIREMODE_2");
#endif
      }
      // events
      if (safetySensorButton.isPressed()) // we want to uncompress the spring and leave firing
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
      if (!safetySensorButton.isPressed()) // we want to uncompress the spring and leave firing
      {
        if (cocked == false)
        {
          digitalWrite(MOTOR_PIN, HIGH);
          tappetPlateSensorButton.waitForPress();
          digitalWrite(MOTOR_PIN, LOW);
        }
        enterSTATE_IDLE_HOT();
      }
      break;
    }

    break;

  case STATE_FIRING:
    if (!tappetPlateSensorButton.isPressed()) // a bb has been fired and the sring is uncompressed
    {
      if (shotsLeft > 0)
      {
        shotsLeft -= 1;
      }

      cocked = false;
      //TODO : remove one BB from magazine

      if (safetySensorButton.isPressed()) // we want to uncompress the spring and leave firing
      {
        digitalWrite(MOTOR_PIN, LOW);
        enterSTATE_IDLE_SAFE();
      }
      else // we want to precock the piston
      {
        tappetPlateSensorButton.waitForPress();
        cocked = true;
        if (
            !(
                (triggerButton.isPressed() && (fullAuto || currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE_AND_KEEP_ON_PRESS)) ||
                (shotsLeft > 0 && (currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE || currentFiremodeCycleConfig == FIREMODE_CYCLE_CONFIG_CONTINUE_ON_RELEASE_UNTIL_END_OF_CYCLE_AND_KEEP_ON_PRESS))))
        {
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

void displayVoltage()
{
  display.displayDecimal((((float)analogRead(VOLTAGE_SENSOR_PIN) / 1024.0) * 5) / (100.0 / (160.0 + 100.0)) * VOLTAGE_CORRECTION_FACTOR, 2);
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
