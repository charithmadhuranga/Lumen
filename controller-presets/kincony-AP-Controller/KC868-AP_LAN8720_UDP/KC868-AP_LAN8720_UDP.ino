#include <ETH.h> 
#include <WiFiUdp.h> 

#define ETH_ADDR        0
#define ETH_POWER_PIN  -1
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN   18   
#define ETH_TYPE       ETH_PHY_LAN8720
#define ETH_CLK_MODE   ETH_CLOCK_GPIO17_OUT

WiFiUDP Udp;                      //Create UDP object
unsigned int localUdpPort = 4196; //local port

// Set it based on the IP address of the router
IPAddress local_ip(192, 168, 1, 200); 
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 1);

void setup()
{
  Serial.begin(115200);
  Serial.println();
   
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE); //start with ETH

  // write confir for static IP, gateway,subnet,dns1,dns2
  if (ETH.config(local_ip, gateway, subnet, dns, dns) == false) {
    Serial.println("LAN8720 Configuration failed.");
  }else{Serial.println("LAN8720 Configuration success.");}
  
 /* while(!((uint32_t)ETH.localIP())) //wait for IP
  {

  }*/
  Serial.println("Connected");
  Serial.print("IP Address:");
  Serial.println(ETH.localIP());

  Udp.begin(localUdpPort); //begin UDP listener
}

void loop()
{
  int packetSize = Udp.parsePacket(); //get package size
  if (packetSize)                     //if have received data
  {
    char buf[packetSize];
    Udp.read(buf, packetSize); //read current data

    Serial.println();
    Serial.print("Received: ");
    Serial.println(buf);
    Serial.print("From IP: ");
    Serial.println(Udp.remoteIP());
    Serial.print("From Port: ");
    Serial.println(Udp.remotePort());

    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort()); //ready to send data
    Udp.print("Received: ");    
    Udp.write((const uint8_t*)buf, packetSize); //copy data to sender buffer
    Udp.endPacket();            //send data
  }
} 
