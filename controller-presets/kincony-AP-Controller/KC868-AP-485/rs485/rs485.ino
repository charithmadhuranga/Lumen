/*/*WWW.KINCONY.COM*/
/*KC868-A8s code of RS485 */
/*void setup() {
Serial2.begin(115200,SERIAL_8N1,16,14); //  IO32   485RX   IO33 485TX


}

void loop() {
Serial2.println("KinCony ACBDEFG"); // Enter your print string
delay(1500);
}*/


void setup() {
  Serial.begin(115200);
   // Serial2.begin(115200,SERIAL_8N1,32,33);//a8&a8s
  Serial2.begin(115200,SERIAL_8N1,34,2);//a16
  //Serial2.begin(115200,SERIAL_8N1,16,14);//a32
  //Serial2.begin(115200,SERIAL_8N1,13,14);//a64
   Serial2.println("RS485 SEND is OK!!");
   Serial2.println("******************");
  
}

void loop() {
  /*print the received data of RS485 port*/
  while(Serial2.available()>0)
   {
    Serial2.print((char)Serial2.read());//Read rs485 receive data  and print it
   }
  delay(200);
}
