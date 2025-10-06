// attiny85
// 8MHz operation

// pin functions:
// 0 OC0A standby led
// 1 PORTB1 soft start bypass mosfet output
// 2 INT0 charge button
// 3 PORTB3 charge mosfet output
// 4 ADC2 output voltage in
// 5 ADC0 output voltage set

enum LedMode : uint8_t {
  OFF, // duh
  ON, // duh
  BLINK, // 2Hz full blinks
  BREATHE, // 2Hz, increasing then decreasing duty cycle
  BREATHE_FAST, // 4Hz breathe
  BLINK_FAST, // 4Hz full blinks
  BLINK_VERY_FAST, // 8Hz full blinks
  BLINK_EXTREMELY_FAST // 16Hz full blinks
};

enum Mode : uint8_t {
  IDLE, // low voltage, not active
  CHARGED, // high voltage, not active
  SOFT_START_CHARGING, // low voltage, active
  CHARGING, // high voltage, active
  CHARGED_MAINTAIN, // high voltage, not active
  CHARGING_MAINTAIN, // high voltage, active
  ERROR // high voltage, not active
};

void setup() {
  cli(); // no interrupting setup
  
  DDRB = 0b00001011; // output on pin 0, pin 1, pin 3 
  PORTB = 0b00000100; // pullup 2

  TCCR0A = (1 << WGM01) | (1 << WGM00); // Fast PWM
  TCCR0A |= (1 << COM0A1); // non inverting output on OC0A (pin 0)
  TCCR0B = (1 << CS01); //set prescaler to /8

  TCCR1 = (1 << CS13) | (1 << CS12) | (1 << CS11) | (1 << CS10); // set prescaler to /16384
  // thus timer1 ticks take 2ms and timer1 overflow is every 500ms

  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1); // enable ADC, set prescaler to /64 since 8MHz sysclock
  ADMUX = (1 << ADLAR); // left-adjust ADC to turn it into a 8-bit ADC, and use Vcc (regulated) as reference
  
  GIMSK |= (1 << INT0); // enable external interrupt...
  MCUCR |= (1 << ISC01); // on falling edge

  sei();
}

void maintainLedOutputs(LedMode standbyLedMode){
  static uint8_t standbyLedOutput; // OCR0A
  switch(standbyLedMode){
    case OFF:
      OCR0A = 0;
      break;
    case ON:
      OCR0A = 0xFF;
      break;
    case BLINK:
      OCR0A = (TCNT1 & 0x80)? 0xFF : 0;
      break;
    case BLINK_FAST:
      OCR0A = (TCNT1 & 0x40)? 0xFF : 0;
      break;
    case BLINK_VERY_FAST:
      OCR0A = (TCNT1 & 0x20)? 0xFF : 0;
      break;
    case BLINK_EXTREMELY_FAST:
      OCR0A = (TCNT1 & 0x10)? 0xFF : 0;
      break;
    case BREATHE:
      OCR0A = (TCNT1 & 0x80)
        ? map(TCNT1, 0x80, 0xFF, 0xFF, 0)
        : map(TCNT1, 0, 0x7F, 0, 0xFF);
      break;
    case BREATHE_FAST:
      OCR0A = (TCNT1 & 0x40)
        ? map(2*TCNT1, 0x80, 0xFF, 0xFF, 0)
        : map(2*TCNT1, 0, 0x7F, 0, 0xFF);
      break;
  }
  return;
}
 
uint8_t MAINTAIN_VOLTAGE_DELTA_2V = 8;
uint8_t SAFE_VOLTAGE_2V = 15;
uint8_t MINUMUM_SOFT_START_BYPASS_VOLTAGE_2V = 45; // over 90V, current consumption on continuous charging will be low enough
uint8_t MINIMUM_SET_VOLTAGE_2V = 80; // at least 160V
uint8_t MAXIMUM_SET_VOLTAGE_2V = 210; // at most 420V
uint8_t MAXIMUM_OPERATING_VOLTAGE_2V = 218; // ~435V - stop immediately!

// in units of 2V (since expect an absolute maximum of 450V)
uint8_t readSetVoltage(){
  ADMUX &= ~(15 << MUX0); // use ADC0 (pin 5)
  ADCSRA |= (1 << ADSC); // start ADC
  while (ADCSRA & (1 << ADSC));
  // this is the reset pin, so MSB will always read 1
  return map(ADCH, 0x80, 0xFF, MINIMUM_SET_VOLTAGE_2V, MAXIMUM_SET_VOLTAGE_2V);
}

// in units of 2V (since expect an absolute maximum of 450V)
uint8_t readOutputVoltage(){
  ADMUX &= ~(15 << MUX0); 
  ADMUX |= (1 << MUX1); // use ADC2 (pin 4)

  ADCSRA |= (1 << ADSC); // start ADC
  while (ADCSRA & (1 << ADSC));
  // the output voltage is fed through a 1/101 divider. Vref is 5.1V. 
  // this will require calibration with an external voltage reference to be sure, though, since the ADC just isn't that accurate.
  return map(ADCH, 0, 0xFF, 0, 0xFC);
  // on my particular ATTINY, setting the input and output ranges as such gives the correct voltage as output in units of 2V. 
  // note that the maximum output represents a voltage well in excess of 500V, and therefore also an imminent explosion.
}

volatile uint8_t buttonEventToConsume;
ISR(INT0_vect){
  buttonEventToConsume = 2; // depending on where in the main loop this happens, a value of 1 could be wiped out too soon
}
uint8_t chargeButtonIsPressed(){
  uint8_t ret = buttonEventToConsume;
  buttonEventToConsume = 0;
  return ret;
}

void charge(uint8_t active){
  if (active){
    PORTB |= (1 << PORTB3);
  } else {
    PORTB &= ~(1 << PORTB3);
  }
}

void bypassSoftStart(uint8_t bypass){
    if (bypass){
    PORTB |= (1 << PORTB1);
  } else {
    PORTB &= ~(1 << PORTB1);
  }
}

void loop() {
  static Mode mode = (readOutputVoltage() >= MINIMUM_SET_VOLTAGE_2V)? CHARGED : IDLE;
  switch (mode){
    case IDLE:
      maintainLedOutputs(ON);
      bypassSoftStart(0);
      if (chargeButtonIsPressed()){
        if (readOutputVoltage() >= MINUMUM_SOFT_START_BYPASS_VOLTAGE_2V){
          mode = CHARGING;
        } else {
          mode = SOFT_START_CHARGING;
        }
      }
      break;
    case SOFT_START_CHARGING:
      // capacitors at 0V will suck a TON of current, easily knocking out a ZVS driver. So direct voltage through the soft start resistors first
      maintainLedOutputs(BLINK);
      bypassSoftStart(0);
      charge(1); 
      if (readOutputVoltage() >= MINUMUM_SOFT_START_BYPASS_VOLTAGE_2V){
        mode = CHARGING;
      }
      break;
    case CHARGING:
      maintainLedOutputs(BLINK_FAST);
      bypassSoftStart(1);
      charge(1);
      // finished charging?
      if (readOutputVoltage() >= readSetVoltage()){
        mode = CHARGED;
      }
      // externally discharged
      if (readOutputVoltage() < SAFE_VOLTAGE_2V){
        mode = IDLE;
      }
      break;
    case CHARGED:
      maintainLedOutputs(ON);
      charge(0);
      if (chargeButtonIsPressed()){
        mode = CHARGED_MAINTAIN;
      }
      // voltage dropped back down?
      if (readOutputVoltage() < MINIMUM_SET_VOLTAGE_2V){
        mode = IDLE;
      }
      break;
    case CHARGED_MAINTAIN:
      maintainLedOutputs(BREATHE);
      charge(0);
      if (chargeButtonIsPressed()){
        mode = CHARGED;
      }
      // voltage dropped back down?
      if (readOutputVoltage() < readSetVoltage() - MAINTAIN_VOLTAGE_DELTA_2V){
        mode = CHARGING_MAINTAIN;
      }
      // externally discharged
      if (readOutputVoltage() < MINIMUM_SET_VOLTAGE_2V){
        mode = IDLE;
      }
      break;
    case CHARGING_MAINTAIN:
      maintainLedOutputs(BREATHE_FAST);
      charge(1);
      if (chargeButtonIsPressed()){
        mode = CHARGING;
      }
      // finished charging?
      if (readOutputVoltage() >= readSetVoltage()){
        mode = CHARGED_MAINTAIN;
      }
      // externally discharged
      if (readOutputVoltage() < MINIMUM_SET_VOLTAGE_2V){
        mode = IDLE;
      }
      break;

    case ERROR:
      // terminal state - deactivate charging!
      charge(0);
      bypassSoftStart(0);
      maintainLedOutputs(BLINK_EXTREMELY_FAST);
      break;
  }
  // check output voltage is within maximum
  if (readOutputVoltage() > MAXIMUM_OPERATING_VOLTAGE_2V){
    mode = ERROR;
  }
  // button event decays
  if (buttonEventToConsume > 0){
    buttonEventToConsume--;
  }
}


