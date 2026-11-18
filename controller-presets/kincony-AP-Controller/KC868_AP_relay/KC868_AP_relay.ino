void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(13,OUTPUT);
  pinMode(2,OUTPUT);
 
}

// the loop function runs over and over again forever
void loop() {


  digitalWrite(2, HIGH);
  delay(1000);
  digitalWrite(13, HIGH);
  delay(1000);
  digitalWrite(2, LOW);
  delay(1000);
  digitalWrite(13, LOW);

}
