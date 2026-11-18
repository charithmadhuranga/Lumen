#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#define SDA  4
#define SCL  16
// called this way, it uses the default address 0x40
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
int brightness = 0;    
int fadeAmount = 64;   
void setup() {
  Serial.begin(9600);
  Serial.println("16 channel PWM test!");
  Wire.begin(SDA,SCL,100000);
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  
  pwm.setPWMFreq(1000);  // 1600 This is the maximum PWM frequency

  // if you want to really speed stuff up, you can go into 'fast 400khz I2C' mode
  // some i2c devices dont like this so much so if you're sharing the bus, watch
  // out for this!
  Wire.setClock(400000);
}

void loop() {
  // Drive each PWM in a 'wave'
 
   for (uint8_t pwmnum=0; pwmnum < 16; pwmnum++) {
      pwm.setPWM(pwmnum, 0, brightness );
    }
    brightness = brightness + fadeAmount;
   if (brightness == 0 || brightness == 4096) {
    fadeAmount = -fadeAmount ;
  } 
  Serial.println(brightness);             
   delay(20);
    
  
   

  
}
