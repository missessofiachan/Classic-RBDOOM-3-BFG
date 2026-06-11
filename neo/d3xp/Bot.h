/*
===========================================================================
Doom 3 BFG Edition GPL Source Code
===========================================================================
*/

#ifndef __BOT_H__
#define __BOT_H__

// A clean struct to hold our bot's brain state without cluttering idPlayer
struct BotState {
  // Performance Throttling
  int nextPathTime;
  idVec3 cachedMoveDir;
  bool cachedNeedJump;

  // Combat & Weapon Systems
  int nextWeaponSwitchTime;

  // Combat Movement
  int strafeDir;
  int nextStrafeChangeTime;

  // Constructor to initialize everything safely
  BotState() {
    nextPathTime = 0;
    cachedMoveDir.Zero();
    cachedNeedJump = false;
    nextWeaponSwitchTime = 0;
    strafeDir = 1;
    nextStrafeChangeTime = 0;
  }
};

#endif /* !__BOT_H__ */