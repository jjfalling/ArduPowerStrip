/*
ARDUPOWERSTRIP - JJFALLING ©2012
 https://github.com/jjfalling/ArduPowerStrip
 Published under GNU GPL.
 
 
 Much of this is based off of this: https://github.com/ajfisher/arduino-command-server
 Also where I found the split library
 Flash library found here: http://arduiniana.org/libraries/flash/
 
 
 There are a few quirks and random issues. They might be caused by a memory leak, not sure...
 
 */

//################## 
//User settings
//################## 

//Network info
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] =   { 192,  168,  15, 17 };
byte gateway[] = { 192,  168,  15, 1 };
byte subnet[] = { 255, 255, 255, 0 };

//Hostname
const char hostname[] = "APS-rpc1";

//You need to define the type of relay you are using or how you have it wired.
//NC is 1, NO is 0.
const int relayType = 1;    

//What digital pins are your outlets attached to (outlet1 is the first pin listed, outlet2 is the second pin, etc)?
const int outlets[] = { 7,8};

//How long should the delay between off and on during a reboot be (in milliseconds)?
const int rebootDelay = 3000;

//Enable serial debug? 
boolean debug = true;


//################## 
//End user settings
//################## 

//start global section

#include <SPI.h>
#include <Ethernet.h>
#include <split.h>
#include <Flash.h>
#include <avr/wdt.h>

//disable watchdog, this is needed on newer chips to prevent a reboot loop. However I can't test this...
void wdt_init(void)
{
  MCUSR = 0;
  wdt_disable();

  return;
}

//program name and version
#define _NAME "ArduPowerStrip"
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

//FIX: Need to make this automatic:
#define MAX_COMMANDS 8
Command com[MAX_COMMANDS]={
};

//Don't know why, but for some reason I have to divide by 2 to get the real number
const int numOfOutlets = (sizeof(outlets))/2;

// start server on port 23 (telnet)
EthernetServer server(23);
boolean newClient = false;

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
FLASH_STRING(error_4,"ERR: invalid power_req, this should not happen...\n");
FLASH_STRING(error_5,"ERR: Syntax error please use a valid command\n");
FLASH_STRING(set1,"Setting outlet ");
FLASH_STRING(set2," to ");
FLASH_STRING(off1,"OFF\n");
FLASH_STRING(on1,"OFF\n");
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
FLASH_STRING(reset1,"Resetting controler.\n");
FLASH_STRING(exit1,"Closing connection. Goodbye...\n");

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


  com[0]=(Command){"HELP", "Prints this. Try HELP <CMD> for more", command_help      };
  com[1]=(Command){"INFO", "Shows system information", command_info      };
  com[2]=(Command){"SHOW", "Shows the status of a outlet", command_status      };
  com[3]=(Command){"ON", "Sets an outlet to on", command_on      };
  com[4]=(Command){"OFF", "Sets an outlet to off", command_off      };
  com[5]=(Command){"REBOOT", "Reboots an outlet", command_reboot      };
  com[6]=(Command){"QUIT", "Quits this session gracefully", command_quit      };
  com[7]=(Command){"RESET", "Preform a software reset on this device (and resets ALL relays!)", command_reset  };
  //com[8]=(Command){"SET", "Set system params (maybe?)", command_set};


}




void loop() {

  eclient = server.available();

  if (eclient) {
    if (debug) Serial.println("User connected");
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

    newClient = true;
    eclient.flush();

  // run the memory test function and print the results to the serial port
  int result = memoryTest();
  
  Serial.print("Bytes free ");
  Serial.println(result,DEC);


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
      if (debug) Serial.println("User disconnected");
    }
  }

}




//############################
//start defining commands

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
  
  if (args.length() <= 0) {
    eclient << error_1;
    print_prompt();
    return;
  }
  if (args.length() > 2) {
    eclient << error_2;
    print_prompt();
    return;
  }

  if (pin > numOfOutlets || pin < 0) {
    eclient << error_3;
    print_prompt();
    return;
  }
  
  int realPin = pin -1;
  realPin = outlets[realPin];
  

  if (pin > numOfOutlets || pin < 0) {
    eclient << error_3;
    print_prompt();
    return;
  }

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

void command_on(String args) {
  
  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  int pin = atoi(&args[0]);
  
  if (args.length() <= 0) {
    eclient << error_1;
    print_prompt();
    return;
  }
  if (args.length() > 2) {
    eclient << error_2;
    print_prompt();
    return;
  }

  if (pin > numOfOutlets || pin < 0) {
    eclient << error_3;
    print_prompt();
    return;
  }
  
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

void command_off(String args) {

  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  int pin = atoi(&args[0]);
  
  if (args.length() <= 0) {
    eclient << error_1;
    print_prompt();
    return;
  }
  if (args.length() > 2) {
    eclient << error_2;
    print_prompt();
    return;
  }

  if (pin > numOfOutlets || pin < 0) {
    eclient << error_3;
    print_prompt();
    return;
  }
  
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

void command_reboot(String args) {

  // argument passed in should simply be a number and it's that one we read.
  // we do need to get both chars though because it can be 2 digits
  int pin = atoi(&args[0]);
  
  if (args.length() <= 0) {
    eclient << error_1;
    print_prompt();
    return;
  }
  if (args.length() > 2) {
    eclient << error_2;
    print_prompt();
    return;
  }

  if (pin > numOfOutlets || pin < 0) {
    eclient << error_3;
    print_prompt();
    return;
  }
  
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

  //ip addr
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
  ethcl->stop();

  wdt_enable(WDTO_30MS);
  while(1) {
  };


}

//This bit from http://www.faludi.com/itp/arduino/Arduino_Available_RAM_Test.pde:
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



