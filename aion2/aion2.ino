volatile uint32_t PIT_Ticks = 0;
const uint32_t PIT_TICKS_PER_SECOND = 512;
volatile uint32_t seconds = 0;
volatile uint32_t btnTicks = 0;

enum DEVICE_STATE {
  INITALIZING,
  STARTUP,
  MAIN_TIMER,
  WAITING_FOR_PAUSE,
  PAUSE_TIMER
};

DEVICE_STATE deviceState = INITALIZING;

enum DEVICE_MODE {
  NORMAL_MODE,
  QUIET_MODE,
  AGGRESIVE_MODE,
};

DEVICE_MODE deviceMode = NORMAL_MODE;


enum LED_STATE {
  LED_TRANSITION_LOW,
  LED_LOW,
  LED_TRANSITION_MID,
  LED_HIGH,
  LED_TRANSITION_HIGH
};

volatile LED_STATE LED = LED_LOW;


#define LED_PWM_1024 TCA0_SINGLE_CMP1
#define M1_PWM_1024 TCA0_SINGLE_CMP2

enum M1_STATE {
  M1_TIMER_DISABLE = -1,
  M1_DISABLE = 0,
  M1_MID = 250,
  M1_TIMER_HIGH = 379,
  M1_HIGH = 380,
};

volatile M1_STATE M1State = M1_DISABLE;
volatile int M1TimerVal = 0;

#define LATCH_OUT_PIN PIN_PA6
#define LATCH_IN_PIN PIN_PA5

#define LED_PIN PIN_PB1
#define M1_PIN PIN_PB2
#define BUZZER_PIN PIN_PB0

void handleLED() {
  const uint8_t step = 8;

  switch(LED) {
    case LED_LOW:
      LED_PWM_1024 = 0;
      break;

    case LED_TRANSITION_LOW:
      if (LED_PWM_1024 >= 0 + step) {
        LED_PWM_1024 -= step;
      }
      break;
    
    case LED_TRANSITION_MID:
      if (LED_PWM_1024 >= 0 && LED_PWM_1024 <= 256-step) {
        LED_PWM_1024 += step;
      } else if (LED_PWM_1024 <= 1024 && LED_PWM_1024 >= 256+step) {
        LED_PWM_1024 -= step;
      } 
      break;
    case LED_TRANSITION_HIGH:
      if (LED_PWM_1024 <= 1024 - step) {
        LED_PWM_1024 += step;
      }
      break;
    
    case LED_HIGH:
      LED_PWM_1024 = 1024;
      break;

    default:
      LED_PWM_1024 = 0;
  }
}

void handleM1() {
  int multiplier = 10;
  if (deviceMode == QUIET_MODE) {
    multiplier = 0;
  } else if (deviceMode == AGGRESIVE_MODE) {
    multiplier = 15;
  }

  if (M1State == M1_TIMER_HIGH && M1TimerVal < 0) {
    M1TimerVal = 0;
    M1State = M1_TIMER_DISABLE;
  } else {
    M1TimerVal--;
  }

  switch (M1State) {
    case M1_TIMER_DISABLE:
    case M1_DISABLE:
      M1_PWM_1024 = (M1_DISABLE * multiplier) / 10;
      break;

    case M1_MID:
      M1_PWM_1024 = (M1_MID * multiplier) / 10;
      break;

    case M1_TIMER_HIGH:
    case M1_HIGH:
      M1_PWM_1024 = (M1_HIGH * multiplier) / 10;
      break;
  }
}

void shutdown() {
  deviceMode = NORMAL_MODE;
  LED = LED_TRANSITION_HIGH;
  M1State = M1_MID;
  wait_125ms();
  wait_125ms();

  //Disable all outputs (except M1 so that it bleeds energy from caps) and sleep, which will cut the power to the MCU until next button press
  cli();
  PORTA_DIRCLR = 0xff;
  PORTB_DIRCLR = 0xff - (1 << 2);

  SLPCTRL_CTRLA = SLEEP_MODE_IDLE | SLEEP_ENABLED_gc;
  asm("SLEEP");
}

void wait_125ms() {
  const uint32_t startTicks = PIT_Ticks;
  uint32_t diff = 0;
  while (diff <= 64) {
    if (PIT_Ticks >= startTicks) {
      diff = PIT_Ticks - startTicks;
    } else {
      diff = (512 - startTicks) + PIT_Ticks;
    }
  }
}

void wait_500ms() {
  wait_125ms();
  wait_125ms();
  wait_125ms();
  wait_125ms();
}

void wait_1s() {
  wait_500ms();
  wait_500ms();
}

void reset_timer() {
  PIT_Ticks = 0;
  seconds = 0;
}

void setup() {
  pinMode(LATCH_OUT_PIN, OUTPUT);
  digitalWrite(LATCH_OUT_PIN, HIGH);

  cli();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(M1_PIN, OUTPUT);
  digitalWrite(M1_PIN, LOW);

  pinMode(LATCH_IN_PIN, INPUT_PULLUP);


  // RTC PIT
  CLKCTRL.MCLKCTRLA = 0x1;
  RTC_CLKSEL = 0x0;

  RTC_PITINTCTRL = 0x1;
  RTC_PITCTRLA = RTC_PERIOD_CYC64_gc + 1;

  // TIMER A
  takeOverTCA0();
  TCA0_SINGLE_PER = 1024 - 1;

  TCA0_SINGLE_CNT = 0;
  TCA0_SINGLE_CTRLA = TCA_SINGLE_CLKSEL_DIV4_gc | 1;
  TCA0_SINGLE_CTRLB = (1 << 6) | (1 << 5) | TCA_SINGLE_WGMODE_SINGLESLOPE_gc;

  TCA0_SINGLE_CMP1 = 0;
  TCA0_SINGLE_CMP2 = 0;

  sei();
  
  deviceState = STARTUP;

}



void loop() {
  if (btnTicks > PIT_TICKS_PER_SECOND && deviceState != STARTUP) {
    shutdown();
  }


  switch (deviceState) {
    case STARTUP:
      LED = LED_TRANSITION_HIGH;

      wait_125ms();
      while(btnTicks > 0) {
        if (btnTicks > PIT_TICKS_PER_SECOND*3) {
          M1State = M1_HIGH;
          deviceMode = AGGRESIVE_MODE;
        } else if (btnTicks > (PIT_TICKS_PER_SECOND*2)/3) {
          LED = LED_TRANSITION_LOW;
          deviceMode = QUIET_MODE;
        }
      }

      LED = LED_TRANSITION_HIGH;
      M1State = M1_MID;
      wait_1s(); 
      LED = LED_TRANSITION_LOW;
      M1State = M1_DISABLE;
      wait_125ms();
      wait_125ms(); 
      wait_125ms();
      LED = LED_TRANSITION_HIGH;
      M1State = M1_MID;
      wait_125ms();
      wait_125ms();
      wait_125ms();
      LED = LED_TRANSITION_LOW;
      M1State = M1_DISABLE;
      
      reset_timer();
      deviceState = MAIN_TIMER;
      break;
    
    case MAIN_TIMER:
    {
      LED = LED_TRANSITION_LOW;
      M1State = M1_DISABLE;

      // const uint32_t TIMER_MAX = (deviceMode == AGGRESIVE_MODE) ? 5 : 10; //debug
      const uint32_t TIMER_MAX = (deviceMode == AGGRESIVE_MODE) ? 60 * 18 : 60 * 23;
      if (seconds >= TIMER_MAX) {
        reset_timer();
        deviceState = WAITING_FOR_PAUSE;
      }
      break;
    }
    case WAITING_FOR_PAUSE:
    {
      // Enable M1 for some time ONCE every few seconds without blocking
      // For unknown reasons interrupt seem to break this if and produce indeterminate behaviour
      cli();
      if ((seconds % 14 == 0) && (PIT_Ticks < 4)) {
        if (M1State != M1_TIMER_HIGH) {
          M1State = M1_TIMER_HIGH;
          M1TimerVal = PIT_TICKS_PER_SECOND/2;
        }
      }
      sei();

      if (seconds % 2 == 0) {
        LED = LED_TRANSITION_HIGH;
      } else {
        LED = LED_TRANSITION_MID;
      }

      if (btnTicks > 15) {
        reset_timer();
        deviceState = PAUSE_TIMER;
      }
      break;
    }

    case PAUSE_TIMER:
    {
      LED = LED_TRANSITION_LOW;
      M1State = M1_DISABLE;

      const uint32_t PAUSE_MAX = (deviceMode == AGGRESIVE_MODE) ? 35 : 25;
      if (seconds >= PAUSE_MAX) {
          LED = LED_TRANSITION_HIGH;
          M1State = M1_HIGH;
          wait_1s();

          reset_timer();
          deviceState = MAIN_TIMER;
      }
      break;
    }
    default:
      deviceState = STARTUP;
  }


}


void tickHandler() {
  PIT_Ticks++;
  if (PIT_Ticks >= PIT_TICKS_PER_SECOND) {
    seconds++;
    PIT_Ticks = 0;
  }

  bool buttonState = digitalRead(LATCH_IN_PIN);
  if (buttonState == LOW) {
    btnTicks++;
  } else {
    btnTicks = 0;
  }

  handleLED();
  handleM1();
}


ISR(RTC_PIT_vect) {
  cli();
  tickHandler();
  RTC_PITINTFLAGS = 1;
  sei();
}