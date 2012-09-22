/*
ARDUPOWERSTRIP - JJFALLING ©2012
 https://github.com/jjfalling/ArduPowerStrip
 Published under GNU GPL.
 
 
 Much of this is based off of this: https://github.com/ajfisher/arduino-command-server
 Also where I found the split library
 Flash library found here: http://arduiniana.org/libraries/flash/
 Got EmonLib from here: http://openenergymonitor.org
 
 Also modified EmonLib using http://roysoala.wordpress.com/2012/04/20/energy-monitoring-using-pachube-and-arduino-1-0/
 
 TODO:
 -enable telent session timeout
 -allow only one session
 -on/off/reboot/status all
 -remove debug option for serial
 -add snmp support
 -up/down arrows/history?
 -snmp support -v5
 -various items commented as fix -v4
 -change network/hostname over telnet? move said settings to flash. -v4/v5
 
 -free up memory by making as many globals local as possible (voltage, amps, temp/humid, etc). also turn into functions - v4
 
 */


//################## 
//User settings
//################## 

//Keep in mind, standard ethernet shield reserves pins 4,10,11,12,13
//Also, you can use analog pins instead of dig pins by using A[pin number]. I have only tested this with relays.

//Network info
byte mac[] = { 
  0xDE, 0xAD, 0xFE, 0xAB, 0xFE, 0xED };
byte ip[] =   { 
  192,  168,  15, 17 };
byte gateway[] = { 
  192,  168,  15, 1 };
byte subnet[] = { 
  255, 255, 255, 0 };


//Hostname
const char hostname[] = "APS-rpc1";

//You need to define the type of relay you are using or how you have it wired. Some relays
// are off when set to low while others are on while set to low. You many need to play with this.
// To try an make this more simple, here is a guide for if your relay is set to NO or NC:
//NC: 1 is off=pin low, 0 is off=pin high  | NO: 1 is off=pin high, 0 is off=pin low
#define relayType 0    

//What digital pins are your outlets attached to (outlet1 is the first pin listed, outlet2 is the second pin, etc)?
const int outlets[] = {
  A0,A1,A2 };

//How long should the delay between off and on during a reboot be (in milliseconds)?
#define rebootDelay 3000

//How long before the telnet session times out(in milliseconds)?
#define telnetTimeout 300000

//What pin is the statusLED connected to?
#define statusLED 2

// What pin is the lcd backlight button connected to?
#define buttonPin 3

//What pin is the lcd serial pin connected to?
#define lcdTxPin 5

//What pin is the "factory default" button connected to?
#define defaultPin 6

//How long should the lcd be on before it turns its self off (in ms) [Set to 0 to disable]? 
#define lcdTimeout 600000

//What pin is the internal humid/temp (dht11) sensor on?
#define intDHT11 7

//What pin is the external humid/temp 1 (dht11) sensor on?
#define ext1DHT11 8

//What pin is the external humid/temp 2 (dht11) sensor on?
#define ext2DHT11 9

//Do you want fahrenheit or celsius? True is f, false if c
#define tempF true

//What pin are you using to sense voltage?
#define voltSensorPin 4

//What voltage in the ac line?
#define acVoltage 120

//What is your phase shift?
#define voltphaseShift 1.7

//What pin are you using to sense amperage?
#define ampSensorPin 5

//What amperage calibration?
#define ampCalibration 29

//FIX rm this: Enable serial debug? 
#define debug true


//################## 
//End user settings
//################## 




//start global section

#include <SPI.h>
#include <Ethernet.h>
#include <split.h>
#include <Flash.h>
#include <avr/wdt.h>
#include <SoftwareSerial.h>
#include <dht11.h>
#include <EmonLib.h>


//program name and version
#define _NAME "ArduPowerStrip"
#define _VERSION "0.4"



// Create an instance
EnergyMonitor emon1;                  

//Define the dht instance (I think?)
dht11 DHT11;

//define software serial for the lcd
SoftwareSerial lcdSerial = SoftwareSerial(255, lcdTxPin);

//disable watchdog, this is needed on newer chips to prevent a reboot loop. However I can't test this...
void wdt_init(void)
{
  MCUSR = 0;
  wdt_disable();

  return;
}


//status led
int ledState = LOW;             // ledState used to set the LED
long previousMillis = 0;        // will store last time LED was updated
long interval = 700;           // interval at which to blink (milliseconds)


int buttonState = 0;         // current state of the button
long previousMillisLCD = 0;
boolean backlight = true;     // variable for reading the pin status

int chk;

Print *client = &Serial;
Client *ethcl = NULL;

typedef void (* CommandFuncPtr)(String args); // typedef to the command

struct Command {
  char code[7]; // the code used to call the command
  String help; // the snippet help version
  CommandFuncPtr cmd; // point to the command to be called
};

String command;

//used to store time since last command in telnet
unsigned long telnetTime=0; 

//array for sensors (even is humid, odd is temp)
float sensorData[6];

//used for figuring out where we shove data into the array.
int startNum = 0;

//the sensors need a delay between reading, so we need a timer for them.
long previousMillisSensor = 0;
int sensorCounter = 0;

long previousMillisLCDT = 0;
int lcdCounter = 0;

//set this to the number of commands that are defined
#define MAX_COMMANDS 8
Command com[MAX_COMMANDS]={
};

//Don't know why, but for some reason I have to divide by 2 to get the real number
const int numOfOutlets = (sizeof(outlets))/2;

// start server on port 23 (telnet)
EthernetServer server(23);
boolean newClient = false;

boolean allRequested = false;

boolean validateError = false;

byte firstClientIP[4];

//init the ethernet library here so we can use it outside of the main loop.
EthernetClient eclient;

//shove a bunch of data into flash to save on sram...
FLASH_STRING(help_string,"----------------------\n| Available Commands |\n----------------------\nTry help <cmd> for more info\n\n");
FLASH_STRING(info_string,"----------------------\n| System Information |\n----------------------\n");
FLASH_STRING(boot1,"\nBooting...\n\n");
FLASH_STRING(boot2,"Outlet ");
FLASH_STRING(boot3," is pin ");
FLASH_STRING(boot4,"Device ready, telnet to ");
FLASH_STRING(boot5," to use this device.\n");
FLASH_STRING(main1,"Welcome to ");
FLASH_STRING(main2," running ");
FLASH_STRING(main3,"Enter command or type HELP...\n");
FLASH_STRING(process1,"ERR: Unknown command, try HELP?\n");
FLASH_STRING(error_1,"ERR: Outlet number not supplied\n");
FLASH_STRING(error_2,"ERR: Outlet number too high or too many arguments\n");
FLASH_STRING(error_3,"ERR: That outlet does not exist\n");
FLASH_STRING(error_4,"ERR: invalid power_req, this is an internal error that should not happen...\n");
FLASH_STRING(error_5,"ERR: Syntax error please use a valid command\n");
FLASH_STRING(set1,"Setting outlet ");
FLASH_STRING(set2," to ");
FLASH_STRING(off1,"OFF\n");
FLASH_STRING(on1,"ON\n");
FLASH_STRING(reboot1,"Rebooting outlet ");
FLASH_STRING(reboot2,", please wait about ");
FLASH_STRING(reboot3," ms.\n");
FLASH_STRING(done1,"Done.\n");
FLASH_STRING(status4,"Status of outlet ");
FLASH_STRING(status5," is: ");
FLASH_STRING(status6,"OFF\n");
FLASH_STRING(status7,"ON\n");
FLASH_STRING(status8,"UNKNOWN, found value is: ");
FLASH_STRING(info1,"Software: ");
FLASH_STRING(info2,"Version: ");
FLASH_STRING(info3,"Free memory (bytes): ");
FLASH_STRING(info4,"IP: ");
FLASH_STRING(info5,".");
FLASH_STRING(info6,"Mask: ");
FLASH_STRING(info7,"Gateway: ");
FLASH_STRING(info8,"MAC: ");
FLASH_STRING(info9,":");
FLASH_STRING(info10,"Hostname: ");
FLASH_STRING(info11,"Number of outlets: ");
FLASH_STRING(info12,"Reboot delay (ms): ");
FLASH_STRING(info13,"Volts: ");
FLASH_STRING(info14,"Amps: ");
FLASH_STRING(info15,"Int humid/temp: ");
FLASH_STRING(info16,"Ext1 humid/temp: ");
FLASH_STRING(info17,"Ext2 humid/temp: ");
FLASH_STRING(reset1,"Resetting controller.\n");
FLASH_STRING(exit1,"Closing connection. Goodbye...\n");
FLASH_STRING(connect1,"\n\nAnother user is already connected.");
FLASH_STRING(connect2,"\nOnly one user can connect at a time.\nClosing connection. Goodbye...\n\n");
FLASH_STRING(connect3,"Second user tried to connect");
FLASH_STRING(lcd1,"Volts:    ");
FLASH_STRING(lcd2,"Amps:      ");
FLASH_STRING(lcd3,"Humid I:    ");
FLASH_STRING(lcd4,"Humid E1:   ");
FLASH_STRING(lcd5,"Humid E2:   ");
FLASH_STRING(lcd6,"Temp I:    ");
FLASH_STRING(lcd7,"Temp E1:   ");
FLASH_STRING(lcd8,"Temp E2:   ");
FLASH_STRING(lcd9,"     Uptime:");

double Irms;
double Vrms;



//end of global section
//########################## 


void setup() { 

  Serial.begin(9600);
  client = &Serial;
  if (debug) Serial << boot1; 

  //set the pin modes 
  int x;
  for (x=0; x < numOfOutlets; x++) {
    int curr_pin=outlets[x];
    pinMode(curr_pin, OUTPUT);

    //set init pin state for the relay according to user settings
    if (relayType==0){
      digitalWrite(curr_pin, HIGH);
    }
    else {
      digitalWrite(curr_pin, LOW);
    }

    if (debug) Serial << boot2;
    if (debug) Serial.print(x +1);
    if (debug) Serial << boot3;
    if (debug) Serial.println(curr_pin);

  }

  client = &Serial;
  Ethernet.begin(mac, ip, gateway, subnet);
  // start listening for clients
  server.begin(); 


  Serial.println();
  Serial << boot4;
  Serial.print(ip[0]);
  Serial.print(".");
  Serial.print(ip[1]);
  Serial.print(".");
  Serial.print(ip[2]);
  Serial.print(".");
  Serial.print(ip[3]);
  Serial << boot5;


  com[0]=(Command){
    "HELP", "Prints this. Try HELP <CMD> for more", command_help  };
  com[1]=(Command){
    "INFO", "Shows system information", command_info  };
  com[2]=(Command){
    "STATUS", "Shows the status of a outlet", command_status  };
  com[3]=(Command){
    "ON", "Sets an outlet to on", command_on  };
  com[4]=(Command){
    "OFF", "Sets an outlet to off", command_off  };
  com[5]=(Command){
    "REBOOT", "Reboots an outlet", command_reboot  };
  com[6]=(Command){
    "QUIT", "Quits this session gracefully", command_quit  };
  com[7]=(Command){
    "RESET", "Preform a software reset on this device (and resets ALL relays!)", command_reset  };
  //com[8]=(Command){"SET", "Set system params (maybe?)", command_set};

  pinMode(statusLED, OUTPUT);

  // initialize the button pin as a input:
  pinMode(buttonPin, INPUT);
  pinMode(lcdTxPin, OUTPUT);
  digitalWrite(lcdTxPin, HIGH);  //parallax uses this in their example, so I assume its required...

  lcdSerial.begin(19200);
  delay(100);
  lcdSerial.write(22);                 // Turn backlight on
  lcdSerial.write(12);                 // Clear    
  delay(5);                            // Required delay  
  lcdSerial.write(17);                 // Turn backlight on


  //start up scale chirp thing
  lcdSerial.write(216);                //scale
  lcdSerial.write(208);                //note
  lcdSerial.write(220);                // play a
  lcdSerial.write(222);                // play b
  lcdSerial.write(223);                // play c
  lcdSerial.write(225);                // play d
  lcdSerial.write(227);                // play e
  lcdSerial.write(228);                // play f
  lcdSerial.write(230);                // play g


  //print name and version to lcd
  lcdSerial.write(128);              // line 0 pos 0
  lcdSerial.write(" ");
  lcdSerial.write(_NAME); 
  lcdSerial.write(148);            // line 1 pos 0
  lcdSerial.write("       ");      //as the version changes, the padding may need adjusting
  lcdSerial.write(_VERSION); 



  pinMode(defaultPin, INPUT_PULLUP); 
  
  // if the resetSwitch is LOW restore password and tcp parameters todefaul values
  if ( digitalRead( defaultPin ) == LOW ) { 

    lcdSerial.write(12);                // Clear 
 
    int foovar = 0;
    int resetMessage = 0;
    while(foovar < 200){
    
    unsigned long currentMillis = millis();
    if(currentMillis - previousMillisSensor > 2000) {
    
    switch (resetMessage) {
    
    case 0:
    lcdSerial.write(128);               // line 0 pos 0 
    lcdSerial.write("Default? press");   // Turn backlight on
    lcdSerial.write(148);               // line 1 pos 0
    lcdSerial.write("LCD button to");  // Turn backlight on
    resetMessage++
    
    case 1:
    lcdSerial.write(128);               // line 0 pos 0 
    lcdSerial.write("default settings");   // Turn backlight on
    lcdSerial.write(148);               // line 1 pos 0
    lcdSerial.write("or reset device");  // Turn backlight on
    resetMessage++
    
    case 2:
    lcdSerial.write(128);               // line 0 pos 0 
    lcdSerial.write("to cancel and");   // Turn backlight on
    lcdSerial.write(148);               // line 1 pos 0
    lcdSerial.write("boot normally");  // Turn backlight on
    resetMessage = 0;
    
    }
    }
    }
  }
  



  //update voltage and amperage data 
  emon1.voltage(voltSensorPin, acVoltage, voltphaseShift);  // Voltage: input pin, calibration, phase_shift
  emon1.current(ampSensorPin, ampCalibration);       // Current: input pin, calibration. calibration const= 1800/62. CT SCT-013-030 ratio=1800, RL 62ohm 

}



void loop() {

  eclient = server.available();

  //turn status led on since the device is now operational
  digitalWrite(statusLED, HIGH);

  if (eclient && !newClient) {
    if (debug) Serial.println("User connected");

    newClient = true;

    //FIX: get client ip so the multiple user error message will say who is logged in
    //get client's ip
    //firstClientIP = eclient.remoteIP()

    client = &eclient;
    ethcl = &eclient; // set the global to use later
    client->println();
    client->println();

    eclient << main1;
    client->print(hostname);
    eclient << main2;
    client->print(_NAME);
    client->print(" ");
    client->println(_VERSION);
    eclient << main3;  
    print_prompt();

    eclient.flush();


    while(eclient.connected()) {

      if (eclient.available()){
        char ch = eclient.read();
        //Serial.println(ch, DEC);
        if (ch == 10) {
          // new line so now we can attempt to process the line
          //eclient.println(command);
          process_command(&command);
          command = "";
        } 
        else if ((ch < 10 && ch > 0) || (ch > 10 && ch < 32)) {
          // ignore control chars up to space and allow nulls to pass through
          ; 
        } 
        else {
          command += String(ch);
        }
      }


      //Blink status led while there is an active telnet session
      //This is from http://arduino.cc/en/Tutorial/BlinkWithoutDelay 
      unsigned long currentMillis = millis();
      if(currentMillis - previousMillis > interval) {
        // save the last time you blinked the LED 
        previousMillis = currentMillis;   
        // if the LED is off turn it on and vice-versa:
        if (ledState == LOW)
          ledState = HIGH;
        else
          ledState = LOW;
        // set the LED with the ledState of the variable:
        digitalWrite(statusLED, ledState);
      }


      //control lcd backlight
      controlLCDBacklight();

      //update sensors
      updateSensors();

      //write data to lcd
      writeLCD();

      //FIX: calculating the power causes the prompt to freeze for about 2 seconds...

      //update power usage
      Irms = emon1.calcIrms(1480);  // Calculate Irms only
      Vrms = emon1.calcVrms(1480); // Calculate Vrms only   


    }

    if (!eclient.connected() && newClient) {
      eclient.stop();
      newClient = false;
      if (debug) Serial.println("User disconnected");

    }

  } 

  else if (eclient) {

    Serial <<  connect3;    
    eclient << connect1;
    // eclient << firstClientIP;
    eclient << connect2;
    client->println();
    eclient.stop();

  }


  //control lcd backlight
  controlLCDBacklight();

  //update sensors
  updateSensors();

  //write data to lcd
  writeLCD();

  //update power usage
  Irms = emon1.calcIrms(1480);  // Calculate Irms only
  Vrms = emon1.calcVrms(1480); // Calculate Vrms only   


}


//############################
//start defining commands/functions

void process_command(String* command) {
  // this method takes the command string and then breaks it down
  // looking for the relevant command and doing something with it or giving an error.
  String argv[2]; // we have 2 args, the command and the param
  split(' ', *command, argv, 1); // so split only once
  int cmd_index = command_item(argv[0]);

  //check if the all param was used. if so, change the allRequested to true
  if (argv[1] == "all"){
    allRequested = true; 
  }

  if (cmd_index >= 0) {
    com[cmd_index].cmd(argv[1]);
  } 
  else {
    eclient << process1;
    print_prompt();
  }

  return;
}



int command_item(String cmd_code) {
  // this method does all of the comparison stuff to determine the id of a command
  // which it then passes back
  int i=0;
  boolean arg_found = false;
  // look through the array of commands until you find it or else you exhaust the list.
  while (!arg_found && i<MAX_COMMANDS) {
    if (cmd_code.equalsIgnoreCase((String)com[i].code)) {
      arg_found = true;
    } 
    else {
      i++;
    }
  }

  if (arg_found) {
    return (i);
  } 
  else {
    return (-1);
  }

}



void command_status(String args) {
  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits


    int pin = atoi(&args[0]);

  validatePin(pin, args);

  if (validateError == true){
    validateError = false;
    return;
  }
  else {

    int realPin = pin -1;
    realPin = outlets[realPin];

    client->println();
    eclient << status4;
    client->print(pin);
    eclient << status5;

    int stat_pin = (digitalRead(realPin));

    if (relayType == 0) {

      switch (stat_pin){

        //is off
      case false:
        eclient << status6;
        print_prompt();
        break;

        //is on
      case true:
        eclient << status7;
        print_prompt();
        break;

        //unknown
      default:
        eclient << status8;
        client->println(stat_pin);
        print_prompt();
        break;

      }
    }
    else {

      switch (stat_pin){

        //is off
      case true: 
        eclient << status6;
        print_prompt();
        break;

        //is on
      case false:
        eclient << status7;
        print_prompt();
        break;

        //unknown
      default:
        eclient << status8;
        client->println(stat_pin);
        print_prompt();
        break;

      }
    }
  }
}



void command_on(String args) {

  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  int pin = atoi(&args[0]);

  validatePin(pin, args);

  if (validateError == true){
    validateError = false;
    return;
  }
  else {

    int realPin = pin -1;
    realPin = outlets[realPin];


    client->println();
    eclient << set1;
    client->print(pin);
    eclient << set2;
    eclient << on1;

    set_outlet(realPin, 1);
    eclient << done1; 

    print_prompt();
  }
}



void command_off(String args) {

  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  int pin = atoi(&args[0]);

  validatePin(pin, args);

  if (validateError == true){
    validateError = false;
    return;
  }
  else {

    int realPin = pin -1;
    realPin = outlets[realPin];


    client->println();
    eclient << set1;
    client->print(pin);
    eclient << set2;
    eclient << off1;
    set_outlet(realPin, 2);
    eclient << done1;  
    print_prompt();
  }

}



void command_reboot(String args) {

  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  int pin = atoi(&args[0]);

  validatePin(pin, args);
  if (validateError == true){
    validateError = false;
    return;
  }

  else {
    int realPin = pin -1;
    realPin = outlets[realPin];

    client->println();
    eclient << reboot1;
    client->print(pin);
    eclient << reboot2;
    client->print(rebootDelay);
    eclient << reboot3;
    set_outlet(realPin, 3);
    eclient << done1;
    print_prompt();
  }
}



void set_outlet(int pin, int power_req) {

  switch (power_req){

    //on
  case 1:
    {
      if (relayType == 0) {
        digitalWrite(pin, HIGH);
      }

      else {
        digitalWrite(pin, LOW);
      }
    }
    break;

    //off
  case 2:
    {
      if (relayType == 0) {
        digitalWrite(pin, LOW);
      }

      else {
        digitalWrite(pin, HIGH);
      }
    }
    break;

    //reboot
    //FIX: change from delay to something else so other tasks dont pause (such as status light, sensor updates, etc)
  case 3:
    {
      if (relayType == 0) {
        digitalWrite(pin, LOW);
        delay (rebootDelay);
        digitalWrite(pin, HIGH);

      }

      else {
        digitalWrite(pin, HIGH);
        delay (rebootDelay);
        digitalWrite(pin, LOW);

      }
    }
    break;


    //something went wrong  
  default:
    {
      if (debug) Serial << error_4;
      client->println();
      eclient << error_4;
    }
    break;

  }


}



//to save a bit of memory, making this into a function
void print_prompt() {
  client->println();
  client->print("> ");
}



//user requests help
void command_help(String args) {
  // this command spits out the help messages


    int cmd_index;
  if (args.length() >=2) {
    // we attempt to see if there is a command we should spit out instead.
    cmd_index = command_item(args);
    if (cmd_index < 0) eclient << error_5;
  } 
  else {
    cmd_index = -1;
  }

  if (cmd_index < 0) {
    client->println();

    eclient << help_string;
    for (int i=0; i<MAX_COMMANDS; i++){
      if (com[i].help != ""){
        client->print(com[i].code);
        if (i % 3 == 0) {
          client->println();
        } 
        else {
          client->print("\t");
        }
      }
    }
    client->println();
    print_prompt();
  } 
  else {
    // client->println("HELP");
    client->println();
    client->print(com[cmd_index].code);
    client->print(": ");
    client->println(com[cmd_index].help);
    client->println();
    print_prompt();
  }
}



//user requests info
void command_info(String args) {
  // this command spits out the info messages

  client->println();
  eclient << info_string;
  client->println();
  eclient << info1;
  client->println(_NAME);
  eclient << info2;
  client->print(_VERSION);
  client->println();

  //ip address
  eclient << info4;
  client->print(ip[0]);
  eclient << info5;
  client->print(ip[1]);
  eclient << info5;
  client->print(ip[2]);
  eclient << info5;
  client->println(ip[3]);

  //subnet mask
  eclient << info6;
  client->print(subnet[0]);
  eclient << info5;
  client->print(subnet[1]);
  eclient << info5;
  client->print(subnet[2]);
  eclient << info5;
  client->println(subnet[3]);

  //gateway
  eclient << info7;
  client->print(gateway[0]);
  eclient << info5;
  client->print(gateway[1]);
  eclient << info5;
  client->print(gateway[2]);
  eclient << info5;
  client->println(gateway[3]);

  //mac address
  eclient << info8;
  client->print(mac[0], HEX);
  eclient << info9;
  client->print(mac[1], HEX);
  eclient << info9;
  client->print(mac[2], HEX);
  eclient << info9;
  client->print(mac[3], HEX);
  eclient << info9;
  client->print(mac[4], HEX);
  eclient << info9;
  client->println(mac[5], HEX);

  //hostname
  eclient << info10;
  client->println(hostname);

  //number of outlets
  eclient << info11;
  client->println(numOfOutlets);

  //reboot delay
  eclient << info12;
  client->println(rebootDelay);

  //volt sensor
  eclient << info13;
  client->println(Vrms);

  //amp sensor
  eclient << info14;
  client->println(Irms);

  //humid/temp sensors
  eclient << info15;
  client->print(sensorData[0]);
  client->print(" | " );
  client->println(sensorData[1]);
  eclient << info16;
  client->print(sensorData[2]);
  client->print(" | " );
  client->println(sensorData[3]);
  eclient << info17;
  client->print(sensorData[4]);
  client->print(" | " );
  client->println(sensorData[5]);

  //show free mem
  eclient << info3;
  int result = memoryTest();
  client->println(result,DEC);

  print_prompt(); 

}



//user requests quit
void command_quit(String args) {
  // this method closes down the network connection.
  client->println();
  eclient << exit1;
  client->println();
  ethcl->stop();
}



//user wanted to reset controller
void command_reset(String args) {
  client->println();
  eclient << reset1;
  Serial << reset1;
  eclient << exit1;
  client->println();
  newClient=false;
  ethcl->stop();

  wdt_enable(WDTO_30MS);
  while(1) {
  };


}



//This bit from http://www.faludi.com/itp/arduino/Arduino_Available_RAM_Test.pde :
// this function will return the number of bytes currently free in RAM
int memoryTest() {
  int byteCounter = 0; // initialize a counter
  byte *byteArray; // create a pointer to a byte array
  // More on pointers here: http://en.wikipedia.org/wiki/Pointer#C_pointers

  // use the malloc function to repeatedly attempt allocating a certain number of bytes to memory
  // More on malloc here: http://en.wikipedia.org/wiki/Malloc
  while ( (byteArray = (byte*) malloc (byteCounter * sizeof(byte))) != NULL ) {
    byteCounter++; // if allocation was successful, then up the count for the next try
    free(byteArray); // free memory after allocating it
  }

  free(byteArray); // also free memory after the function finishes
  return byteCounter; // send back the highest number of bytes successfully allocated
}



//this controls the lcd backlight
void controlLCDBacklight() {


  // read the current state of the button
  buttonState = digitalRead(buttonPin);

  // if the button was pushed and the backlight is off, turn the light on
  if (buttonState == HIGH && backlight == false) {
    backlight = 1;
    lcdSerial.write(17);

    //keep screen from flickering 
    while (buttonState == HIGH){
      buttonState = digitalRead(buttonPin);
    }
  } 

  // if the button was pushed and the backlight is on, turn the light off
  else if (buttonState == HIGH && backlight == true) {
    backlight = false;
    lcdSerial.write(18);

    //keep screen from flickering 
    while (buttonState == HIGH){
      buttonState = digitalRead(buttonPin);
    }
  }


  //Check if backlight is on and if the timeout has been reached. 
  if(backlight == true && lcdTimeout != 0){
    unsigned long currentMillis = millis();
    if(currentMillis - previousMillisLCD > lcdTimeout) {
      lcdSerial.write(18);
      backlight = false;
    }
  }


  if(backlight == false){
    previousMillisLCD = millis();
  }  

}



void writeLCD() {
  //here we can have a few different screens for the lcd

  //need to use dtostrf to conv float to char. define buffer for said function.
  static char dtostrfbuffer1[2];

  unsigned long currentMillis = millis();
  if(currentMillis - previousMillisLCDT > 5000) {

    //display volt/amp
    if (lcdCounter == 0 ) {

      //FIX: make real data go to the lcd:
      lcdSerial.write(12);                 // Clear    
      delay(5);                            // Required delay
      lcdSerial.write(128);      // line 0 pos 0
      lcdSerial << lcd1;
      lcdSerial.print(Vrms); 
      lcdSerial.write(148);      // line 1 pos 0
      lcdSerial << lcd2;  
      lcdSerial.print(Irms);


      lcdCounter++;
      previousMillisLCDT = millis();

    }

    //display sensor 1
    else if (lcdCounter == 1 ) {

      lcdSerial.write(12);       // Clear    
      delay(5);                  // Required delay
      lcdSerial.write(128);      // line 0 pos 0
      lcdSerial << lcd3;
      lcdSerial.write(dtostrf(sensorData[0],2,0,dtostrfbuffer1));
      lcdSerial.write("%");
      lcdSerial.write(148);      // line 1 pos 0
      lcdSerial << lcd6;
      lcdSerial.write(dtostrf(sensorData[1],4,1,dtostrfbuffer1));

      lcdCounter++;
      previousMillisLCDT = millis();

    }

    //display sensor 2
    else if (lcdCounter == 2 ) {

      lcdSerial.write(12);       // Clear    
      delay(5);                  // Required delay
      lcdSerial.write(128);      // line 0 pos 0
      lcdSerial << lcd4;
      lcdSerial.write(dtostrf(sensorData[2],2,0,dtostrfbuffer1));
      lcdSerial.write("%");
      lcdSerial.write(148);      // line 1 pos 0
      lcdSerial << lcd7;
      lcdSerial.write(dtostrf(sensorData[3],4,1,dtostrfbuffer1));

      lcdCounter++;
      previousMillisLCDT = millis();

    }

    //display sensor 3
    else if (lcdCounter == 3 ) {

      lcdSerial.write(12);       // Clear    
      delay(5);                  // Required delay
      lcdSerial.write(128);      // line 0 pos 0
      lcdSerial << lcd5;
      lcdSerial.write(dtostrf(sensorData[4],2,0,dtostrfbuffer1));
      lcdSerial.write("%");
      lcdSerial.write(148);      // line 1 pos 0
      lcdSerial << lcd8;
      lcdSerial.write(dtostrf(sensorData[5],4,1,dtostrfbuffer1));

      lcdCounter++;
      previousMillisLCDT = millis();

    }

    else if (lcdCounter == 4 ) {

      lcdSerial.write(12);       // Clear    
      delay(5);                  // Required delay    
      lcdSerial.write(128);      // line 0 pos 0
      lcdSerial << lcd9; 
      lcdSerial.write(148);      // line 1 pos 0


      //following bit from http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1294011483
      int days=0;
      long hours=0;
      long mins=0;
      long secs=0;
      secs = currentMillis/1000; //convect milliseconds to seconds
      mins=secs/60; //convert seconds to minutes
      hours=mins/60; //convert minutes to hours
      days=hours/24; //convert hours to days
      secs=secs-(mins*60); //subtract the converted seconds to minutes in order to display 59 secs max
      mins=mins-(hours*60); //subtract the converted minutes to hours in order to display 59 minutes max
      hours=hours-(days*24); //subtract the converted hours to days in order to display 23 hours max

      lcdSerial.write(dtostrf(days,2,0,dtostrfbuffer1)); //max uptime is about 50 days, so dont do more then 2 chars
      lcdSerial.write("Days ");

      //if hours < 10 then prepend 0
      if (hours < 10 ) {
        lcdSerial.write("0");
        lcdSerial.write(dtostrf(hours,1,0,dtostrfbuffer1));
      }
      else {
        lcdSerial.write(dtostrf(hours,2,0,dtostrfbuffer1));
      }
      lcdSerial.write(":");
      //if mins < 10 then prepend 0
      if (mins < 10 ) {
        lcdSerial.write("0");
        lcdSerial.write(dtostrf(mins,1,0,dtostrfbuffer1));
      }
      else {
        lcdSerial.write(dtostrf(mins,2,0,dtostrfbuffer1));
      }
      lcdSerial.write(":");
      //if secs < 10 then prepend 0
      if (secs < 10 ) {
        lcdSerial.write("0");
        lcdSerial.write(dtostrf(secs,1,0,dtostrfbuffer1));
      }
      else {
        lcdSerial.write(dtostrf(secs,2,0,dtostrfbuffer1));
      }

      //since this is the last screen, reset the counter to start over
      lcdCounter=0;
      previousMillisLCDT = millis();

    }
  }
}



void updateSensors() {


  //there seems to be a need to have a delay between polling different sensor. I have had issues with other projects 
  // and had to use a delay to fix it. 

  unsigned long currentMillis = millis();
  if(currentMillis - previousMillisSensor > 2000) {

    if (sensorCounter == 0 ) {
      checkDHT11(1);
      sensorCounter++;
      previousMillisSensor = millis();

    }

    else if (sensorCounter == 1 ) {
      checkDHT11(2);
      sensorCounter++;
      previousMillisSensor = millis();

    }

    else if (sensorCounter == 2 ) {
      checkDHT11(3);
      sensorCounter=0;
      previousMillisSensor = millis();

    }
  }
}



//check requested sensor
int checkDHT11(int sensorNumber) {

  //figure out which sensor to poll
  switch (sensorNumber)
  {
  case 1:
    {
      chk = DHT11.read(intDHT11);
      startNum = 0;
      break;
    }


  case 2:
    {
      chk = DHT11.read(ext1DHT11);
      startNum = 2;
      break;
    }

  case 3:
    {
      chk = DHT11.read(ext2DHT11);
      startNum = 4;
      break;
    }

  }

  int secondNum = startNum + 1;

  //check if we can read the sensor 
  switch (chk)
  {
    //can read the sensor, put valid data into vars
  case 0:
    { 

      //get the humidity
      sensorData[startNum] = DHT11.humidity;

      //if user wanted f instead of c, convert
      if (tempF == true){
        float tempf = DHT11.temperature;
        sensorData[secondNum] = 1.8 * tempf + 32;
      }

      //otherwise dont convert
      else {
        sensorData[secondNum] = DHT11.temperature; 
      }
      break;

    }

    //invalid data. put impossible numbers into vars
  default: 
    {
      sensorData[startNum] =  999;
      sensorData[secondNum] = 999.9;
      break;
    }
  }

}



//here is a function to validate the pin input the user requested
void validatePin(int pin, String args){
  if (args.length() <= 0) {
    eclient << error_1;
    print_prompt();
    validateError=true;
    return;
  }	

  if (args.length() > 2) {
    eclient << error_2;
    print_prompt();
    validateError=true;
    return;
  }

  if (pin > numOfOutlets || pin < 1) {
    eclient << error_3;
    print_prompt();
    validateError=true;
    return;
  }
}






