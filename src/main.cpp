#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

#define DHT_PIN 2
#define DHT_TYPE DHT11
#define LIGHT_PIN A0

#define BUZZER_PIN 4
#define RED_PIN 5
#define GREEN_PIN 6
#define BLUE_PIN 7

#define BTN_1 8
#define BTN_2 9
#define BTN_3 10
#define BTN_4 11

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT_TYPE);

/*
    Aici am definit meniurile principale ale proiectului.

    NAME_MENU este meniul de la pornire, unde utilizatorul isi alege numele pet-ului.
    INFO_MENU este meniul principal, unde se afiseaza informatii despre pet.
    FEED_MENU este meniul in care utilizatorul alege portia de mancare.
    PLAY_MENU este meniul jocului de dexteritate.
*/
enum MenuState {
    NAME_MENU,
    INFO_MENU,
    FEED_MENU,
    PLAY_MENU
};

/*
    Aici am definit starile jocului de dexteritate.

    PLAY_START porneste jocul.
    PLAY_WAIT asteapta un timp random.
    PLAY_SIGNAL aprinde LED-ul verde si cere apasarea butonului.
    PLAY_RESULT era gandit pentru rezultat, dar rezultatul este afisat direct in cod.
*/
enum PlayState {
    PLAY_START,
    PLAY_WAIT,
    PLAY_SIGNAL,
    PLAY_RESULT
};

MenuState currentMenu = NAME_MENU;
PlayState playState = PLAY_START;

/*
    Aceste variabile reprezinta starea interna a animalului virtual.

    hunger creste in timp, iar daca devine mare pet-ul devine flamand.
    energy scade in timp si influenteaza mood-ul.
    happiness reprezinta cat de fericit este pet-ul.
    xp si level sunt folosite pentru progres.
*/
int hunger = 15;
int energy = 80;
int happiness = 75;
int xp = 0;
int level = 1;

int feedPortion = 10;

unsigned long lastStatsUpdate = 0;
unsigned long lastBeep = 0;
unsigned long startTime = 0;

unsigned long playStartTime = 0;
unsigned long playSignalTime = 0;
unsigned long reactionTime = 0;
unsigned long waitTime = 0;

int screenPage = 0;

char petName[12] = "";
int nameIndex = 0;
char currentLetter = 'A';

bool lastB1 = HIGH;
bool lastB2 = HIGH;
bool lastB3 = HIGH;
bool lastB4 = HIGH;

/*
    Functia printLine afiseaza text pe un rand al LCD-ului.

    Am facut aceasta functie ca sa evit problema in care raman caractere vechi
    pe ecran. Deoarece LCD-ul are 16 caractere pe linie, completez textul cu
    spatii pana la 16 caractere.
*/
void printLine(int row, String text)
{
    while (text.length() < 16) {
        text += " ";
    }

    lcd.setCursor(0, row);
    lcd.print(text.substring(0, 16));
}

/*
    Functia setColor controleaza LED-ul RGB.

    Am implementat aceasta functie ca sa pot seta usor culoarea pet-ului:
    - verde pentru happy
    - albastru pentru sleepy sau sad
    - rosu pentru angry
    - galben/cyan pentru alte stari
*/
void setColor(bool red, bool green, bool blue)
{
    digitalWrite(RED_PIN, red ? HIGH : LOW);
    digitalWrite(GREEN_PIN, green ? HIGH : LOW);
    digitalWrite(BLUE_PIN, blue ? HIGH : LOW);
}

/*
    Functia beep genereaza un sunet pe buzzer.

    Am folosit tone pentru ca pot controla frecventa sunetului.
    Buzzerul este folosit pentru confirmari, erori, level up si joc.
*/
void beep(int frequency, int duration)
{
    tone(BUZZER_PIN, frequency);
    delay(duration);
    noTone(BUZZER_PIN);
}

/*
    Functia buttonPressed verifica daca un buton a fost apasat.

    Butoanele sunt legate cu INPUT_PULLUP, deci:
    - HIGH inseamna neapasat
    - LOW inseamna apasat

    Am adaugat debounce prin delay(80), ca sa evit citirile multiple
    cauzate de contactul mecanic al butonului.
*/
bool buttonPressed(int pin, bool &lastState)
{
    bool currentState = digitalRead(pin);

    if (lastState == HIGH && currentState == LOW) {
        delay(80);

        if (digitalRead(pin) == LOW) {
            lastState = LOW;
            return true;
        }
    }

    if (currentState == HIGH) {
        lastState = HIGH;
    }

    return false;
}

/*
    Functia isNight verifica daca este intuneric.

    Am folosit valoarea citita de la senzorul TEMT6000.
    Daca lumina este sub pragul 180, consider ca este noapte.
*/
bool isNight(int lightValue)
{
    return lightValue < 180;
}

/*
    Functia getEnvironmentStatus interpreteaza valorile senzorilor.

    Aici nu afisez doar numere brute, ci transform temperatura, umiditatea
    si lumina intr-un status usor de inteles: NIGHT, TOO HOT, TOO COLD etc.
*/
String getEnvironmentStatus(float temp, float hum, int lightValue)
{
    if (isNight(lightValue)) return "NIGHT";
    if (temp > 30) return "TOO HOT";
    if (temp < 18) return "TOO COLD";
    if (hum < 30) return "DRY AIR";
    if (hum > 75) return "HUMID AIR";
    if (lightValue > 850) return "BRIGHT";

    return "COMFY";
}

/*
    Functia getMood calculeaza mood-ul pet-ului.

    Am implementat un sistem pe baza de scor, nu o singura conditie.
    Mood-ul depinde de:
    - happiness
    - hunger
    - energy
    - temperatura
    - umiditate
    - lumina

    Astfel, pet-ul nu devine angry doar pentru ca temperatura este mare,
    ci se tine cont de mai multi factori.
*/
String getMood(float temp, float hum, int lightValue)
{
    int score = happiness;

    score -= hunger / 2;

    if (energy < 25) score -= 25;
    else if (energy < 50) score -= 10;

    if (temp > 30) score -= 25;
    else if (temp > 27) score -= 10;

    if (temp < 18) score -= 20;
    if (hum < 30 || hum > 75) score -= 10;
    if (lightValue > 850) score -= 8;

    if (isNight(lightValue) && energy < 70) return "SLEEPY";
    if (hunger > 80) return "HUNGRY";
    if (score >= 75) return "HAPPY";
    if (score >= 55) return "CALM";
    if (score >= 35) return "SAD";

    return "ANGRY";
}

/*
    Functia applyMoodOutput aplica efectele pentru mood.

    In functie de mood, LED-ul RGB isi schimba culoarea.
    Daca pet-ul este angry, buzzerul scoate un sunet rar, nu continuu,
    ca sa nu devina enervant.
*/
void applyMoodOutput(String mood)
{
    if (currentMenu == PLAY_MENU && playState == PLAY_WAIT) {
        setColor(false, false, false);
        return;
    }

    if (currentMenu == PLAY_MENU && playState == PLAY_SIGNAL) {
        setColor(false, true, false);
        return;
    }

    if (mood == "HAPPY") {
        setColor(false, true, false);
    } else if (mood == "CALM") {
        setColor(false, true, true);
    } else if (mood == "SLEEPY") {
        setColor(false, false, true);
    } else if (mood == "HUNGRY") {
        setColor(true, true, false);
    } else if (mood == "SAD") {
        setColor(false, false, true);
    } else {
        setColor(true, false, false);

        if (millis() - lastBeep > 8000) {
            beep(1000, 150);
            lastBeep = millis();
        }
    }
}

/*
    Functia checkLevelUp verifica daca pet-ul a acumulat suficient XP.

    Cand xp ajunge la 20, nivelul creste, se afiseaza mesaj pe LCD
    si se reda un sunet special.
*/
void checkLevelUp()
{
    if (xp >= 20) {
        xp = 0;
        level++;

        if (level > 9) {
            level = 9;
        }

        lcd.clear();
        printLine(0, "LEVEL UP!");
        printLine(1, "Level: " + String(level));

        beep(1500, 120);
        delay(100);
        beep(1800, 120);

        delay(1000);
        lcd.clear();
    }
}

/*
    Functia updateStats actualizeaza automat starea pet-ului.

    Am facut ca la fiecare 5 secunde:
    - foamea sa creasca
    - energia sa scada
    - happiness sa creasca daca mediul este bun
    - happiness sa scada daca mediul este rau

    Aceasta functie face pet-ul sa para viu, chiar daca utilizatorul nu apasa nimic.
*/
void updateStats(float temp, float hum, int lightValue)
{
    if (millis() - lastStatsUpdate < 5000) {
        return;
    }

    lastStatsUpdate = millis();

    hunger += 2;
    energy -= 1;

    if (isNight(lightValue)) {
        energy += 3;
        happiness += 1;
    }

    if (temp >= 20 && temp <= 27 && hum >= 35 && hum <= 70) {
        happiness += 2;
        xp += 2;
    } else {
        happiness -= 3;
    }

    if (hunger > 70) happiness -= 2;
    if (energy < 25) happiness -= 2;

    hunger = constrain(hunger, 0, 100);
    energy = constrain(energy, 0, 100);
    happiness = constrain(happiness, 0, 100);

    checkLevelUp();
}

/*
    Functia showNameScreen afiseaza meniul de alegere a numelui.

    Pe prima linie apare numele construit pana acum plus litera curenta.
    Pe a doua linie apar instructiunile pentru butoane.
*/
void showNameScreen()
{
    String nameText = String(petName);

    if (nameIndex < 11) {
        nameText += currentLetter;
    }

    printLine(0, "Name:" + nameText);
    printLine(1, "B3 add B4 ok");
}

/*
    Functia handleNameMenu gestioneaza alegerea numelui.

    Am folosit cele 4 butoane astfel:
    - B1 creste litera curenta
    - B2 scade litera curenta
    - B3 adauga litera in nume
    - B4 confirma numele

    Numele poate avea intre 1 si 11 caractere.
*/
void handleNameMenu()
{
    showNameScreen();

    if (buttonPressed(BTN_1, lastB1)) {
        currentLetter++;

        if (currentLetter > 'Z') {
            currentLetter = 'A';
        }

        beep(900, 60);
        showNameScreen();
    }

    if (buttonPressed(BTN_2, lastB2)) {
        currentLetter--;

        if (currentLetter < 'A') {
            currentLetter = 'Z';
        }

        beep(700, 60);
        showNameScreen();
    }

    if (buttonPressed(BTN_3, lastB3)) {
        if (nameIndex < 11) {
            petName[nameIndex] = currentLetter;
            nameIndex++;
            petName[nameIndex] = '\0';
            currentLetter = 'A';
            beep(1200, 80);
        } else {
            beep(400, 150);
        }

        showNameScreen();
    }

    if (buttonPressed(BTN_4, lastB4)) {
        if (nameIndex >= 1) {
            lcd.clear();
            printLine(0, "Hello " + String(petName));
            printLine(1, "MoodPet ready!");

            beep(1500, 100);
            delay(150);
            beep(1800, 100);

            delay(1200);
            lcd.clear();

            currentMenu = INFO_MENU;
            screenPage = 0;
            startTime = millis();
        } else {
            printLine(0, "Name too short");
            printLine(1, "Add 1 letter");
            beep(500, 200);
            delay(1000);
        }
    }
}

/*
    Functia showInfoScreen afiseaza meniul principal.

    Am impartit informatiile in pagini, deoarece LCD-ul are doar 16x2 caractere.
    Butonul B4 schimba pagina, iar B1 si B2 intra in meniurile interactive.
*/
void showInfoScreen(float temp, float hum, int lightValue, String mood)
{
    String env = getEnvironmentStatus(temp, hum, lightValue);
    String name = String(petName);

    if (screenPage == 0) {
        printLine(0, name + " " + mood);
        printLine(1, "B1Feed B2Play");
    } else if (screenPage == 1) {
        printLine(0, "T:" + String((int)temp) + "C H:" + String((int)hum) + "%");
        printLine(1, "Light:" + String(lightValue));
    } else if (screenPage == 2) {
        printLine(0, "Food:" + String(100 - hunger));
        printLine(1, "En:" + String(energy) + " Hap:" + String(happiness));
    } else {
        printLine(0, "Lvl:" + String(level) + " XP:" + String(xp));
        printLine(1, "Time:" + String((millis() - startTime) / 1000) + "s");
    }
}

/*
    Functia showFeedScreen afiseaza meniul de hranire.

    Utilizatorul poate modifica portia cu butoanele B2 si B3,
    iar cu B4 confirma hranirea.
*/
void showFeedScreen()
{
    printLine(0, "Feed portion:");
    printLine(1, "-" + String(feedPortion) + "+  B4 confirm");
}

/*
    Functia handleFeedMenu gestioneaza hranirea pet-ului.

    Am implementat portii variabile:
    - B2 scade portia
    - B3 creste portia
    - B4 confirma

    Cand pet-ul mananca, hunger scade, happiness creste,
    iar energia scade putin pentru ca o portie mare il face mai obosit.
*/
void handleFeedMenu()
{
    showFeedScreen();

    if (buttonPressed(BTN_2, lastB2)) {
        feedPortion -= 5;

        if (feedPortion < 5) {
            feedPortion = 5;
        }

        beep(700, 60);
        showFeedScreen();
    }

    if (buttonPressed(BTN_3, lastB3)) {
        feedPortion += 5;

        if (feedPortion > 50) {
            feedPortion = 50;
        }

        beep(900, 60);
        showFeedScreen();
    }

    if (buttonPressed(BTN_4, lastB4)) {
        hunger -= feedPortion;

        if (feedPortion <= 10) {
            happiness += 7;
        } else if (feedPortion <= 20) {
            happiness += 12;
        } else if (feedPortion <= 30) {
            happiness += 20;
        } else {
            happiness += 25;
        }

        energy -= feedPortion / 10;
        xp += feedPortion / 10;

        hunger = constrain(hunger, 0, 100);
        happiness = constrain(happiness, 0, 100);
        energy = constrain(energy, 0, 100);
        xp = constrain(xp, 0, 25);

        lcd.clear();
        printLine(0, String(petName) + " ate!");
        printLine(1, "Food +" + String(feedPortion));

        beep(1500, 100);
        delay(1200);
        lcd.clear();

        checkLevelUp();

        currentMenu = INFO_MENU;
        screenPage = 2;
    }
}

/*
    Functia startPlayGame porneste jocul de dexteritate.

    LED-ul se stinge si utilizatorul trebuie sa astepte.
    Dupa un timp random, LED-ul devine verde si utilizatorul trebuie
    sa apese rapid B1.
*/
void startPlayGame()
{
    playState = PLAY_WAIT;
    waitTime = random(1500, 5000);
    playStartTime = millis();
    setColor(false, false, false);

    lcd.clear();
    printLine(0, "Dexterity game");
    printLine(1, "Wait for GREEN");
}

/*
    Functia handlePlayMenu implementeaza jocul de dexteritate.

    Logica jocului:
    - daca utilizatorul apasa prea devreme, pierde happiness
    - cand LED-ul devine verde, trebuie sa apese B1 rapid
    - cu cat timpul de reactie este mai mic, cu atat castiga mai mult
      happiness si XP
*/
void handlePlayMenu()
{
    if (playState == PLAY_START) {
        startPlayGame();
        return;
    }

    if (playState == PLAY_WAIT) {
        setColor(false, false, false);
        printLine(0, "Wait...");
        printLine(1, "Do not press!");

        if (buttonPressed(BTN_1, lastB1)) {
            happiness -= 5;
            happiness = constrain(happiness, 0, 100);

            lcd.clear();
            printLine(0, "Too early!");
            printLine(1, "Hap -5");
            beep(400, 300);
            delay(1200);

            currentMenu = INFO_MENU;
            screenPage = 2;
            playState = PLAY_START;
            return;
        }

        if (millis() - playStartTime >= waitTime) {
            playState = PLAY_SIGNAL;
            playSignalTime = millis();
            lcd.clear();
            printLine(0, "PRESS B1 NOW!");
            printLine(1, "Go go go!");
            setColor(false, true, false);
            beep(1600, 80);
        }

        return;
    }

    if (playState == PLAY_SIGNAL) {
        setColor(false, true, false);

        if (buttonPressed(BTN_1, lastB1)) {
            reactionTime = millis() - playSignalTime;

            int hapGain = 0;
            int xpGain = 0;

            if (reactionTime < 500) {
                hapGain = 20;
                xpGain = 8;
            } else if (reactionTime < 1000) {
                hapGain = 12;
                xpGain = 5;
            } else if (reactionTime < 2000) {
                hapGain = 6;
                xpGain = 3;
            } else {
                hapGain = 2;
                xpGain = 1;
            }

            happiness += hapGain;
            xp += xpGain;
            energy -= 5;

            happiness = constrain(happiness, 0, 100);
            energy = constrain(energy, 0, 100);
            xp = constrain(xp, 0, 25);

            lcd.clear();
            printLine(0, "React:" + String(reactionTime) + "ms");
            printLine(1, "H+" + String(hapGain) + " XP+" + String(xpGain));

            beep(1800, 100);
            delay(100);
            beep(2000, 100);
            delay(1400);

            checkLevelUp();

            currentMenu = INFO_MENU;
            screenPage = 2;
            playState = PLAY_START;
            lcd.clear();
            return;
        }

        if (millis() - playSignalTime > 3000) {
            happiness -= 5;
            happiness = constrain(happiness, 0, 100);

            lcd.clear();
            printLine(0, "Too slow!");
            printLine(1, "Hap -5");
            beep(400, 300);
            delay(1200);

            currentMenu = INFO_MENU;
            screenPage = 2;
            playState = PLAY_START;
            lcd.clear();
            return;
        }
    }
}

/*
    Functia setup ruleaza o singura data la pornirea placii.

    Aici am configurat pinii, LCD-ul, senzorul DHT11 si generatorul random.
    Tot aici afisez mesajul initial si pornesc proiectul in meniul de nume.
*/
void setup()
{
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    pinMode(BTN_1, INPUT_PULLUP);
    pinMode(BTN_2, INPUT_PULLUP);
    pinMode(BTN_3, INPUT_PULLUP);
    pinMode(BTN_4, INPUT_PULLUP);

    lcd.init();
    lcd.backlight();

    dht.begin();
    randomSeed(analogRead(LIGHT_PIN));

    startTime = millis();

    lcd.clear();
    printLine(0, "MoodPet :)");
    printLine(1, "Set name...");
    beep(1200, 120);
    delay(1000);
    lcd.clear();
}

/*
    Functia loop ruleaza continuu.

    Aici am implementat controlul general al proiectului:
    - daca suntem in NAME_MENU, gestionam alegerea numelui
    - apoi citim senzorii
    - actualizam statisticile pet-ului
    - calculam mood-ul
    - aplicam iesirile pe LED si buzzer
    - gestionam meniul curent: info, feed sau play
*/
void loop()
{
    if (currentMenu == NAME_MENU) {
        handleNameMenu();
        delay(80);
        return;
    }

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int lightValue = analogRead(LIGHT_PIN);

    if (isnan(temp) || isnan(hum)) {
        printLine(0, "Sensor error");
        printLine(1, "Check DHT11");
        setColor(true, false, false);
        delay(1000);
        return;
    }

    updateStats(temp, hum, lightValue);

    String mood = getMood(temp, hum, lightValue);
    applyMoodOutput(mood);

    if (currentMenu == INFO_MENU) {
        if (buttonPressed(BTN_1, lastB1)) {
            feedPortion = 10;
            lcd.clear();
            currentMenu = FEED_MENU;
            beep(1000, 70);
            delay(100);
            return;
        }

        if (buttonPressed(BTN_2, lastB2)) {
            lcd.clear();
            currentMenu = PLAY_MENU;
            playState = PLAY_START;
            beep(1300, 70);
            delay(100);
            return;
        }

        if (buttonPressed(BTN_4, lastB4)) {
            screenPage++;

            if (screenPage > 3) {
                screenPage = 0;
            }

            beep(1000, 70);
        }

        showInfoScreen(temp, hum, lightValue, mood);
    }

    if (currentMenu == FEED_MENU) {
        handleFeedMenu();
    }

    if (currentMenu == PLAY_MENU) {
        handlePlayMenu();
    }

    delay(120);
}