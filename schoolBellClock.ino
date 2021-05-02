#include <LiquidCrystal.h>
#include <DS3231.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <EEPROM.h>

const byte buttonMain = A2;
const byte buttonPlan = A0;
const byte bellout = A1;
const byte ledout = A3;
const byte cardReaderPin = 4;

const unsigned short dataSize = 28;
byte plan1Data[dataSize] = {7, 57, 3, 45, 17, 3, 45, 7, 3, 45, 7, 3, 45, 7, 3, 45, 7, 3, 45, 7, 3, 45, 7, 3, 45, 2, 3, 45};
byte plan2Data[dataSize] = {7, 57, 3, 35, 12, 3, 35, 7, 3, 35, 7, 3, 35, 7, 3, 35, 7, 3, 35, 7, 3, 35, 7, 3, 35, 7, 3, 35};

TinyGPSPlus gps;
SoftwareSerial ss(12, 13);

LiquidCrystal lcd(2, 3, 6, 7, 8, 9);
DS3231 Clock;
const unsigned short holdThreshold = 400;
const unsigned short clickThreshold = 80;
unsigned long buttonHold = 0;
unsigned long lastClockSync;
byte hoursToAddAddress = 2;
byte plan = 1;
byte state = 0;
/*
 * 0: SHOW CLOCK
 * 1: GPS DATA
 * 2: setDays
 * 3: hoursToAdd
 */

byte onlyWorkDaysAddress = 0;

struct ClockData {
  int h, m, i;
};

ClockData next = {-1, -1, -1};
ClockData nextNext = {-1, -1, -1};
bool updated = false;

ClockData findNext(ClockData curr) {
  if(plan == 0) {
    return {-1, -1, -1};
  }

  byte data[dataSize];
  for(byte i = 0; i < dataSize; i++) {
    if(plan == 1) {
      data[i] = plan1Data[i];
    } else {
      data[i] = plan2Data[i];
    }
  }
  
  ClockData next = {data[0], data[1], 0};
  bool nw = false;

  for(byte i = 2; i <= dataSize; i++) {
    if((curr.h == next.h && next.m > curr.m) || (next.h > curr.h)) {
      if(!nw) {
        next.i = -1;
      }
      return next;
    } 

    nw = false;
    if(i % 3 == 0) {
      next.i++;
      nw = true;
    }

    if(i < dataSize) {
      byte am = data[i];
      if(am > 0) {
        next.m += data[i];
        if(next.m > 59) {
          next.h++;
          next.m -= 60;
        }
      }
    }
  }

  return {data[0], data[1], -1};
}

void updateNext(int h, int m) {
  next = findNext({h, m, -1});
  nextNext = findNext(next);
}

String clockToStr(int c) {
  return (c < 10 ? "0" : "") + (String)c;
}

String clockToDisplay(byte h, byte m, byte s, bool e = false, bool last = true) {
  String firstPart = clockToStr(h) + ":" + clockToStr(m);
  
  if(last) {
    firstPart += (e ? "|" : ":") + clockToStr(s);
  }

  return firstPart;
}

void updatePlanButton(byte h, byte m) {
  int value = analogRead(buttonPlan);
  byte newPlan = plan;

  if(value > 700) {
    newPlan = 1;
  } else if(value < 100) {
    newPlan = 0;
  } else {
    newPlan = 2;
  }

  
  if(newPlan != plan) {
    plan = newPlan;
    updateNext(h, m);
    state = 0;
    lcd.clear();
  }
}

bool doBell(byte h, byte m, byte s) {
  if(h == next.h && m == next.m) {
    if(s < 10) {
      return true;
    } else if(s > 20 && s < 30 && m % 5 != 0) {
      return true;
      
    } else if((m % 5 == 0 && s > 10 && s < 12) || (s > 30 && s < 32)){
      updateNext(h, m);
    }
  }
  return false;
}

void displayClock(byte h, byte m, byte s, byte dow, byte p = -1, ClockData n = {-1, -1, -1}, ClockData nn = {-1, -1, -1}) {
  String DAYOFWEEK[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

  bool onlyWorkDays = EEPROM.read(onlyWorkDaysAddress) == 1;

  lcd.setCursor(0, 0);
  lcd.print(clockToDisplay(h, m, s));

  lcd.setCursor(9, 0);
  lcd.print(DAYOFWEEK[dow - 1]);

  if(p >= 0) {
    lcd.setCursor(14 - (plan == 0 ? 1 : 0), 0);
    lcd.print(p == 0 ? "OFF" : ((onlyWorkDays ? "R" : "S") + (String)p));
  }

  if(n.h > 0) {
    if((onlyWorkDays && dow >= 6) || (!onlyWorkDays && dow <= 5)) {
      lcd.setCursor(3, 1);
      lcd.print(onlyWorkDays ? "-> MON" : "-> SAT");
      return;
    }

    lcd.setCursor(15, 1);
    lcd.print(n.i > 0 ? (String)n.i : "N");

    lcd.setCursor(0, 1);
    lcd.print(clockToDisplay(n.h, n.m, (n.m % 5 == 0 ? 10 : 30) - s , true, doBell(h, m, s)));
  }

  if(nn.h > 0) {
    lcd.setCursor(9, 1);
    lcd.print(clockToDisplay(nn.h, nn.m, -1, true, false));
  }
}

bool isGPSConnected() {
  return gps.location.isUpdated() && gps.satellites.value() > 0;
}

bool syncClock() {
  if(isGPSConnected()) {
    unsigned short hours = gps.time.hour();
    if(hours + 2 > 24) {
      hours -= 24;
    }
    else {
      hours += (int)EEPROM.read(hoursToAddAddress);
    }

    Clock.setHour(hours);
    Clock.setMinute(gps.time.minute());
    Clock.setSecond(gps.time.second());
    unsigned short y = gps.date.year();
    Clock.setYear(y);
    Clock.setMonth(gps.date.month());
    Clock.setDate(gps.date.day());
    
    const short t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= gps.date.month() < 3;
    Clock.setDoW((y + y / 4 - y / 100 + y / 400 + t[gps.date.month() - 1] + gps.date.day()) % 7);

    lastClockSync = millis();

    return true;
  }

  return false;
}

void setup() {
  Serial.begin(9600);
  
  bool cardRead = false;
  if (SD.begin(cardReaderPin)) {
    if (SD.exists("plan.txt")) {
      cardRead = true;
      File file = SD.open("plan.txt");
      bool plan1 = true;
      int index = 0;
      String num = "";
      
      while(file.available()) {
        String t = file.readString();
        for(int i = 0; i <= t.length(); i++) {
          char ch = ' ';
          if(i < t.length()){
            ch = t.charAt(i);
          }
          
          if(ch == ' ') {
            if(plan1) {
              plan1Data[index] = num.toInt();
            } else {
              plan2Data[index] = num.toInt();
            }

            index++;
            if(index == dataSize) {
              index = 0;
              plan1 = false;
            }
            num = "";
          } 
          else {
            num += ch;
          }
        }
      }

      file.close();
    }
  }
  
  lcd.begin(16, 2);
  Wire.begin();
  ss.begin(9600);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Starting..."));
  delay(2000);

  if(cardRead) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Card reading..."));
    delay(500);
    lcd.setCursor(0, 1);
    lcd.print("OK");
    delay(1500);
  }

  lcd.clear();
  
  buttonHold = 0;

  if(EEPROM.read(onlyWorkDaysAddress) > 1) {
    EEPROM.write(onlyWorkDaysAddress, 0);
  }

  if(EEPROM.read(hoursToAddAddress) > 4) {
    EEPROM.write(hoursToAddAddress, 3);
  }
}

bool buttonReady = false;
bool buttonClick = false;

void loop() {
  Serial.print(ss.available());
  Serial.print("   ");
  Serial.println(buttonHold);
  while (ss.available() > 0) {
    gps.encode(ss.read());
    //Serial.println(ss.read());
  }

  unsigned short clockSeconds = Clock.getSecond();
  unsigned short clockMinutes = Clock.getMinute();
  bool h12, pm;
  unsigned short clockHours = Clock.getHour(h12, pm);

  if(h12 && pm) {
    clockHours += 12;
  }
  unsigned short clockDOW = Clock.getDoW();
  
  if((clockHours == 6 && clockMinutes == 0 && clockSeconds == 30) || 
    (clockHours == 19 && clockMinutes == 0 && clockSeconds == 30)) {
    syncClock();
    return;
  }
  
  if(!updated) {
    updateNext(clockHours, clockMinutes);
    updated = true;
  }

  bool onlyWorkDays = EEPROM.read(onlyWorkDaysAddress) == 1;

  switch(state){
    case 0: { //SHOW CLOCK
      updatePlanButton(clockHours, clockMinutes);
      displayClock(clockHours, clockMinutes, clockSeconds, clockDOW, plan, next, nextNext);

      analogWrite(ledout, onlyWorkDays ? 0 : 1023);
      
      if((onlyWorkDays && clockDOW < 6) || (!onlyWorkDays && clockDOW > 5)) {
        if(doBell(clockHours, clockMinutes, clockSeconds)) {
          if(analogRead(bellout) > 100) {
            analogWrite(bellout, 0);
            lcd.clear();
          }
        } 
        else if(analogRead(bellout) < 100) {
          analogWrite(bellout, 1023);
          lcd.clear();
        }
      }
      break;
    }

    case 1: {
      if(isGPSConnected()) {
        lcd.setCursor(1, 0);  
        lcd.print(F("Sats: "));
        lcd.print(gps.satellites.value());
        lcd.print(F("      "));
      }
      else {
        lcd.setCursor(1, 0);  
        lcd.print(F("GPS no signal"));
      }

      lcd.setCursor(0, 1);
      lcd.print(F("Last sync: "));
      lcd.print((millis() - lastClockSync) / 3600000); //millis to Hours
      lcd.print(F("  h"));
      
      break;
    }

    case 2: {
      lcd.setCursor(2, 0);
      lcd.print(F("R: MON -> FRI"));
      lcd.setCursor(2, 1);
      lcd.print(F("S: SAT -> SUN"));

      lcd.setCursor(0, onlyWorkDays ? 1 : 0);
      lcd.print(F(" "));
      lcd.setCursor(0, onlyWorkDays ? 0 : 1);
      lcd.print(F(">"));
      
      break;
    }

    case 3: {
      lcd.setCursor(1, 0);
      lcd.print(F("HoursToAdd:"));
      lcd.print((int)EEPROM.read(hoursToAddAddress));
    }
  }

  if(plan != 0) {
    return;
  }

  if(analogRead(buttonMain) > 900) {
    if(buttonHold == 0) {
      buttonHold = millis();
      buttonReady = true;
    }
    else {
      if(millis() - buttonHold >= holdThreshold) {
        buttonClick = true;
      }
    }
  } 
  else {
    buttonClick = buttonReady;
  }
  
  if(buttonClick && buttonReady) {
    buttonReady = false;
    int hold = millis() - buttonHold;
    if (hold > holdThreshold) { //BUTTON HOLD
      switch(state) {
        case 1: {
          lcd.clear();
          lcd.setCursor(1, 0);
          lcd.print(F("Reading data"));
          delay(1500);
          lcd.setCursor(5, 1);
          if(syncClock()) {
            lcd.print(F("Done"));
          }
          else {
            lcd.print(F("Error"));
          }
          delay(1000);
          lcd.clear();

          break;
        }

        case 2: {
          onlyWorkDays = (onlyWorkDays ? false : true);
          EEPROM.write(onlyWorkDaysAddress, onlyWorkDays ? 1 : 0);

          break; 
        }

        case 3: {
          int hours = (int)EEPROM.read(hoursToAddAddress);
          hours += 1;
          if(hours == 4) {
            hours = 0;
          }
          EEPROM.write(hoursToAddAddress, hours);
        }
      }
    }
    else if(hold >= clickThreshold) { //BUTTON CLICK
      state += 1;
      if(state == 4) {
        state = 0;
      }

      lcd.clear();
    }
    buttonHold = 0;
  }
}
