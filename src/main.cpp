#include "robotka.h"

#include <Arduino.h>
#include <string>

#include "Roadside.h"
#include "Movement.h"
#include "ContestTimer.h"
#include "Shoulder.h"

// Nastavení Roadsidu
GameManager RoadsideGame;

// Nastavení Časovače
ContestTimer GameTimer; 

// Globální instance Ramene (propojí se přes extern všude tam, kde je Shoulder.h)
Shoulder Rameno;

// --- Proměnné pro MENU ---
enum class MenuState {
    ROADSIDE_SELECT_TEAM,
    ROADSIDE_SELECT_LAYOUT,
    ROADSIDE_WAIT_START,
    GAME_RUNNING
};

MenuState eCurrentState = MenuState::ROADSIDE_SELECT_TEAM;
TeamColor eSelectedTeam = TeamColor::Blue; // Výchozí tým
int iSelectedLayout = 0; 
bool bRoadsideGameStarted = false;


float rCurrentRobotX = 1400.0f; 
float rCurrentRobotY = 200.0f;  

void setup() {
    printf("robotka started\n");

    // Inicializace knihovny Robotka
    rkConfig cfg;
    rkSetup(cfg);

    printf("Robotka started!\n");
    Serial.begin(115200);

    //rkCheckBattery(); 
    
    rkLedAll(false); // Vypneme LED na startu, menu se o ně postará


    // Nastavení Chytrých serv
    rkSmartServoInit(0, 0, 240, 500, 3);
    rkSmartServoInit(1, 0, 240);

    
    //rkWaitForStart(); 
}
void loop() {

    switch (eCurrentState) {
        
        // ========================================================
        // ROADSIDE MENU 1: Výběr Týmu (Barvy)
        // ========================================================

        case MenuState::ROADSIDE_SELECT_TEAM:
            // Signalizace podle vybraného týmu
            rkLedAll(false);
            if (eSelectedTeam == TeamColor::Blue) rkLedBlue(true);
            else if (eSelectedTeam == TeamColor::Red) rkLedRed(true);

            if (rkButton1(true)) { eSelectedTeam = TeamColor::Blue; printf("Tym: MODRA\n"); }
            if (rkButton2(true)) { eSelectedTeam = TeamColor::Red; printf("Tym: CERVENA\n"); }
            
            // Potvrzení MENU
            if (rkButtonOn(true)) {
                eCurrentState = MenuState::ROADSIDE_SELECT_LAYOUT;
                printf("Presun do: ROADSIDE MENU 2 (Vyber rozlozeni)\n");
            }
            break;

        // ========================================================
        // ROADSIDE MENU 2: Výběr Rozložení Baterií
        // ========================================================

        case MenuState::ROADSIDE_SELECT_LAYOUT:
            // Signalizace rozložení (0-3) pomocí barev
            rkLedAll(false);
            if (iSelectedLayout == 0) rkLedRed(true);
            else if (iSelectedLayout == 1) rkLedYellow(true);
            else if (iSelectedLayout == 2) rkLedGreen(true);
            else if (iSelectedLayout == 3) rkLedBlue(true);

            // -- Výběr hodnot
            if (rkButton1(true)) { 
                iSelectedLayout = (iSelectedLayout + 1) % 4; 
                printf("Rozlozeni (NEXT): %d\n", iSelectedLayout); 
            }
            if (rkButton2(true)) { 
                iSelectedLayout = (iSelectedLayout - 1 < 0) ? 3 : iSelectedLayout - 1; 
                printf("Rozlozeni (BACK): %d\n", iSelectedLayout); 
            }

            // Potvrzení MENU
            if (rkButtonOn(true)) {
                eCurrentState = MenuState::ROADSIDE_WAIT_START;
                printf("Presun do: ROADSIDE MENU 3 (Cekam na start)\n");
            }
            // Návrat do predchozího MENU
            if (rkButtonOff(true)) {
                eCurrentState = MenuState::ROADSIDE_SELECT_TEAM;
                printf("Zpet na: Vyber tymu\n");
            }
            break;

        // ========================================================
        // ROADSIDE MENU 3: START (ON)
        // ========================================================

        case MenuState::ROADSIDE_WAIT_START:
            // Blikání všech LED na znamení připravenosti ke startu
            rkLedAll(millis() % 500 < 250);

            // Potvrzení MENU ---> Spuštění Roadside
            if (rkButtonOn(true)) {
                rkLedAll(false); // Vypnutí LED
                RoadsideGame.fInitGame(iSelectedLayout, eSelectedTeam);
                bRoadsideGameStarted = true;
                eCurrentState = MenuState::GAME_RUNNING;
                GameTimer.fStart(); // Spuštění vlákna pro odpočet času
                printf("=== ROADSIDE ZAPAS ODSTARTOVAN! ===\n");
            }
            // Návrat do predchozího MENU
            if (rkButtonOff(true)) {
                eCurrentState = MenuState::ROADSIDE_SELECT_LAYOUT;
                printf("Zpet na: Vyber rozlozeni\n");
            }
            break;

        // ========================================================
        // FÁZE 2: HLAVNÍ JÍZDNÍ SMYČKA (Po odstartování)
        // ========================================================

        case MenuState::GAME_RUNNING:
            
            // Zjistíme, jestli nám nedochází čas (limit 300s, tolerance např. 45s pro návrat)
            if (GameTimer.bIsTimeRunningOut(300, 45)) {
                printf("[CAS!] Zbyva malo casu (ubehlo %d s)! Ukoncuji sber a vracim se na start...\n", GameTimer.iGetElapsedSeconds());
                // Následoval by kód pro odjezd domů
                }

                // Tady už běží samotná odometrie a logika soutěže ROADSIDE
                
                // Zkusi sebrat baterii s vyhýbáním překážkám
                RoadsideGame.fTakeClosestBattery(rCurrentRobotX);

            break;
    }
// ======= TESTOVACÍ ČÁSTI ==========

    
    if (rkButtonIsPressed(BTN_LEFT) && rkButtonIsPressed(BTN_RIGHT))    {  }
    if (rkButtonIsPressed(BTN_UP) && rkButtonIsPressed(BTN_DOWN))       {  }
    if (rkButtonIsPressed(BTN_ON))      { }
    if (rkButtonIsPressed(BTN_OFF))     { 
        
        // SCÉNÁŘ PRO MANIPULÁTOR 1 (ID 0)
        
             // 1. Otevři chapadlo
        delay(1500);
        
              // 2. Dojeď pro předmět před sebe                       | Cíl: X=180, Y=50, Základna = 0° (vpřed)
        delay(10000);
        
             // 3. Zavři chapadlo (chytni předmět)
        delay(1500);    
        
            // 4. Zvedni ho do výšky a otoč se s ním doprava        | Cíl: X=180, Y=50, Základna = -90° (vpravo)
        delay(10000);
        
              // 5. Otevři chapadlo (pusť předmět)
        delay(1500);

        printf("\n");
        
    }

    // Ruční nastavování pozice Manipulátoru
    // if (rkButtonIsPressed(BTN_UP))      { rkSmartServoMove(0, rkSmartServosPosicion(0) + 5); }
    // if (rkButtonIsPressed(BTN_DOWN))    { rkSmartServoMove(0, rkSmartServosPosicion(0) - 5 ); } 
    // if (rkButtonIsPressed(BTN_LEFT))    { rkSmartServoMove(1, rkSmartServosPosicion(1) + 5); }
    // if (rkButtonIsPressed(BTN_RIGHT))   { rkSmartServoMove(1, rkSmartServosPosicion(1) - 5);}

    delay(10);
}
