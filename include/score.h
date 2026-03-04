/**
 * score.h - Structure score minimale
 */

#pragma once
#include <Arduino.h>

struct Score {
  uint8_t scoreA = 0;
  uint8_t scoreB = 0;
  uint8_t setA   = 0;
  uint8_t setB   = 0;
  
  void incrementA() { if (scoreA < 99) scoreA++; }
  void incrementB() { if (scoreB < 99) scoreB++; }
  void decrementA() { if (scoreA > 0) scoreA--; }
  void decrementB() { if (scoreB > 0) scoreB--; }
  
  void nextSet() {
    if (scoreA > scoreB) setA++;
    else if (scoreB > scoreA) setB++;
    scoreA = 0;
    scoreB = 0;
  }
  
  void reset() {
    scoreA = 0;
    scoreB = 0;
    setA = 0;
    setB = 0;
  }
};

// Instance globale
extern Score currentScore;
