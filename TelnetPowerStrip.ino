/*
ARDUPOWERSTRIP - JJFALLING ©2012
 Published under GNU GPL.
 
 Much of this is based off of this: https://github.com/ajfisher/arduino-command-server
 Also where I found the split library
 
 
 There is a known bug with the help function, looks like a memory issue. Working on fixing it...
 */

//################## 17
//User settings
//################## 

//Network info
byte mac[] = { 
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] =   { 
  192,  168,  15, 17 };
byte gateway[] = { 
  192,  168,  15, 1 };
byte subnet[] = { 
  255, 255, 255, 0 };

//Hostname
const char hostname[] = "APS-rpc1";

//You need to define the type of relay you are using or how you have it wired.
//NC is 1, NO is 0.
const int relayType = 1;    

//What digital pins are your outlets attached to (outlet1 is the first pin listed, outlet2 is the second pin, etc)?
const int outlets[] = {
  7,8};

//How long should the delay between off and on during a reboot be (in milliseconds)?
const int rebootDelay = 3000;

//Enable serial debug? (leave this as true)
boolean debug = true;

//What pin is connected to the reset pin?
//const int resetPin = 2;

//################## 
//End user settings
//################## 

#include <SPI.h>
#include <Ethernet.h>
#include <split.h>

//program name and version
const char progName[] = "ArduPowerStrip";
#define _VERSION "0.1"

Print *client = &Serial;
Client *ethcl = NULL;

typedef void (* CommandFuncPtr)(String args); // typedef to the command

struct Command {
  char code[7]; // the code used to call the command
  String help; // the snippet help version
  CommandFuncPtr cmd; // point to the command to be called
};


String command;

#define MAX_COMMANDS 7
Command com[MAX_COMMANDS]={
};

//Don't know why, but for some reason I have to divide by 2 to get the real number
const int numOfOutlets = (sizeof(outlets))/2;


//##########################
// telnet defaults to port 23
EthernetServer server(23);
boolean newClient = false;

void setup() {

  Serial.begin(9600);
  client = &Serial;
  if (debug) Serial.println("Booting...\nStarted serial interface");

  //set the pin modes
  int x;
  for (x=0; x < numOfOutlets; x++) {
    int curr_pin=outlets[x];
    pinMode(curr_pin, OUTPUT);
//    if (debug) Serial.print("Outlet ");
//    if (debug) Serial.print(x +1);
//    if (debug) Serial.print(" is pin ");
//    if (debug) Serial.println(curr_pin);

  }

  //pinMode(resetPin, OUTPUT);

  client = &Serial;
  if (debug) Serial.println("Starting network");
  Ethernet.begin(mac, ip, gateway, subnet);
  // start listening for clients
  if (debug) Serial.println("Starting telent server");
  server.begin(); 

  client->println();
  client->print("Device ready, telnet to ");
  client->print(ip[0]);
  client->print(".");
  client->print(ip[1]);
  client->print(".");
  client->print(ip[2]);
  client->print(".");
  client->print(ip[3]);
  client->println(" to use this device.");


  com[0]=(Command){
    "HELP", "Prints this. Try HELP <CMD> for more", command_help    };
  com[1]=(Command){
    "INFO", "Shows system information", command_info    };
  com[2]=(Command){
    "SHOW", "Shows the status of a outlet", command_status    };
  com[3]=(Command){
    "ON", "Sets an outlet to on", command_on    };
  com[4]=(Command){
    "OFF", "Sets an outlet to off", command_off    };
  com[5]=(Command){
    "REBOOT", "Reboots an outlet", command_reboot    };
  com[6]=(Command){
    "QUIT", "Quits this session gracefully", command_quit    };
  //com[7]=(Command){"RESET", "Resets (reboots) this device", command_reset};
  //com[8]=(Command){"NAME", "To be impimented... Maybe?", command_name};


}



void loop() {

  EthernetClient eclient = server.available();

  if (eclient) {
    if (debug) Serial.print("We have a new client ");
    client = &eclient;
    ethcl = &eclient; // set the global to use later
    client->println();
    client->println();
    client->print("Welcome to ");
    client->print(hostname);
    client->print(" running ");
    client->print(progName);
    client->print(" ");
    client->println(_VERSION);
    client->println("Enter command or type HELP...");  
    print_prompt();

    newClient = true;
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
    }

    if (!eclient.connected() && newClient) {
      eclient.stop();
      newClient = false;
      if (debug) Serial.println("disconnecting");
    }
  }

}









//############################

void process_command(String* command) {
  // this method takes the command string and then breaks it down
  // looking for the relevant command and doing something with it or erroring.
  String argv[2]; // we have 2 args, the command and the param
  split(' ', *command, argv, 1); // so split only once
  int cmd_index = command_item(argv[0]);
  if (cmd_index >= 0) {
    com[cmd_index].cmd(argv[1]);
  } 
  else {
    client->println("ERR: Unknown command, try HELP?");
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
  // reads a specified digital pin
  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  if (args.length() <= 0) {
    client->println("ERR: Outlet number not supplied");
    print_prompt();
    return;
  }
  if (args.length() > 2) {
    client->println("ERR: Outlet number too high or too many arguments");
    print_prompt();
    return;
  }

  int pin = atoi(&args[0]);
  int realPin = pin -1;
  realPin = outlets[realPin];

  if (pin > numOfOutlets || pin < 0) {
    client->println("ERR: That outlet does not exist");
    print_prompt();
    return;
  }

  client->println();
  client->print("Status of outlet ");
  client->print(pin);
  client->print(" is: ");

  int stat_pin = (digitalRead(realPin));

  if (relayType == 0) {

    switch (stat_pin){

    case false:
      client->println("OFF");
      print_prompt();
      break;

    case true:
      client->println("ON");
      print_prompt();
      break;

    default:
      client->print("UNKNOWN, found value is: ");
      client->println(stat_pin);
      print_prompt();
      break;

    }
  }
  else {

    switch (stat_pin){

    case true:
      client->println("OFF");
      print_prompt();
      break;

    case false:
      client->println("ON");
      print_prompt();
      break;

    default:
      client->print("UNKNOWN, found value is: ");
      client->println(stat_pin);
      print_prompt();
      break;

    }
  }

}

void command_on(String args) {

  // reads a specified digital pin
  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  if (args.length() <= 0) {
    client->println("ERR: Outlet number not supplied");
    return;
  }
  if (args.length() > 2) {
    client->println("ERR: Outlet number too high or too many arguments");
    return;
  }

  int pin = atoi(&args[0]);
  int realPin = (atoi(&args[0])) -1;

  if (pin > numOfOutlets || pin < 0) {
    client->println("ERR: Invalid outlet specified");
    print_prompt();
    return;
  }

  client->println();
  client->print("Setting outlet ");
  client->print(pin);
  client->println(" to ON");

  set_outlet(realPin, 1);
  client->println("Done. "); 

  print_prompt();

}

void command_off(String args) {

  // reads a specified digital pin
  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  if (args.length() <= 0) {
    client->println("ERR: Outlet number not supplied");
    return;
  }
  if (args.length() > 2) {
    client->println("ERR: Outlet number too high or too many arguments");
    return;
  }

  int pin = atoi(&args[0]);
  int realPin = (atoi(&args[0])) -1;

  if (pin > numOfOutlets || pin < 0) {
    client->println("ERR: Invalid outlet specified");
    print_prompt();
    return;
  }

  client->println();
  client->print("Setting outlet ");
  client->print(pin);
  client->println(" to OFF");

  set_outlet(realPin, 2);

  client->println("Done. ");  

  print_prompt();

}

void command_reboot(String args) {

  // reads a specified digital pin
  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  if (args.length() <= 0) {
    client->println("ERR: Outlet number not supplied");
    print_prompt();
    return;
  }
  if (args.length() > 2) {
    client->println("ERR: Outlet number too high or too many arguments");
    print_prompt();
    return;
  }

  int pin = atoi(&args[0]);
  int realPin = (atoi(&args[0])) -1;

  if (pin > numOfOutlets || pin < 0) {
    client->println("ERR: Invalid outlet specified");
    print_prompt();
    return;
  }

  client->println();
  client->print("Rebooting outlet ");
  client->print(pin);
  client->print(", please wait about ");
  client->print(rebootDelay);
  client->println(" ms");

  set_outlet(realPin, 3);

  client->println("Done. ");  

  print_prompt();
}


void set_outlet(int reqpin, int power_req) {

  //int power_req = atoi(&args[0]);
  //int pin = atoi(&args[0]);

  int pin = outlets[reqpin];


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
  case 3:
    {
      if (relayType == 0) {
        digitalWrite(pin, LOW);
        delay(rebootDelay);
        digitalWrite(pin, HIGH);
      }

      else {
        digitalWrite(pin, HIGH);
        delay(rebootDelay);
        digitalWrite(pin, LOW);
      }
    }
    break;


    //something went wrong  
  default:
    {
      if (debug) Serial.println("ERR: invalid power_req, this should not happen...");
      client->println();
      client->print("ERR: invalid power_req, this should not happen...");
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
    if (cmd_index < 0) client->println("ERR: Syntax error please use a command");
  } 
  else {
    cmd_index = -1;
  }

  if (cmd_index < 0) {
    client->println();
    client->println("System Help");
    client->println("Try help <cmd> for more info");
    client->println("----------------------");
    client->println("| Available Commands |");
    client->println("----------------------");
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
  client->println("----------------------");
  client->println("| System Information |");
  client->println("----------------------");
  client->println();
  client->print("Software: ");
  client->println(progName);
  client->print("Version: ");
  client->print(_VERSION);
  client->println();

  //ip addr
  client->print("IP: ");
  client->print(ip[0]);
  client->print(".");
  client->print(ip[1]);
  client->print(".");
  client->print(ip[2]);
  client->print(".");
  client->println(ip[3]);
  //subnet mask
  client->print("Mask: ");
  client->print(subnet[0]);
  client->print(".");
  client->print(subnet[1]);
  client->print(".");
  client->print(subnet[2]);
  client->print(".");
  client->println(subnet[3]);

  //gateway
  client->print("Gateway: ");
  client->print(gateway[0]);
  client->print(".");
  client->print(gateway[1]);
  client->print(".");
  client->print(gateway[2]);
  client->print(".");
  client->println(gateway[3]);

  //mac address
  client->print("MAC: ");
  client->print(mac[0], HEX);
  client->print(":");
  client->print(mac[1], HEX);
  client->print(":");
  client->print(mac[2], HEX);
  client->print(":");
  client->print(mac[3], HEX);
  client->print(":");
  client->print(mac[4], HEX);
  client->print(":");
  client->println(mac[5], HEX);

  //hostname
  client->print("Hostname: ");
  client->println(hostname);

  //number of outlets
  client->print("Number of outlets: ");
  client->println(numOfOutlets);

  //reboot delay
  client->print("Reboot delay (ms): ");
  client->println(rebootDelay);


  print_prompt(); 

}


//user requests quit
void command_quit(String args) {
  // this method closes down the network connection.
  client->println("");
  client->println("Goodbye...");
  client->println();
  ethcl->stop();
}

/* 
 
 //user wanted to reset controller
 void command_reset(String args) {
 client->println("");
 client->println("Resetting controler.");
 client->println("Goodbye...");
 client->println();
 ethcl->stop();
 
 digitalWrite(resetPin, LOW);
 
 
 }
 */


