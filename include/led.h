/**
 * led.h - Affichage LED avec vrais chiffres 4x7
 * Matrice 24x8
 */

#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "config.h"
#include "score.h"

namespace LED {

static CRGB _leds[Config::NUM_LEDS];
static Preferences _prefs;

// Mapping coordonnée → index LED
// Câblage : zigzag vertical, commence en bas à droite
// Col 0 (droite) : bas→haut, Col 1 : haut→bas, Col 2 : bas→haut, etc.
inline int xy(int x, int y) {
  // Inverser x car ça commence à droite (x=0 devient la colonne de droite)
  int col = (Config::NUM_COLS - 1) - x;
  
  // Si colonne paire : bas→haut, donc inverser y
  // Si colonne impaire : haut→bas, y normal
  int row;
  if (col % 2 == 0) {
    row = (Config::NUM_ROWS - 1) - y;  // Bas→haut (inverser y)
  } else {
    row = y;  // Haut→bas (y normal)
  }
  
  return col * Config::NUM_ROWS + row;
}

// Définition des chiffres 4x7 (1 = allumé, 0 = éteint)
static const uint8_t DIGITS[10][7][4] = {
  // 0
  {{0,1,1,0},
   {1,0,0,1},
   {1,0,0,1},
   {1,0,0,1},
   {1,0,0,1},
   {1,0,0,1},
   {0,1,1,0}},
  
  // 1
  {{0,0,1,0},
   {0,1,1,0},
   {0,0,1,0},
   {0,0,1,0},
   {0,0,1,0},
   {0,0,1,0},
   {0,1,1,1}},
  
  // 2
  {{0,1,1,0},
   {1,0,0,1},
   {0,0,0,1},
   {0,0,1,0},
   {0,1,0,0},
   {1,0,0,0},
   {1,1,1,1}},
  
  // 3
  {{0,1,1,0},
   {1,0,0,1},
   {0,0,0,1},
   {0,1,1,0},
   {0,0,0,1},
   {1,0,0,1},
   {0,1,1,0}},
  
  // 4
  {{0,0,1,0},
   {0,1,1,0},
   {1,0,1,0},
   {1,0,1,0},
   {1,1,1,1},
   {0,0,1,0},
   {0,0,1,0}},
  
  // 5
  {{1,1,1,1},
   {1,0,0,0},
   {1,1,1,0},
   {0,0,0,1},
   {0,0,0,1},
   {1,0,0,1},
   {0,1,1,0}},
  
  // 6
  {{0,1,1,0},
   {1,0,0,0},
   {1,0,0,0},
   {1,1,1,0},
   {1,0,0,1},
   {1,0,0,1},
   {0,1,1,0}},
  
  // 7
  {{1,1,1,1},
   {0,0,0,1},
   {0,0,1,0},
   {0,0,1,0},
   {0,1,0,0},
   {0,1,0,0},
   {0,1,0,0}},
  
  // 8
  {{0,1,1,0},
   {1,0,0,1},
   {1,0,0,1},
   {0,1,1,0},
   {1,0,0,1},
   {1,0,0,1},
   {0,1,1,0}},
  
  // 9
  {{0,1,1,0},
   {1,0,0,1},
   {1,0,0,1},
   {0,1,1,1},
   {0,0,0,1},
   {0,0,0,1},
   {0,1,1,0}}
};

// Dessine un chiffre 4x7 à une position donnée
inline void drawDigit(int digit, int startX, int startY, CRGB color) {
  if (digit < 0 || digit > 9) return;
  
  for (int y = 0; y < 7; y++) {
    for (int x = 0; x < 4; x++) {
      if (DIGITS[digit][y][x] == 1) {
        int posX = startX + x;
        int posY = startY + y;
        if (posX >= 0 && posX < Config::NUM_COLS && posY >= 0 && posY < Config::NUM_ROWS) {
          _leds[xy(posX, posY)] = color;
        }
      }
    }
  }
}

inline void init() {
  FastLED.addLeds<WS2812B, Config::PIN, GRB>(_leds, Config::NUM_LEDS);
  
  // Charge la luminosité depuis NVS (défaut: 80)
  _prefs.begin("led", true);
  uint8_t brightness = _prefs.getUChar("brightness", Config::BRIGHTNESS);
  _prefs.end();

  FastLED.setBrightness(Config::BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  Serial.println("[LED] Initialized");
}

inline void setBrightness(uint8_t brightness) {
  brightness = constrain(brightness, 1, 255);
  FastLED.setBrightness(brightness);
  FastLED.show();
  
  // Sauvegarde en NVS
  _prefs.begin("led", false);
  _prefs.putUChar("brightness", brightness);
  _prefs.end();
  
  Serial.printf("[LED] Brightness set to %d\n", brightness);
}

inline uint8_t getBrightness() {
  return FastLED.getBrightness();
}

inline void update(const Score& score) {
  FastLED.clear();
  
  // Couleurs
  CRGB colorA = CRGB(255, 68, 0);   // Orange
  CRGB colorB = CRGB(0, 136, 255);  // Bleu
  
  // Layout sur 24 pixels de large :
  // [dizainesA(4px)][gap(1px)][unitésA(4px)][gap(2px)][dizainesB(4px)][gap(1px)][unitésB(4px)] = 20px
  // Centré : (24 - 20) / 2 = 2 pixels de marge à gauche
  
  int startX = 2;  // Marge gauche pour centrer
  int startY = 0;  // Aligné en haut
  
  // Score A (dizaines puis unités avec gap de 1px)
  int dizA = score.scoreA / 10;
  int uniA = score.scoreA % 10;
  drawDigit(dizA, startX, startY, colorA);      // Dizaines A à x=2
  drawDigit(uniA, startX + 5, startY, colorA);  // Unités A à x=7 (4 + 1 gap)
  
  // Gap de 2 pixels entre les équipes
  
  // Score B (dizaines puis unités avec gap de 1px)
  int dizB = score.scoreB / 10;
  int uniB = score.scoreB % 10;
  drawDigit(dizB, startX + 11, startY, colorB); // Dizaines B à x=13 (7 + 4 + 2)
  drawDigit(uniB, startX + 16, startY, colorB); // Unités B à x=18 (13 + 4 + 1)
  
  // Sets (en bas, ligne 7, petits carrés)
  // Team A : 2 pixels à gauche
  if (score.setA > 0) _leds[xy(1, 7)] = colorA;
  if (score.setA > 1) _leds[xy(2, 7)] = colorA;
  
  // Team B : 2 pixels à droite
  if (score.setB > 0) _leds[xy(21, 7)] = colorB;
  if (score.setB > 1) _leds[xy(22, 7)] = colorB;
  
  FastLED.show();
}

} // namespace LED