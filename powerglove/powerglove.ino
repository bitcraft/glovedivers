#include <digitalWriteFast.h>
#include <Keyboard.h>

// ATmega32u4 - beetle
// pin values below are specific to my wiring
// function arduino "pin" PORT  BIT
// latch    9             B     5
// clock    11            B     7
// data     10            B     6

// these values are tuned a bit to make it faster, and deviate from the specs
// 3us is the minimum that delayMicroseconds can be accurate to
#define DATA_CLOCK_DELAY 0x05
#define LATCH_LONG_DELAY 0x0A
#define WRITE_CLOCK_WAIT 0x05
// too low causes packets to be 0xFF randomly
#define NEXT_GLOVE_BYTE_WAIT 0x82

// track modes
#define JOYSTICK 0x00
#define HIRES 0x01

// button values in glove mode
#define GBUTTON_0 0x00
#define GBUTTON_1 0x01
#define GBUTTON_2 0x02
#define GBUTTON_3 0x03
#define GBUTTON_4 0x04
#define GBUTTON_5 0x05
#define GBUTTON_6 0x06
#define GBUTTON_7 0x07
#define GBUTTON_8 0x08
#define GBUTTON_9 0x09
#define GBUTTON_A 0x0A
#define GBUTTON_B 0x0B
#define GBUTTON_LEFT 0x0C
#define GBUTTON_UP 0x0D
#define GBUTTON_DOWN 0x0E
#define GBUTTON_RIGHT 0x0F
#define GBUTTON_ENTER 0x80
#define GBUTTON_START 0x82
#define GBUTTON_SELECT 0x83

// Keyboard constants not defined in Keyboard.h
#define KEY_A 0x61
#define KEY_D 0x64
#define KEY_S 0x73
#define KEY_W 0x77

// virtual/mapped inputs
#define VKEY_UP KEY_W
#define VKEY_DOWN KEY_S
#define VKEY_LEFT KEY_A
#define VKEY_RIGHT KEY_D
#define VKEY_FIST KEY_LEFT_CTRL

// track vkey to create press/release events
#define VKEY_STATE_UP 0x01
#define VKEY_STATE_DOWN 0x02
#define VKEY_STATE_LEFT 0x04
#define VKEY_STATE_RIGHT 0x08
#define VKEY_STATE_A 0x10
#define VKEY_STATE_B 0x20
#define VKEY_STATE_FIST 0x30

// hardware
int latch = 9;
int clock = 11;
int data = 10;

unsigned int controller_state = 0;
unsigned int controller_old_state = 0;

unsigned char ready = 0;
unsigned char xabs = 0;
unsigned char yabs = 0;
unsigned char zabs = 0;
unsigned char rotation = 0;
unsigned char flexmap = 0;
unsigned char swroh = 0;
unsigned char gstat1 = 0;
unsigned char gstat2 = 0;
unsigned char rvalid = 0;

unsigned char thumb = 0;
unsigned char index_finger = 0;
unsigned char middle_finger = 0;
unsigned char ring_finger = 0;
bool fist = 0;

/* SETUP */
void setup() {
  Serial.begin(115200);
  while (!Serial)
    Keyboard.begin();

  pinMode(latch, OUTPUT);
  pinMode(clock, OUTPUT);
  pinMode(data, INPUT);
  digitalWrite(latch, LOW);
  digitalWrite(clock, LOW);

  // timer1
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  // OCR1A = 33332;  // 60hz
  OCR1A = 24999;  // 10hz
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (0 << CS12) | (1 << CS11) | (0 << CS10);
  TIMSK1 |= (1 << OCIE1A);
  sei();
}

ISR(TIMER1_COMPA_vect){
  // controllerReadHiRes();
};


void hires_enable() {
  int hires_enable_command[] = { 0x06, 0xC1, 0x08, 0x00, 0x02, 0xFF, 0x01 };

  // beeps when it is enabled
  // drain shift register?  needed?
  read_byte();

  // handshake
  digitalWriteFast(latch, HIGH);
  delayMicroseconds(7212);
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(2260);

  // write out init on clock&latch lines
  int x = 0;
  for (int t = 0; t < 7; t++) {
    x = hires_enable_command[t];
    for (int j = 0; j < 8; j++) {
      if (x & 0x80) {
        //Trigger latch with clock if MSB bit high
        digitalWriteFast(latch, HIGH);
        digitalWriteFast(clock, HIGH);
        delayMicroseconds(WRITE_CLOCK_WAIT);
        digitalWriteFast(clock, LOW);
        delayMicroseconds(WRITE_CLOCK_WAIT);
        digitalWriteFast(clock, HIGH);
      } else {
        //MSB is low so only need to trigger clock
        digitalWriteFast(latch, LOW);
        digitalWriteFast(clock, HIGH);
        delayMicroseconds(WRITE_CLOCK_WAIT);
        digitalWriteFast(clock, LOW);
        delayMicroseconds(WRITE_CLOCK_WAIT);
        digitalWriteFast(latch, HIGH);
      }
      x = x << 1;
      delayMicroseconds(17);
    }
    delayMicroseconds(78);
  }
  /// ????
  // delayMicroseconds(892);
  digitalWriteFast(clock, LOW);
  digitalWriteFast(latch, HIGH);
}

unsigned char read_byte(void) {
  unsigned char buffer = 0;

  digitalWriteFast(clock, LOW);
  digitalWriteFast(latch, HIGH);
  delayMicroseconds(LATCH_LONG_DELAY);
  digitalWriteFast(latch, LOW);
  buffer += digitalReadFast(data);

  for (int i = 0; i < 7; i++) {
    delayMicroseconds(DATA_CLOCK_DELAY);
    digitalWriteFast(clock, HIGH);
    delayMicroseconds(DATA_CLOCK_DELAY);
    buffer <<= 1;
    buffer += digitalReadFast(data);
    digitalWriteFast(clock, LOW);
  }

  // in spec, but seems to be not needed for power glove
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);

  return buffer;
}

void controllerReadHiRes() {
  ready = read_byte();
  // Serial.println(ready, HEX);

  if (ready == 0xFF) {
    hires_enable();
    delay(1000);
    return;
  }

  if (ready == 0xA0) {
    // Serial.println("ready");

    swroh = 0;
    flexmap = 0;

    // default to an open hand
    thumb = 4;
    index_finger = 4;
    middle_finger = 4;
    ring_finger = 4;

    xabs = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    yabs = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    zabs = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    rotation = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    flexmap = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    swroh = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    gstat1 = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    gstat2 = read_byte();
    delayMicroseconds(NEXT_GLOVE_BYTE_WAIT);
    rvalid = read_byte();

    // Serial.println(xabs, HEX);
    // Serial.println(yabs, HEX);
    // Serial.println(zabs, HEX);
    // Serial.println(rotation, HEX);
    // Serial.println(flexmap, BIN);
    // Serial.println(swroh, HEX);
    // Serial.println(gstat1, HEX);
    // Serial.println(gstat2, HEX);
    // Serial.println(rvalid, HEX);

    // the glove seems to occasionally 

    controller_old_state = controller_state;
    controller_state = 0;

    if (flexmap != 0xFF) {
      thumb = (flexmap & 0b11000000) >> 6;
      index_finger = (flexmap & 0b00110000) >> 4;
      middle_finger = (flexmap & 0b00001100) >> 2;
      ring_finger = (flexmap & 0b00000011);

      // Serial.print("thumb         "); Serial.println(thumb);
      // Serial.print("index_finger  "); Serial.println(index_finger);
      // Serial.print("middle_finger "); Serial.println(middle_finger);
      // Serial.print("ring_finger   "); Serial.println(ring_finger);

      int clenched = index_finger + middle_finger + ring_finger;
      if (clenched >= 5) {
        fist = 1;
        controller_state |= VKEY_STATE_FIST;
      } else if (fist & clenched <= 2) {
        controller_state &= VKEY_STATE_FIST;
        fist = 0;
      }
    }

    switch (swroh) {
      case GBUTTON_0:
        //Serial.println("lt");
        break;
      case GBUTTON_1:
        // Keyboard.write("1");
        break;
      case GBUTTON_2:
        // Keyboard.write("2");
        break;
      case GBUTTON_3:
        // Keyboard.write("3");
        break;
      case GBUTTON_4:
        // Keyboard.write("4");
        break;
      case GBUTTON_5:
        // Keyboard.write("5");
        break;
      case GBUTTON_6:
        // Keyboard.write("6");
        break;
      case GBUTTON_7:
        // Keyboard.write("7");
        break;
      case GBUTTON_8:
        // Keyboard.write("8");
        break;
      case GBUTTON_9:
        // Keyboard.write("9");
        break;
      case GBUTTON_A:
        controller_state |= VKEY_STATE_A;
        break;
      case GBUTTON_B:
        controller_state |= VKEY_STATE_B;
        break;
      case GBUTTON_LEFT:
        controller_state |= VKEY_STATE_LEFT;
        break;
      case GBUTTON_UP:
        controller_state |= VKEY_STATE_UP;
        break;
      case GBUTTON_DOWN:
        controller_state |= VKEY_STATE_DOWN;
        break;
      case GBUTTON_RIGHT:
        controller_state |= VKEY_STATE_RIGHT;
        break;
      case GBUTTON_ENTER:
        // Keyboard.press(KEY_RETURN);
        break;
      case GBUTTON_START:
        // Keyboard.press("r");
        break;
      case GBUTTON_SELECT:
        // Keyboard.press(KEY_TAB);
        break;
    }

    if (!(controller_state & controller_old_state & VKEY_STATE_FIST)) {
      if (controller_state & VKEY_STATE_FIST) {
        Keyboard.press(VKEY_FIST);
      } else if (controller_old_state & VKEY_STATE_FIST) {
        Keyboard.release(VKEY_FIST);
      }
    }
    if (!(controller_state & controller_old_state & VKEY_STATE_UP)) {
      if (controller_state & VKEY_STATE_UP) {
        Keyboard.press(VKEY_UP);
      } else if (controller_old_state & VKEY_STATE_UP) {
        Keyboard.release(VKEY_UP);
      }
    }
    if (!(controller_state & controller_old_state & VKEY_STATE_DOWN)) {
      if (controller_state & VKEY_STATE_DOWN) {
        Keyboard.press(VKEY_DOWN);
      } else if (controller_old_state & VKEY_STATE_DOWN) {
        Keyboard.release(VKEY_DOWN);
      }
    }
    if (!(controller_state & controller_old_state & VKEY_STATE_LEFT)) {
      if (controller_state & VKEY_STATE_LEFT) {
        Keyboard.press(VKEY_LEFT);
      } else if (controller_old_state & VKEY_STATE_LEFT) {
        Keyboard.release(VKEY_LEFT);
      }
    }
    if (!(controller_state & controller_old_state & VKEY_STATE_RIGHT)) {
      if (controller_state & VKEY_STATE_RIGHT) {
        Keyboard.press(VKEY_RIGHT);
      } else if (controller_old_state & VKEY_STATE_RIGHT) {
        Keyboard.release(VKEY_RIGHT);
      }
    }
  }
}

void controllerReadJoystick() {
  // for default mode, or prog 14

  controller_old_state = controller_state;
  controller_state = 0;

  // a is special
  digitalWriteFast(latch, HIGH);
  delayMicroseconds(12);
  digitalWriteFast(latch, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x01;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // loops are overrated

  // b
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x02;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // select
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x04;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // start
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x08;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // up
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x10;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // down
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x20;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // left
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x40;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // right
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);
  if (digitalReadFast(data) == LOW) {
    controller_state |= 0x80;
  }
  delayMicroseconds(DATA_CLOCK_DELAY);

  // final
  digitalWriteFast(clock, HIGH);
  delayMicroseconds(DATA_CLOCK_DELAY);
  digitalWriteFast(clock, LOW);

  // grip? CTRL

  // now we can use the data since we are not in lockstep with the glovea

  // if (!(controller_state & controller_old_state & 0x10)) {
  //   if (controller_state & 0x10) {
  //     Keyboard.press(119);
  //   } else if (controller_old_state & 0x10) {
  //     Keyboard.release(119);
  //   }
  // }
  // if (!(controller_state & controller_old_state & 0x20)) {
  //   if (controller_state & 0x20) {
  //     Keyboard.press(115);
  //   } else if (controller_old_state & 0x20) {
  //     Keyboard.release(115);
  //   }
  // }
  // if (!(controller_state & controller_old_state & 0x40)) {
  //   if (controller_state & 0x40) {
  //     Keyboard.press(97);
  //   } else if (controller_old_state & 0x40) {
  //     Keyboard.release(97);
  //   }
  // }
  // if (!(controller_state & controller_old_state & 0x80)) {
  //   if (controller_state & 0x80) {
  //     Keyboard.press(100);
  //   } else if (controller_old_state & 0x80) {
  //     Keyboard.release(100);
  //   }
  // }
}

void loop() {
  controllerReadHiRes();
  delay(60);
}