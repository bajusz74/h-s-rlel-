#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <Nextion.h>
#include "AM2315C.h"
#include <OneWire.h>
#include <DallasTemperature.h>



#define ONE_WIRE_BUS 27 // Change this to the actual pin used
#define dbSerial Serial    
#define nexSerial Serial1   // A Nextion HMI csatlakoztatása az 1-es soros portra

// DS18B20 sensor pin and setup

RTC_DS1307 rtc; // DS1307 RTC objektum
AM2315C DHT;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);



const int pinCooling = 2;
const int pinFan = 3;
const int pinHeating = 4;
const int pinDefrost = 5;
const int pinError = 6;

unsigned long defrostStartTime = 0;
unsigned long coolingStartTime = 0;
unsigned long coolingElapsed = 0;  // Track how long cooling has been running
unsigned long agingStartTime = 0;
unsigned long aging2StartTime = 0;
unsigned long lastWaveformUpdate = 0;
bool isAging2PhaseOn = true;
bool isCooling = false;
bool isDefrosting = false;
bool isAgingPhaseOn = true;



// user parameter
String errorMessage;
String cooltemp, drytemp, agetemp, age2temp;
String dryhum, agehum, age2hum, drytime, agetime, age2time;
float coolTemp, dryTemp, ageTemp, age2Temp;
int dryHum, ageHum, age2Hum;
int dryTime, ageTime, age2Time;

// setting parameter
String tempcomp, humcomp, temphist, humhist, deftime1, deftime2, deftemp;
String ageontime, ageofftime, age2ontime, age2offtime;  
float tpcomp, hmcom, tphist, hmhist, dftemp;
int ageont, ageofft, age2ont, age2offt, dftime1, dftime2;

float compensatedTemperature = 0.0;
float compensatedHumidity = 0.0;

int year, month, day, hour, minute;
const int chipSelect = 4; // SD kártya csatlakoztatásának lába
File dataFile;
unsigned long totalHours;
uint8_t ndt[3]= {255,255,255};
enum State {
    COOLING,
    DRYING,
    AGING1,
    AGING2,
    DEFROST
};

State currentState = COOLING;
State previousState;
// Indítás ideje
DateTime startTime;

// Nextion Components
NexButton btnStart = NexButton(0, 20, "btnStart");
NexButton btnStop = NexButton(0, 2, "btnStop");
NexButton btnSaveUserp = NexButton(1, 5, "btnSaveUserp");
NexButton btnSavesetp = NexButton(1, 5, "btnSavesetp");
NexButton btnMenu1 = NexButton(0, 3, "btnMenu1");
NexButton btnMenu2 = NexButton(0, 11, "btnMenu2");
NexButton btnMenu3 = NexButton(0, 12, "btnMenu3");
NexButton btnMenu4 = NexButton(0, 13, "btnMenu4");
NexButton btnMenu5 = NexButton(0, 14, "btnMenu5");
NexButton btnMenu6 = NexButton(0, 15, "btnMenu6");
NexButton btnSetRTC = NexButton(0, 16, "btnSetRTC");


// Nextion gombok eseménykezelő listája
NexTouch *nex_listen_list[] = {
    &btnStart,
    &btnStop,
    &btnSaveUserp,
    &btnSavesetp,
    &btnMenu1,
    &btnMenu2,
    &btnMenu3,
    &btnMenu4,
    &btnMenu5,
    &btnMenu6,
    &btnSetRTC,
    NULL
};


void setup() {
    dbSerial.begin(9600);
    nexSerial.begin(9600);
    nexInit();
    Wire.begin();
    DHT.begin();
    sensors.begin();
    pinMode(pinCooling, OUTPUT);
    pinMode(pinFan, OUTPUT);
    pinMode(pinHeating, OUTPUT);
    pinMode(pinDefrost, OUTPUT);
    pinMode(pinError, OUTPUT);

    digitalWrite(pinCooling, LOW);
    digitalWrite(pinFan, LOW);
    digitalWrite(pinHeating, LOW);
    digitalWrite(pinDefrost, LOW);
    digitalWrite(pinError, LOW);

    if (!rtc.begin()) {
        dbSerial.println("Couldn't find DS1307 RTC");
        while (1);
    }

    if (!SD.begin(chipSelect)) {
        dbSerial.println("SD Card initialization failed!");
        return;
    }
    
    dbSerial.println("SD Card initialized.");

     // Attach the new buttons to their callback functions
    btnStart.attachPop(startButtonCallback);
    btnStop.attachPop(stopButtonCallback);
    btnSaveUserp.attachPop(UserParameterSaveCallback);
    btnSavesetp.attachPop(SettingParameterSaveCallback);
    btnMenu1.attachPop(menu1ButtonCallback);
    btnMenu2.attachPop(menu2ButtonCallback);
    btnMenu3.attachPop(menu3ButtonCallback);
    btnMenu4.attachPop(menu4ButtonCallback);
    btnMenu5.attachPop(menu5ButtonCallback);
    btnMenu6.attachPop(menu6ButtonCallback);
    btnSetRTC.attachPop(setRTCButtonCallback);


    loadsettingparameter();
    loadparameter();
    loadProcessState();
    callpage0();

}

void loop() {
    DateTime now = rtc.now();
    updateNextionTextbox("page0.ev", static_cast<int>(now.year()));
    updateNextionTextbox("page0.honap", static_cast<int>(now.month()));
    updateNextionTextbox("page0.nap", static_cast<int>(now.day()));
    updateNextionTextbox("page0.ora", static_cast<int>(now.hour()));
    updateNextionTextbox("page0.perc", static_cast<int>(now.minute()));

    if (millis() - DHT.lastRead() >= 1000) {
        int status = DHT.read();
        if (status == AM2315C_OK) {
            float humidity = DHT.getHumidity();
            float temperature = DHT.getTemperature();

            compensatedHumidity = humidity + hmcom;
            compensatedTemperature = temperature + tpcomp;

            updateNextionTextbox("page0.temp", compensatedTemperature);
            updateNextionTextbox("page0.hum", compensatedHumidity);
            if (millis() - lastWaveformUpdate >= 120000) { // 300000 ms = 5 minutes
                updateWaveform(compensatedTemperature, compensatedHumidity);
                lastWaveformUpdate = millis();
            }

            dbSerial.print("Temperature: ");
            dbSerial.print(compensatedTemperature);
            dbSerial.print(" °C, Humidity: ");
            dbSerial.print(compensatedHumidity);
            dbSerial.println(" %");

            switch (currentState) {
                case COOLING:
                    performCooling();
                    break;
                case DRYING:
                    performDrying();
                    checkTransitionToAging1(now);
                    break;
                case AGING1:
                    performAging1();
                    checkTransitionToAging2(now);
                    break;
                case AGING2:
                    performAging2();
                    checkCompletion(now);
                    break;
                case DEFROST:
                    defrost();
                    break;
            }
        } else {
            handleSensorError(status);
        }
    }

    nexLoop(nex_listen_list);
    while (nexSerial.available()) {
        String command = nexSerial.readStringUntil('\n');
        processCommand(command);
    }
}

void checkTransitionToAging1(DateTime now) {
    dbSerial.print("Elapsed hours in DRYING: ");
    dbSerial.println(elapsedTimeInMinutes(now, startTime));
    if (elapsedTimeInMinutes(now, startTime) >= dryTime) {
        dbSerial.println("Transitioning to AGING1...");
        currentState = AGING1;
        startTime = now;
        saveProcessState();
    }
}

void checkTransitionToAging2(DateTime now) {
    dbSerial.print("Elapsed hours in AGING1: ");
    dbSerial.println(elapsedTimeInMinutes(now, startTime));
    if (elapsedTimeInMinutes(now, startTime) >= ageTime) {
        dbSerial.println("Transitioning to AGING2...");
        currentState = AGING2;
        startTime = now;
        saveProcessState();
    }
}

void checkCompletion(DateTime now) {
    dbSerial.print("Elapsed hours in AGING2: ");
    dbSerial.println(elapsedTimeInMinutes(now, startTime));
    if (elapsedTimeInMinutes(now, startTime) >= age2Time) {
        dbSerial.println("Az érlelés befejeződött");
    }
}

void handleSensorError(int status) {
    String errorMessage;
    switch (status) {
        case AM2315C_ERROR_CHECKSUM:
            errorMessage = "Checksum error";
            break;
        case AM2315C_ERROR_CONNECT:
            errorMessage = "Connect error";
            break;
        case AM2315C_MISSING_BYTES:
            errorMessage = "Missing bytes";
            break;
        case AM2315C_ERROR_BYTES_ALL_ZERO:
            errorMessage = "All bytes read zero";
            break;
        case AM2315C_ERROR_READ_TIMEOUT:
            errorMessage = "Read timeout";
            break;
        case AM2315C_ERROR_LASTREAD:
            errorMessage = "Error: read too fast";
            break;
        default:
            errorMessage = "Unknown error";
            break;
    }
    updateMessageDisplay(errorMessage);
    dbSerial.print("Sensor error: ");
    dbSerial.println(errorMessage);
}

void loadparameter() {
    dataFile = SD.open("userparm.txt");
    if (dataFile) {
        while (dataFile.available()) {
            String line = dataFile.readStringUntil('\n');
            parseLine(line);
        }
        dataFile.close();
    } else {
        Serial.println("Nem tudtam megnyitni a fájlt!");
    }


}

void parseLine(String line) {
    int commaIndex = line.indexOf(':');
    String key = line.substring(0, commaIndex);
    String value = line.substring(commaIndex + 1);

    if (key == "CoolTemp") coolTemp = value.toFloat();
    else if (key == "DryTemp") dryTemp = value.toFloat();
    else if (key == "AgeTemp") ageTemp = value.toFloat();
    else if (key == "Age2Temp") age2Temp = value.toFloat();
    else if (key == "DryHum") dryHum = value.toInt();
    else if (key == "AgeHum") ageHum = value.toInt();
    else if (key == "Age2Hum") age2Hum = value.toInt();
    else if (key == "DryTime") dryTime = value.toInt();
    else if (key == "AgeTime") ageTime = value.toInt();
    else if (key == "Age2Time") age2Time = value.toInt();
    else if (key == "Tempcomp") tpcomp = value.toFloat();
    else if (key == "Humcomp") hmcom = value.toFloat();
    else if (key == "Temphiszt") tphist = value.toFloat();
    else if (key == "Humhiszt") hmhist = value.toFloat();
    else if (key == "Deftime1") dftime1 = value.toInt();
    else if (key == "Deftime2") dftime2 = value.toInt();
    else if (key == "Deftemp") dftemp = value.toFloat();
    else if (key == "Ageontime") ageont = value.toInt();
    else if (key == "Ageofftime") ageofft = value.toInt();
    else if (key == "Age2onTime") age2ont = value.toInt();
    else if (key == "Age2offTime") age2offt = value.toInt();

    else Serial.println("Unknown parameter: " + key);
}

void startButtonCallback(void *ptr) {
    if (currentState == COOLING) {
        currentState = DRYING;
        startTime = rtc.now();
        saveProcessState();
        dbSerial.println("Starting drying...");
    }
}

void stopButtonCallback(void *ptr) {
    if (currentState != COOLING) {
        currentState = COOLING;
        saveProcessState();
        callpage0();

        dbSerial.println("Stopping process and returning to cooling...");
    }
}

void UserParameterSaveCallback(void *ptr) {
    cooltemp = getStringValue("print cooltemp.txt");
    drytemp = getStringValue("print drytemp.txt");
    agetemp = getStringValue("print agetemp.txt");
    age2temp = getStringValue("print age2temp.txt");
    dryhum = getStringValue("print dryhum.txt");
    agehum = getStringValue("print agehum.txt");
    age2hum = getStringValue("print age2hum.txt");
    drytime = getStringValue("print drytime.txt");
    agetime = getStringValue("print agetime.txt");
    age2time = getStringValue("print age2time.txt");

    saveuserparameterToSDCard();
    callpage0();
    
    //Serial1.print("page page0");
    //Serial1.write(0xFF);
    //Serial1.write(0xFF);
    //Serial1.write(0xFF);
}

void SettingParameterSaveCallback(void *ptr) {
   // 
    tempcomp = getStringValue("print tempcomp.txt");
    humcomp = getStringValue("print humcomp.txt");
    temphist = getStringValue("print temphist.txt");
    humhist = getStringValue("print humhist.txt");
    deftime1 = getStringValue("print deftime1.txt");
    deftime2 = getStringValue("print deftime2.txt");
    deftemp = getStringValue("print deftemp.txt");
    ageontime = getStringValue("print ageontime.txt");
    ageofftime = getStringValue("print ageofftime.txt");
    age2ontime = getStringValue("print age2ontime.txt");
    age2offtime = getStringValue("print age2offtime.txt");
  
    savesettingparameterToSDCard();
    callpage0();

    //Serial1.print("page page0");
    //Serial1.write(0xFF);
    //Serial1.write(0xFF);
    //Serial1.write(0xFF);
}

String getStringValue(String command) {
    Serial1.print(command);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    delay(200);

    String response = "";
    while (Serial1.available()) {
        char c = Serial1.read();
        response += c;
    }

    Serial.print("Received response for "); 
    Serial.print(command);
    Serial.print(": ");
    Serial.println(response);
    
    response.trim();
    return response;
}

void savesettingparameterToSDCard() {
    SD.remove("settparm.txt");
    File file = SD.open("settparm.txt", FILE_WRITE);
    if (file) {
        file.print("Tempcomp:"); file.println(tempcomp);
        file.print("Humcomp:"); file.println(humcomp);
        file.print("Temphiszt:"); file.println(temphist);
        file.print("Humhiszt:"); file.println(humhist);
        file.print("Deftime1:"); file.println(deftime1);
        file.print("Deftime2:"); file.println(deftime2);
        file.print("Ageontime:"); file.println(ageontime);
        file.print("Ageofftime:"); file.println(ageofftime); 
        file.print("Age2onTime:"); file.println(age2ontime);
        file.print("Age2offTime:"); file.println(age2offtime);
        file.close();
        Serial.println("Setting parameters saved to SD card.");
        loadsettingparameter();
    } else {
        Serial.println("Error opening file!");
    }
}

void saveuserparameterToSDCard() {
    SD.remove("userparm.txt");
    File file = SD.open("userparm.txt", FILE_WRITE);
    if (file) {
        file.print("CoolTemp:"); file.println(coolTemp);
        file.print("DryTemp:"); file.println(dryTemp);
        file.print("AgeTemp:"); file.println(ageTemp);
        file.print("Age2Temp:"); file.println(age2Temp);
        file.print("DryHum:"); file.println(dryHum);
        file.print("AgeHum:"); file.println(ageHum);
        file.print("Age2Hum:"); file.println(age2Hum);
        file.print("DryTime:"); file.println(dryTime);
        file.print("AgeTime:"); file.println(ageTime);
        file.print("Age2Time:"); file.println(age2Time);
        file.close();
        Serial.println("User parameters saved to SD card.");
    } else {
        Serial.println("Error saving user parameters to SD card!");
    }
}

void loadsettingparameter() {
    File file = SD.open("settparm.txt", FILE_READ);
    if (file) {
        Serial.println("Loading setting parameters...");
        while (file.available()) {
            String line = file.readStringUntil('\n');
            parseLine(line); // or specific handling per parameter
        }
        file.close();
        Serial.println("Setting parameters loaded.");
    } else {
        Serial.println("Couldn't find setting file.");
    }
}

void processCommand(String command) {
    if (command.indexOf("Start") != -1) {
        Serial.println("start");
        startButtonCallback(NULL);
    } else if (command.indexOf("Stop") != -1) {
        Serial.println("stop");
        stopButtonCallback(NULL);
    } else if (command.indexOf("upsave") != -1) {
        Serial.println("upsave");
        UserParameterSaveCallback(NULL);
    } else if (command.indexOf("spsave") != -1){
        Serial.println("spsave");
        SettingParameterSaveCallback(NULL);
    } else if (command.indexOf("menu1") != -1){
        Serial.println("menu1");
        menu1ButtonCallback(NULL);
    } else if (command.indexOf("menu2") != -1){
        Serial.println("menu2");
        menu2ButtonCallback(NULL);
    } else if (command.indexOf("idobeall") != -1){
        Serial.println("dátum idő beállítás");
        setRTCButtonCallback(NULL);
    }
}

unsigned long elapsedTimeInMinutes(DateTime now, DateTime start) {
    unsigned long totalMinutes = 0;
    totalMinutes += (now.year() - start.year()) * 365 * 24 * 60;
    totalMinutes += (now.month() - start.month()) * 30 * 24 * 60;
    totalMinutes += (now.day() - start.day()) * 24 * 60;
    totalMinutes += (now.hour() - start.hour()) * 60;
    totalMinutes += (now.minute() - start.minute());

    //dbSerial.print("Time calculation -> ");
    //dbSerial.print("Years in minutes: "); dbSerial.print((now.year() - start.year()) * 365 * 24 * 60); 
    //dbSerial.print(", Months in minutes: "); dbSerial.print((now.month() - start.month()) * 30 * 24 * 60);
    //dbSerial.print(", Days in minutes: "); dbSerial.print((now.day() - start.day()) * 24 * 60);
    //dbSerial.print(", Hours in minutes: "); dbSerial.print((now.hour() - start.hour()) * 60);
    //dbSerial.print(", Minutes: "); dbSerial.println(now.minute() - start.minute());

    return totalMinutes;
}

void saveProcessState() {
    SD.remove("state.txt");
    File file = SD.open("state.txt", FILE_WRITE);
    if (file) {
        file.println(currentState);

        file.print(startTime.year()); file.print(",");
        file.print(startTime.month()); file.print(",");
        file.print(startTime.day()); file.print(",");
        file.print(startTime.hour()); file.print(",");
        file.print(startTime.minute()); 

        file.close();
        dbSerial.println("Process state saved.");
    } else {
        dbSerial.println("Failed to open file for writing");
    }
}

void loadProcessState() {
    File file = SD.open("state.txt", FILE_READ);
    if (file) {
        dbSerial.println("Loading process state...");
        String line = file.readStringUntil('\n');
        currentState = static_cast<State>(line.toInt());

        int year, month, day, hour, minute;
        line = file.readStringUntil('\n');
        sscanf(line.c_str(), "%d,%d,%d,%d,%d", &year, &month, &day, &hour, &minute);
        
        startTime = DateTime(year, month, day, hour, minute);

        dbSerial.println("Loaded process state.");
        file.close();
    } else {
        dbSerial.println("No process state file found, starting fresh.");
    }
}
void menu1ButtonCallback(void *ptr) {
    dbSerial.println("A 'Menu 1' gomb meg lett nyomva.");

    // Oldal megnyitása
    Serial1.print("page userparam"); 
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    
    // Hőmérsékleti (float) paraméterek
    updateNextionTextbox("userparam.cooltemp", coolTemp);
    updateNextionTextbox("userparam.drytemp", dryTemp);
    updateNextionTextbox("userparam.agetemp", ageTemp);
    updateNextionTextbox("userparam.age2temp", age2Temp);
    
    // Páratartalom és idő (int) paraméterek
    updateNextionTextbox("userparam.dryhum", dryHum);
    updateNextionTextbox("userparam.agehum", ageHum);
    updateNextionTextbox("userparam.age2hum", age2Hum);
    updateNextionTextbox("userparam.drytime", dryTime);
    updateNextionTextbox("userparam.agetime", ageTime);
    updateNextionTextbox("userparam.age2time", age2Time);
    
    Serial.println("A felhasználói paraméterek frissítésre kerültek a 'userparam' oldalon.");
}

void updateNextionTextbox(String componentName, float value) {
    Serial1.print(componentName + ".txt=");
    Serial1.write(0x22);
    Serial1.print(value, 1); // egy tizedesjegy pontosság a float értékeknél
    Serial1.write(0x22);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
}

void updateNextionTextbox(String componentName, int value) {
    Serial1.print(componentName + ".txt=");
    Serial1.write(0x22);
    Serial1.print(value);
    Serial1.write(0x22);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
}

void updateNextionTextbox(String componentName, String value) {
    Serial1.print(componentName + ".txt=");
    Serial1.write(0x22);
    Serial1.print(value); 
    Serial1.write(0x22);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
}

void updateWaveform(float temperature, float humidity) {
    // Channel 0 for temperature
    Serial1.print("add 19,0,");
    Serial1.print((int)temperature);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);

    // Channel 1 for humidity
    Serial1.print("add 19,1,");
    Serial1.print((int)humidity);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
}

void menu2ButtonCallback(void *ptr) {
    dbSerial.println("A 'Menu 2' gomb meg lett nyomva.");

    // Oldal megnyitása
    Serial1.print("page szerviz"); 
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    
    // Beállítási paraméterek (feltételezve a típusokat)
    updateNextionTextbox("szerviz.tempcomp", tpcomp);
    updateNextionTextbox("szerviz.humcomp", hmcom);
    updateNextionTextbox("szerviz.temphist", tphist);
    updateNextionTextbox("szerviz.humhist", hmhist);
    updateNextionTextbox("szerviz.deftime1", dftime1);
    updateNextionTextbox("szerviz.deftime2", dftime2);
    updateNextionTextbox("szerviz.deftemp", dftemp);
    updateNextionTextbox("szerviz.ageontime", ageont);
    updateNextionTextbox("szerviz.ageofftime", ageofft);
    updateNextionTextbox("szerviz.age2ontime", age2ont);
    updateNextionTextbox("szerviz.age2offtime", age2offt);
    
    // Az aktuális idő kiolvasása az RTC-ből
    DateTime now = rtc.now();
    
    // Az aktuális idő betöltése a szövegboxokba, cast to int to avoid ambiguity
    updateNextionTextbox("szerviz.ev", static_cast<int>(now.year()));
    updateNextionTextbox("szerviz.honap", static_cast<int>(now.month()));
    updateNextionTextbox("szerviz.nap", static_cast<int>(now.day()));
    updateNextionTextbox("szerviz.ora", static_cast<int>(now.hour()));
    updateNextionTextbox("szerviz.perc", static_cast<int>(now.minute()));

    Serial.println("A beállítási paraméterek és az aktuális idő frissítésre kerültek a 'szerviz' oldalon.");
}



void menu3ButtonCallback(void *ptr) {
    dbSerial.println("Menu 3 button pressed.");
    // Add your functionality for menu 3 here
}

void menu4ButtonCallback(void *ptr) {
    dbSerial.println("Menu 4 button pressed.");
    // Add your functionality for menu 4 here
}

void menu5ButtonCallback(void *ptr) {
    dbSerial.println("Menu 5 button pressed.");
    // Add your functionality for menu 5 here
}

void menu6ButtonCallback(void *ptr) {
    dbSerial.println("Menu 6 button pressed.");
    // Add your functionality for menu 6 here
}

void setRTCButtonCallback(void *ptr) {
    dbSerial.println("Set RTC button pressed.");

    // A szövegboxok értékeinek kiolvasása a Nextion-ból
    int year = getStringValue("print ev.txt").toInt();
    int month = getStringValue("print honap.txt").toInt();
    int day = getStringValue("print nap.txt").toInt();
    int hour = getStringValue("print ora.txt").toInt();
    int minute = getStringValue("print perc.txt").toInt();

    // RTC frissítése az új dátummal és idővel
    rtc.adjust(DateTime(year, month, day, hour, minute, 0));

    dbSerial.println("RTC updated successfully.");
}
void callpage0(){
  Serial1.print("page page0");
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    Serial1.write(0xFF);
    //Serial.println("kezdőlap");
}
void callpagemenu1(){
  Serial1.print("page userparam");
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  //Serial.println("Menü 1");
}

void updateModeDisplay() {
    String modeText;
    switch (currentState) {
        case COOLING:
            modeText = "Cooling";
            break;
        case DRYING:
            modeText = "Drying";
            break;
        case AGING1:
            modeText = "Aging Phase 1";
            break;
        case AGING2:
            modeText = "Aging Phase 2";
            break;
        case DEFROST:
            modeText = "Defrost";
            break;
        default:
            modeText = "Ismeretlen üzemmód";
            break;
    }
    updateNextionTextbox("page0.uzem", modeText);
}



void performCooling() {
    dbSerial.println("Cooling...");

    if (compensatedTemperature >= coolTemp + tphist) {
        if (!isCooling) {
            digitalWrite(pinCooling, HIGH);
            digitalWrite(pinFan, HIGH);
            isCooling = true;
            coolingStartTime = millis();
        }
    } else if (compensatedTemperature <= coolTemp - tphist) {
        if (isCooling) {
            digitalWrite(pinCooling, LOW);
            digitalWrite(pinFan, LOW);
            isCooling = false;
            coolingElapsed += millis() - coolingStartTime;

            if (coolingElapsed > dftime1 * 60000) {
                transitionToDefrost();
            } else if (coolingElapsed < dftime2 * 60000 / 2) {
                coolingElapsed = 0;
            }
        }
    }
    updateModeDisplay();
}



void defrost() {
    dbSerial.println("Defrosting...");
    digitalWrite(pinCooling, LOW);
    digitalWrite(pinFan, LOW);
    digitalWrite(pinHeating, LOW);
    digitalWrite(pinDefrost, HIGH);

    sensors.requestTemperatures();
    float evaporatorTemp = sensors.getTempCByIndex(0); // Get evaporator temperature

    if (millis() - defrostStartTime > dftime2 * 60000 || evaporatorTemp >= dftemp) {
        // End defrost either by time or temperature threshold
        digitalWrite(pinDefrost, LOW);
        currentState = COOLING;
        isDefrosting = false;
        coolingElapsed = 0;  // Reset coolingElapsed for the next cycle
    }

    updateModeDisplay();
}

void transitionToDefrost() {
    previousState = currentState;
    currentState = DEFROST;
    defrostStartTime = millis();
    isDefrosting = true;
}

void performDrying() {
    dbSerial.println("Drying...");

    // Mindig bekapcsolva
    digitalWrite(pinFan, HIGH);

    // Hőmérséklet szabályozás
    if (compensatedTemperature >= dryTemp + tphist) {
        digitalWrite(pinCooling, HIGH);
    } else if (compensatedTemperature <= dryTemp - tphist) {
        digitalWrite(pinCooling, LOW);
    }

    // Ha a hőmérséklet túl magas
    if (compensatedTemperature > dryTemp + 10) {
        digitalWrite(pinHeating, LOW);  // Kapcsolja le a fűtést
        updateMessageDisplay("Túl magas hőmérséklet");
    } else {
        // Páratartalom szabályozás
        if (compensatedHumidity < dryHum - hmhist) {
            digitalWrite(pinHeating, HIGH);  // Kapcsolja be a fűtést, ha alacsony
        } else if (compensatedHumidity > dryHum + hmhist) {
            digitalWrite(pinHeating, LOW);   // Kapcsolja ki a fűtést, ha magas
        }
    }

    // Leolvasztás feltételeinek ellenőrzése, mint hűtés üzemmódban
    coolingElapsed += (digitalRead(pinCooling) == HIGH) ? millis() - coolingStartTime : 0;
    if (digitalRead(pinCooling) == LOW && coolingElapsed >= dftime1 * 60000) {
        transitionToDefrost();
    }

    coolingStartTime = millis();  // Frissítsük az időzítést, ha a pinCooling LOW
    updateModeDisplay();
}

void performAging1() {
    dbSerial.println("Aging Phase 1...");

    unsigned long currentMillis = millis();
    unsigned long cycleDuration = isAgingPhaseOn ? ageont * 60000 : ageofft * 60000; // 'on' vagy 'off' ciklusnak megfelelő idő

    // Check if the current phase duration has ended
    if (currentMillis - agingStartTime >= cycleDuration) {
        isAgingPhaseOn = !isAgingPhaseOn; // Toggle the phase
        agingStartTime = currentMillis; // Reset the start time for the new phase
    }

    if (isAgingPhaseOn) {
        // Ventilátor bekapcsolva
        digitalWrite(pinFan, HIGH);

        // Hőmérséklet szabályozása
        if (compensatedTemperature >= ageTemp + tphist) {
            digitalWrite(pinCooling, HIGH);
        } else if (compensatedTemperature <= ageTemp - tphist) {
            digitalWrite(pinCooling, LOW);
        }

        // Páratartalom szabályozása
        if (compensatedHumidity < ageHum - hmhist) {
            digitalWrite(pinHeating, HIGH);
        } else if (compensatedHumidity > ageHum + hmhist) {
            digitalWrite(pinHeating, LOW);
        }

        // Túl magas hőmérséklet esetén
        if (compensatedTemperature > ageTemp + 10) {
            digitalWrite(pinHeating, LOW);  // Fűtés lekapcsolása
            updateMessageDisplay("Túl magas hőmérséklet");
        }
        
    } else {
        // Off phase: all elements turned off
        digitalWrite(pinFan, LOW);
        digitalWrite(pinCooling, LOW);
        digitalWrite(pinHeating, LOW);
    }

    updateModeDisplay();
}

void performAging2() {
    dbSerial.println("Aging Phase 2...");

    unsigned long currentMillis = millis();
    unsigned long cycleDuration = isAging2PhaseOn ? age2ont * 60000 : age2offt * 60000; // 'on' vagy 'off' ciklusnak megfelelő idő

    // Check if the current phase duration has ended
    if (currentMillis - aging2StartTime >= cycleDuration) {
        isAging2PhaseOn = !isAging2PhaseOn; // Toggle the phase
        aging2StartTime = currentMillis; // Reset the start time for the new phase
    }

    if (isAging2PhaseOn) {
        // Ventilátor bekapcsolva
        digitalWrite(pinFan, HIGH);

        // Hőmérséklet szabályozása
        if (compensatedTemperature >= age2Temp + tphist) {
            digitalWrite(pinCooling, HIGH);
        } else if (compensatedTemperature <= age2Temp - tphist) {
            digitalWrite(pinCooling, LOW);
        }

        // Páratartalom szabályozása
        if (compensatedHumidity < age2Hum - hmhist) {
            digitalWrite(pinHeating, HIGH);
        } else if (compensatedHumidity > age2Hum + hmhist) {
            digitalWrite(pinHeating, LOW);
        }

        // Túl magas hőmérséklet esetén
        if (compensatedTemperature > age2Temp + 10) {
            digitalWrite(pinHeating, LOW);  // Fűtés lekapcsolása
            updateMessageDisplay("Túl magas hőmérséklet");
        }
        
    } else {
        // Off phase: all elements turned off
        digitalWrite(pinFan, LOW);
        digitalWrite(pinCooling, LOW);
        digitalWrite(pinHeating, LOW);
    }

    updateModeDisplay();
}



void updateMessageDisplay(String message) {
    updateNextionTextbox("page0.message", message);
    if(message.startsWith("Sensor error")) {
        digitalWrite(pinError, HIGH); // Hibajelzés bekapcsolás
    } else {
        digitalWrite(pinError, LOW);  // Hibajelzés kikapcsolás
    }
}