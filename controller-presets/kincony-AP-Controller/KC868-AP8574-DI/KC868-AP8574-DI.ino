#include "Arduino.h"
#include "PCF8574.h"
#define AP_8574_SDA 4
#define AP_8574_SCL 16

// Set i2c address
PCF8574 pcf8574_1(0x22,AP_8574_SDA,AP_8574_SCL);
PCF8574 pcf8574_2(0x21,AP_8574_SDA,AP_8574_SCL);

unsigned long timeElapsed;
void setup()
{
 Serial.begin(115200);
 delay(1000);
 pinMode(34, INPUT);// input 17
 pinMode(35, INPUT);// input 18
 pcf8574_1.pinMode(P0, INPUT);
 pcf8574_1.pinMode(P1, INPUT);
 pcf8574_1.pinMode(P2, INPUT);
 pcf8574_1.pinMode(P3, INPUT);
 pcf8574_1.pinMode(P4, INPUT);
 pcf8574_1.pinMode(P5, INPUT);
 pcf8574_1.pinMode(P6, INPUT);
 pcf8574_1.pinMode(P7, INPUT);

 pcf8574_2.pinMode(P0, INPUT);
 pcf8574_2.pinMode(P1, INPUT);
 pcf8574_2.pinMode(P2, INPUT);
 pcf8574_2.pinMode(P3, INPUT);
 pcf8574_2.pinMode(P4, INPUT);
 pcf8574_2.pinMode(P5, INPUT);
 pcf8574_2.pinMode(P6, INPUT);
 pcf8574_2.pinMode(P7, INPUT);
 pcf8574_1.begin();
 pcf8574_2.begin();

}

void loop()
{
 uint8_t val1 = pcf8574_1.digitalRead(P0);
 uint8_t val2 = pcf8574_1.digitalRead(P1);
 uint8_t val3 = pcf8574_1.digitalRead(P2);
 uint8_t val4 = pcf8574_1.digitalRead(P3);
 uint8_t val5 = pcf8574_1.digitalRead(P4);
 uint8_t val6 = pcf8574_1.digitalRead(P5);
 uint8_t val7 = pcf8574_1.digitalRead(P6);
 uint8_t val8 = pcf8574_1.digitalRead(P7);

 uint8_t val9 = pcf8574_2.digitalRead(P0);
 uint8_t val10 = pcf8574_2.digitalRead(P1);
 uint8_t val11 = pcf8574_2.digitalRead(P2);
 uint8_t val12 = pcf8574_2.digitalRead(P3);
 uint8_t val13 = pcf8574_2.digitalRead(P4);
 uint8_t val14 = pcf8574_2.digitalRead(P5);
 uint8_t val15 = pcf8574_2.digitalRead(P6);
 uint8_t val16 = pcf8574_2.digitalRead(P7);
  
 if (val1==LOW) Serial.println("KEY1 PRESSED");
 if (val2==LOW) Serial.println("KEY2 PRESSED");
 if (val3==LOW) Serial.println("KEY3 PRESSED");
 if (val4==LOW) Serial.println("KEY4 PRESSED");
 if (val5==LOW) Serial.println("KEY5 PRESSED");
 if (val6==LOW) Serial.println("KEY6 PRESSED");
 if (val7==LOW) Serial.println("KEY7 PRESSED");
 if (val8==LOW) Serial.println("KEY8 PRESSED");

 if (val9==LOW) Serial.println("KEY9 PRESSED");
 if (val10==LOW) Serial.println("KEY10 PRESSED");
 if (val11==LOW) Serial.println("KEY11 PRESSED");
 if (val12==LOW) Serial.println("KEY12 PRESSED");
 if (val13==LOW) Serial.println("KEY13 PRESSED");
 if (val14==LOW) Serial.println("KEY14 PRESSED");
 if (val15==LOW) Serial.println("KEY15 PRESSED");
 if (val16==LOW) Serial.println("KEY16 PRESSED");
 if(digitalRead(34)== LOW) Serial2.println("KEY17 PRESSED");
 if(digitalRead(35)== LOW) Serial2.println("KEY18 PRESSED");
	delay(100);
}
