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
  uint8_t firstServer = 0;  // 0 = Team A serves first, 1 = Team B serves first
  // Final scores of completed sets (for Firebase WRITE mode)
  uint8_t histA[3] = {0, 0, 0};
  uint8_t histB[3] = {0, 0, 0};

  void incrementA() { if (scoreA < 99) scoreA++; }
  void incrementB() { if (scoreB < 99) scoreB++; }
  void decrementA() { if (scoreA > 0) scoreA--; }
  void decrementB() { if (scoreB > 0) scoreB--; }

  void nextSet() {
    uint8_t idx = setA + setB;  // 0-based index of the set just finished
    if (idx < 3) {
      histA[idx] = scoreA;
      histB[idx] = scoreB;
    }
    if (scoreA > scoreB) setA++;
    else if (scoreB > scoreA) setB++;
    scoreA = 0;
    scoreB = 0;
  }

  void reset() {
    scoreA = 0; scoreB = 0;
    setA   = 0; setB   = 0;
    firstServer = 0;
    memset(histA, 0, sizeof(histA));
    memset(histB, 0, sizeof(histB));
  }
};

// Serve state derived from score
struct ServeInfo {
  bool teamAServing;
  uint8_t servesLeft;   // serves remaining this rotation
  uint8_t serveTotal;   // total serves in this rotation (1 or 2)
};

// Returns who is currently serving and how many serves remain this rotation.
// Rules:
//   - First serve of the game: firstServer team gets 1 serve
//   - Then teams alternate in groups of 2
//   - Deuce (both >= TARGET-1): 1 serve each, alternating
inline ServeInfo getServeInfo(const Score& score) {
  static const uint8_t TARGET = 21;
  uint8_t total = score.scoreA + score.scoreB;
  bool firstIsA = (score.firstServer == 0);

  // Deuce: both at TARGET-1 or beyond — 1 serve each
  if (score.scoreA >= TARGET - 1 && score.scoreB >= TARGET - 1) {
    uint8_t deuceStart = (uint8_t)((TARGET - 1) * 2);
    uint8_t deucePoints = total - deuceStart;
    // Who serves at the start of deuce? Continue the normal rotation
    bool firstAtDeuce;
    if (deuceStart == 0) {
      firstAtDeuce = firstIsA;
    } else {
      uint8_t group = (uint8_t)((deuceStart - 1) / 2);
      firstAtDeuce = (group % 2 == 0) ? !firstIsA : firstIsA;
    }
    return {(deucePoints % 2 == 0) ? firstAtDeuce : !firstAtDeuce, 1, 1};
  }

  if (total == 0) return {firstIsA, 1, 1};  // first serve: 1 only

  uint8_t group = (uint8_t)((total - 1) / 2);
  uint8_t pos   = (uint8_t)((total - 1) % 2);   // 0=first of group, 1=second
  bool teamAServing = (group % 2 == 0) ? !firstIsA : firstIsA;
  return {teamAServing, (uint8_t)(2 - pos), 2};
}

// Instance globale
extern Score currentScore;
extern SemaphoreHandle_t scoreMutex;
