
//    AM2315C
//                    +-----------------+
//    RED    -------- | VDD             |
//    YELLOW -------- | SDA    AM2315C  |
//    BLACK  -------- | GND             |
//    WHITE  -------- | SCL             |
//                    +-----------------+


#include "AM2315C.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <SD.h>

                                                              // change this to match your SD shield or module;
                                                              // Default SPI on Uno and Nano: pin 10
const int chipSelect = 4;                                     // Arduino Ethernet shield: pin 4
                                                              // Adafruit SD shields and modules: pin 10
                                                              // Sparkfun SD shield: pin 8
                                                              // MKR Zero SD: SDCARD_SS_PIN



#define ONE_WIRE_BUS_1 2                                      //hőmérők beállítása
#define ONE_WIRE_BUS_2 3

OneWire oneWire_in(ONE_WIRE_BUS_1);
OneWire oneWire_out(ONE_WIRE_BUS_2);

DallasTemperature sensor_inhouse(&oneWire_in);
DallasTemperature sensor_outhouse(&oneWire_out);

AM2315C DHT;                                                  //pára érzékelő 

File myFile;                                                  //sd kártya

const byte NUMBER_OF_RECORDS = 30;                   //paraméterek száma
char parameterArray[NUMBER_OF_RECORDS][30];         
char aRecord[30];                                   //maxhossza
byte recordNum;
byte charNum;

float elpartemp,elpartempkor,elpartempbeal,kamratemp,kamratempkor,kamratempbeal,kamratemphiszt;
float kamrapara,kamraparakor,kamraparabeal,kamraparahiszt;
float temperzekelo1,temperzekelo2,hum1;
int szaritido,szaritidobeal,erlel1ido,erlel1idobeal,erlel1szunetido,erlel2idobeal,erlel2szunetido;

int uzem,kompuzem,leolvido,min;
int temp2,hum2,tment;
String dataString = "";
uint8_t count = 0;
float templeker();
float paraleker();


void setup()
{
  pinMode(31, OUTPUT);                //kompreszor relé
  pinMode(33, OUTPUT);                //ventill relé
  pinMode(33, OUTPUT);                //leolv fütés relé
  pinMode(35, OUTPUT);                //fütés relé
  pinMode(37, OUTPUT);
  digitalWrite(31, LOW);             //kompreszor relé
  digitalWrite(33, LOW);             //ventill relé
  digitalWrite(35, LOW);             //leolv fütés relé
  digitalWrite(37, LOW);             //fütés relé

  Serial.begin(115200);
  Serial1.begin(9600);
  sensor_inhouse.begin();
  sensor_outhouse.begin();
 
 if (!SD.begin(chipSelect)) {
    Serial.println("initialization failed. Things to check:");
    Serial.println("1. is a card inserted?");
    Serial.println("2. is your wiring correct?");
    Serial.println("3. did you change the chipSelect pin to match your shield or module?");
    Serial.println("Note: press reset button on the board and reopen this Serial Monitor after fixing your issue!");
    hibajelzes();
    while (true);
  }

  Serial.println("initialization done.");

  Wire.begin();
  DHT.begin();
  uzem=1;
// myFile = SD.open("datalog.txt");                                                       //datalog kiolvasása 
 // if (myFile) {
 //   Serial.println("datalog.txt");

 //   // read from the file until there's nothing else in it:
 //   while (myFile.available()) {
 //     Serial.write(myFile.read());
 //   }
 //   // close the file:
//    myFile.close();
 // } else {
    // if the file didn't open, print an error:
 //   Serial.println("error opening datalog.txt");
  //}
  myFile = SD.open("params.txt");                                                           //paraméterek kiolvasása
  if (myFile)
  {
    Serial.println("params.txt:");
    while (myFile.available())
    {
      char inChar = myFile.read();  //get a character
      if (inChar == '\n') //if it is a newline
      {
        parseRecord(recordNum);
        recordNum++;
        charNum = 0;  //start again at the beginning of the array record
      }
      else
      {
        aRecord[charNum] = inChar;  //add character to record
        charNum++;  //increment character index
        aRecord[charNum] = '\0';  //terminate the record
      }
    }
    myFile.close();
  }
  else
  {
    Serial.println("error opening params.txt");
    hibajelzes();
  }
  printParameterArray();

}


void loop(){

   if (Serial1.available())                   // megnézi jötte adat a kijelzötöl
  {                   
    int inByte = Serial1.read();
  Serial.print(inByte);
  }

  if (millis() - DHT.lastRead() >= 1000)      //  megnézi  mikor kel lekérdezni az érzékelőket
  {
  
    min=min+1;
    Serial.print("********üzemidö******");
    Serial.println(min);
    kiirnextion ();

  templeker();                               // hőmérők lekérdezése  
  
  paraleker();                               // páraérzékelő lekérdezése

  uzemmodv();                                  // megnézi milyen üzemmodban van

  tment=tment+1;

    if (tment>10)
    {                             // naplózás figyelése
    datament();
    tment=0;
    }
  
  kiirnextion ();                             //kijelző irása
  }
}






float templeker()                           //hömérsékletek lekérdezése és korrekciója
{ 

    sensor_inhouse.requestTemperatures();
    sensor_outhouse.requestTemperatures();
    //Serial.println(" done");
    temperzekelo1=sensor_inhouse.getTempCByIndex(0);
    temperzekelo2=sensor_outhouse.getTempCByIndex(0);
    Serial.println("hömérséklet lekérdezés a 18ds20");
    Serial.print("sensor 1=");
    Serial.println(temperzekelo1);
    Serial.print("sensor 2=");
    Serial.println(temperzekelo2);
    Serial.println("lekér vége");
    elpartemp=(temperzekelo1+elpartempkor);
    kamratemp=(temperzekelo2+kamratempkor);
}

float paraleker ()                            //páraérzékelő lekérdezése és korrekcioja
{
  int status = DHT.read();
    hum1=DHT.getHumidity();
    //temp1=DHT.getTemperature();
    Serial.print("AM2315C DHT=");
    Serial.print(hum1);
    Serial.println("%    ");
   kamrapara=(hum1+kamraparakor);

       switch (status)
    {
      case AM2315C_OK:
       // Serial.print("OK");
        break;
      case AM2315C_ERROR_CHECKSUM:
        Serial.print("Checksum error");
        hibajelzes();
       break;
      case AM2315C_ERROR_CONNECT:
        Serial.print("Connect error");
        hibajelzes();
        break;
      case AM2315C_MISSING_BYTES:
        Serial.print("Missing bytes");
         hibajelzes();
        break;
      case AM2315C_ERROR_BYTES_ALL_ZERO:
        Serial.print("All bytes read zero");
        hibajelzes();
      case AM2315C_ERROR_READ_TIMEOUT:
        Serial.print("Read time out");
         hibajelzes();
        break;
      case AM2315C_ERROR_LASTREAD:
        Serial.print("Error read too fast");
         hibajelzes();
        break;
      default:
        Serial.print("Unknown error");
         hibajelzes();
        break;
    


}
}

void datament ()
{
dataString = "";
dataString += String(elpartemp);
dataString += ",";
dataString += String(kamratemp);
dataString += ",";
//dataString += String(temp1);
//dataString += ",";
dataString += String(hum1);
//dataString += "%";
  
File dataFile = SD.open("datalog.txt", FILE_WRITE);

 if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    // print to the serial port too:
   Serial.println(dataString);
   Serial.println("Mentés ok");
    }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening datalog.txt");
    hibajelzes();
   }
}


void kiirnextion ()
{
  int hom,par;
  hom=(kamratemp*100);
  par=(kamrapara*100);
Serial.println();
      //Serial1.println("nextion");
      //Serial1.println("");
      Serial1.write(0xff);
      Serial1.write(0xff);
      Serial1.write(0xff);
      Serial1.write("page0.x0.val=");
      Serial1.print(hom);
      Serial1.write(0xff);
      Serial1.write(0xff);
      Serial1.write(0xff);
      //Serial1.println("");
      Serial1.write("page0.x1.val=");
      Serial1.print(par);
      Serial1.write(0xff);
      Serial1.write(0xff);
      Serial1.write(0xff);
      Serial1.print("page0.t9.txt=");
      Serial1.write(34);
      Serial1.print(uzem);
      Serial1.write(34);
      Serial1.write(0xff);
      Serial1.write(0xff);
      Serial1.write(0xff);
    //  Serial.println(hom);
      //Serial.println(par);
      Serial.println(uzem);
      Serial.println("nextion ki irás ok");
}

void parseRecord(byte index)
{
  char * ptr;
  ptr = strtok(aRecord, " = ");  //find the " = "
  ptr = strtok(NULL, ""); //get remainder of text
  strcpy(parameterArray[index], ptr + 2); //skip 2 characters and copy to array
}

void printParameterArray()                                        //az sd-n lévőparameterek kiirása
{
  Serial.println("\nparameter values from the array");
  for (int index = 0; index < NUMBER_OF_RECORDS; index++)
  {
    Serial.print("parameterArray[");
    Serial.print(index);
    Serial.print("] = ");
    Serial.println(parameterArray[index]);

    Serial.println("");
  }
  kamratempbeal = atof (parameterArray[0]);                        // adott paraméter betöltése
  kamratemphiszt= atof(parameterArray[1]);
  kamratempkor= atof(parameterArray[2]);



}

void hibajelzes()
{


}

void uzemmodv()
{
    switch(uzem=1)
  { 
    case 1:hutes();
    break;
    case 2:szarit();
    break;
    case 3:erlel1();
    break;
    case 4:erlel2();
    break;
    //case 5:fustol();
    //break;
  }
}

void hutes()
{
  uzem=1;
  
if (kamratemp>(kamratempbeal+kamratemphiszt) )  // hőmérséklet vizsgál ha magas kopresszor venill be
{
    kompbe();
    ventbe();
}
if (kamratemp<(kamratempbeal-kamratemphiszt)) //hőmérséklet vizsgál ha alacsony kopresszor venill ki
{
  kompki();
}
}

void szarit()
{
  if (szaritido<szaritidobeal)
  {
  ventbe();
  if (kamratemp>(kamratempbeal+kamratemphiszt) ){kompbe();}
  if (kamratemp<(kamratempbeal-kamratemphiszt)){kompki();}
  if (kamrapara<(kamraparabeal+kamraparahiszt)){kamrafutbe();}
  if (kamrapara>(kamraparabeal+kamraparahiszt)){kamrafutki();}
  }
  else erlel1();
}

void erlel1()
{
if (erlel1ido<erlel1idobeal)
{
  ventbe();
  if (kamratemp>(kamratempbeal+kamratemphiszt) ){kompbe();}
  if (kamratemp<(kamratempbeal-kamratemphiszt)){kompki();}
  if (kamrapara<(kamraparabeal+kamraparahiszt))
    {
    if(kamratemp<(kamratempbeal+10)){kamrafutbe();}
    else {kamrafutki();}
    if (kamrapara>(kamraparabeal+kamraparahiszt)){kamrafutki();}
    }

}
}
void erlel2(){}

//void fustol(){}

void kompbe()
{
  digitalWrite (33,HIGH);
}
void kompki(){

}
void ventbe()
{
  digitalWrite(35,HIGH);
}
void ventki(){}
void kamrafutbe(){}
void kamrafutki(){}
void leolv()
{
 int leolvszamlo;
  //kompki();
  //ventki();
  //if (lolvido>lolvszamlo)             kopresszor üzemidö>leovidobeál és kisseb a beálitott elpártem nél akko olvsz a megadott idöig vany a hömerskletig
 // {if (tempelpar<tempelparp)         se kopresszor se venill
 // {
 //   elparfutbe();
 //   leolvszamlo=leolvszamlo+1;
 // }
 // else{elparfutki()}
  
 // }
}
