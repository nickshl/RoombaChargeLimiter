#include <core_adc.h>

#include <avr/sleep.h>
#include <avr/power.h>

// Resistor pin definition
static const uint8_t RESISTOR_PIN = 0u; // No LED on Model A
static const uint8_t LED_PIN = 1u; // LED on Model A

// ADC parameters. ADC reference voltage is divided by resistor divider scaler
// to calculate voltage before voltrage divider, not on MCU pin. Formula:
// Vref(mV) / (R2/(R1+R2)). If we use 2.56V and R1 = 47kOhm, R2 = 7.5kOhm
// then: 2560 / (7500/(47000+7500)) = 2560 / 0.13762 = 18600.
// Note: this is theoretical value! Because resistors has some tolerance,
// we need to test and correct this field for every device! 
static const uint32_t ADC_REF_VOLTAGE_MV = 18300u;
static const uint32_t ADC_MAX_VAL = 0x03FFu; // 10 bits

// Voltage thresholds
static const uint32_t NOT_CHARGING_THRESHOLD_MV =     16000u; // If less than that - switch state to NOT_CHARGING
static const uint32_t TRICKLE_CHARGING_THRESHOLD_MV = 16400u; // If less than that - resistor can be disengaged
static const uint32_t FULL_CHARGING_THRESHOLD_MV =    16800u; // If reached that - resistor must be enengaged

// Timings
static const uint32_t MIN_RESISTOR_ON_TIME_S = 90u;   // 1.5 minutes (Must be less than MAX_RESISTOR_ON_TIME_S!!!)
static const uint32_t MAX_RESISTOR_ON_TIME_S = 3600u; // 1 hour
static const uint32_t RESISTOR_ON_PAUSE_S =    5u;    // 5 seconds every MAX_RESISTOR_ON_TIME_S interval

// Define to obtain array length
#define NumberOf(x) (sizeof(x)/sizeof(x[0]))

// Data
uint16_t voltage_array[8u];
uint8_t voltage_array_ptr = 0u;

// States
enum State
{
  NOT_CHARGING = 0u,  // This state allow us to save ~1 mA
  FULL_CHARGING = 1u,
  TRICKLE_CHARGE = 2u
};

// Current state
State state = NOT_CHARGING;
// Current resistor state
bool resistor_enabled = false;
// Resistor on counter
uint32_t resistor_enabled_counter = 0u;

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
// Interrupt service routine for the ADC completion
ISR(ADC_vect)
{
  // Don't need to do anything - ADC interrupt wake up AVR core, that's all what we need.
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
ISR(WDT_vect)
{
  // Watchdog Change Enable & Watchdog Enable
  WDTCR = (_BV(WDCE) | _BV(WDE));
  // Disable watchdog
  WDTCR = 0u;
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
// 0=16ms, 1=32ms, 2=64ms, 3=125ms, 4=250ms, 5=500ms, 6=1sec, 7=2sec, 8=4sec, 9=8sec
void WatchdogSetup(uint8_t prescaler)
{
  prescaler = min(9, prescaler);
  uint8_t wdtcsr = (prescaler & 7) | ((prescaler & 8) ? _BV(WDP3) : 0);

  // Disable Watchdog Reset Flag
  MCUSR &= ~_BV(WDRF);
  // Watchdog Change Enable & Watchdog Enable
  WDTCR = _BV(WDCE) | _BV(WDE);
  // Watchdog Timeout Interrupt Enable & Watchdog Timeout Interrupt Enable & Prescaler
  WDTCR = _BV(WDCE) | _BV(WDIE) | wdtcsr;
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
void Sleep(uint8_t power_mode)
{
  // SLEEP_MODE_IDLE - the lowest power saving mode
  // SLEEP_MODE_ADC
  // SLEEP_MODE_PWR_DOWN - the highest power saving mode
  set_sleep_mode(power_mode); // Sleep mode is set here
  // Don't know what this function actually do...
  sleep_enable();
  // System sleeps here
  sleep_mode();
  // System continues execution here when watchdog timed out or ADC is done
  sleep_disable();     
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
uint16_t GetVoltage(uint16_t raw_value)
{
  return (uint16_t)((raw_value * ADC_REF_VOLTAGE_MV) / ADC_MAX_VAL);
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
uint16_t MeasureVoltage(void)
{
  // Enable ADC
  ADCSRA |= _BV(ADEN);
  // Sleep to reduce ADC noise
  Sleep(SLEEP_MODE_ADC);

  // Get ADC data, conver to mV and store in array
  voltage_array[voltage_array_ptr] = GetVoltage(ADC_GetDataRegister());
  voltage_array_ptr++;
  if(voltage_array_ptr >= NumberOf(voltage_array))
  {
    voltage_array_ptr = 0u;
  }

  // Calculate average voltage
  uint32_t voltage = 0u;
  for(uint8_t i = 0u; i < NumberOf(voltage_array); i++)
  {
    voltage += voltage_array[i];
  }
  voltage /= NumberOf(voltage_array);

  // Disable ADC to reduce power consumption
  ADCSRA &= ~_BV(ADEN);

  return (uint16_t)voltage;
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
void ResistorCtrl(bool turn_on, bool set_flag = true)
{
  // Clear bit for resistor pin: no Pull-Up and 0 if output
  PORTB &= ~(1u << RESISTOR_PIN);
  // Depends of requested action
  if(turn_on)
  {
    // Set resistor pin as output
    DDRB |= 1u << RESISTOR_PIN;
    // Turn LED on
    PORTB |= (1u << LED_PIN);
  }
  else
  {
    // Set pin sa input - High-Z state
    DDRB &= ~(1u << RESISTOR_PIN);
    // Turn LED off
    PORTB &= ~(1u << LED_PIN);
  }
  // We do not always need to set flag and clear counter
  if((set_flag) && (resistor_enabled != turn_on))
  {
    // Set resistor state flag
    resistor_enabled = turn_on;
    // Clear counter
    resistor_enabled_counter = 0u;
  }
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
void setup()
{                
  // Pull-up Disable: the pull-ups in the I/O ports are disabled even if the pin are configured to enable the pull-up 
  MCUCR |= _BV(PUD);
  // No Pull-Ups and 0 if output
  PORTB = 0u;
  // Only LED pin is output, the rest of pins is inputs
  DDRB = (1u << LED_PIN);

  // Set proper input channel
  ADC_SetInputChannel(ADC_Input_ADC1);
  // Set ADC Reference Voltage
  ADC_SetVoltageReference(ADC_Reference_Internal_2p56); // analogReference(INTERNAL2V56);
  // Enable ADC interrupt
  ADCSRA |= _BV(ADIE);
  // Prescaler 1/128
  ADCSRA |= _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);

  // Fill voltage array to get right average in the loop()
  for(uint8_t i = 0u; i < NumberOf(voltage_array); i++)
  {
    (void)MeasureVoltage();
  }
}

// ***********************************************************************************
// ***********************************************************************************
// ***********************************************************************************
void loop()
{
  uint32_t voltage = MeasureVoltage();

//  // Inverse LED - for debug
//  PINB |= (1u << LED_PIN);
//  // Voltage drop - discharging - for debug
//  if((state != NOT_CHARGING) && (voltage < NOT_CHARGING_THRESHOLD_MV))
//  {
//    state = NOT_CHARGING;
//    ResistorCtrl(false);
//  }

  // If resistor turn on too long we should disengage it for some time to prevent Err 7
  if(resistor_enabled)
  {
    // If resistor enabled for desired time
    if(resistor_enabled_counter >= 8u * MAX_RESISTOR_ON_TIME_S)
    {
      // Disengage resistor, but do not change flag & counter
      ResistorCtrl(false, false);
      // Clear counter
      resistor_enabled_counter = 0u;
    }
    // If resistor disabled for desired time
    if(resistor_enabled_counter == 8u * RESISTOR_ON_PAUSE_S)
    {
      // Engage resistor, do not change flag & counter
      ResistorCtrl(true, false);
    }
    // Increment counter
    resistor_enabled_counter++;
  }

  // Sleep time(Watchdog prescaler)
  uint8_t sleep_time = 1u; // Should be ~11 ms processing + 32 ms sleep, but in reality it takes 117 ms. Pretty close to desired 125 ms.

  // Process state
  switch(state)
  {
    // *******************************************************************************
    case NOT_CHARGING:
      if(voltage > NOT_CHARGING_THRESHOLD_MV)
      {
        state = FULL_CHARGING;
      }
      else
      {
        sleep_time = 6u; // 1 second sleep
      }
      break;

    // *******************************************************************************
    case FULL_CHARGING:
      if(resistor_enabled)
      {
        // If minimum resistor enable time passed
        if(resistor_enabled_counter > 8u * MIN_RESISTOR_ON_TIME_S)
        {
          // If voltage drop below threshold - disengage resistor
          if(voltage < TRICKLE_CHARGING_THRESHOLD_MV)
          {
            ResistorCtrl(false);
          }
          // Switch to trickle charge state after timeout
          state = TRICKLE_CHARGE;
        }
      }
      else
      {
        if(voltage >= FULL_CHARGING_THRESHOLD_MV)
        {
          ResistorCtrl(true);
        }
        else if(voltage < NOT_CHARGING_THRESHOLD_MV)
        {
          state = NOT_CHARGING; // Voltage drop - discharging
        }
      }
      break;

    // *******************************************************************************
    case TRICKLE_CHARGE:
      if(voltage >= FULL_CHARGING_THRESHOLD_MV)
      {
        // Resistor engaged permanently until state change
        ResistorCtrl(true);
      }
      else if(voltage < NOT_CHARGING_THRESHOLD_MV)
      {
        // Disengage resistor
        ResistorCtrl(false);
        // Voltage drop - discharging
        state = NOT_CHARGING;
      }
      break;

    // *******************************************************************************
    default:
      break;
  }

  // Sleep some time
  WatchdogSetup(sleep_time);
  Sleep(SLEEP_MODE_PWR_DOWN);
}
