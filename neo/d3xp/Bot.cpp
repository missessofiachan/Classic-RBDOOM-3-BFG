/* Statically linked to:                      │
│   └── bot.lib (The AI Library)
===========================================================================
Doom 3 BFG Edition GPL Source Code
===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "../aas/AASFile.h"
#include "Game_local.h"
#include "Bot.h"

idCVar bot_skill(
    "bot_skill", "1", CVAR_INTEGER | CVAR_GAME,
    "Bot skill level (0 = Easy, 1 = Medium, 2 = Hard, 3 = Nightmare)");

/*
==================
idBotState::idBotState
==================
*/
idBotState::idBotState(idPlayer* owner) {
	client = owner;
	botTargetItem = NULL;
	botNextTargetSearchTime = 0;
	botHasTargetPos = false;
	botWanderYaw = 0.0f;
	botWanderTime = 0;
	botNextPathTime = 0;
	botCachedMoveDir.Zero();
	botCachedNeedJump = false;
	botNextWeaponSwitchTime = 0;
	botStrafeDir = 1;
	botNextStrafeChangeTime = 0;
}

idBotState::~idBotState() {
}

void idBotState::Save(idSaveGame *savefile) const {
	savefile->WriteObject(botTargetItem);
	savefile->WriteInt(botNextTargetSearchTime);
	savefile->WriteVec3(botTargetPos);
	savefile->WriteBool(botHasTargetPos);
	savefile->WriteFloat(botWanderYaw);
	savefile->WriteInt(botWanderTime);
	savefile->WriteInt(botNextPathTime);
	savefile->WriteVec3(botCachedMoveDir);
	savefile->WriteBool(botCachedNeedJump);
	savefile->WriteInt(botNextWeaponSwitchTime);
	savefile->WriteInt(botStrafeDir);
	savefile->WriteInt(botNextStrafeChangeTime);
}

void idBotState::Restore(idRestoreGame *savefile) {
	savefile->ReadObject(reinterpret_cast<idClass *&>(botTargetItem));
	savefile->ReadInt(botNextTargetSearchTime);
	savefile->ReadVec3(botTargetPos);
	savefile->ReadBool(botHasTargetPos);
	savefile->ReadFloat(botWanderYaw);
	savefile->ReadInt(botWanderTime);
	savefile->ReadInt(botNextPathTime);
	savefile->ReadVec3(botCachedMoveDir);
	savefile->ReadBool(botCachedNeedJump);
	savefile->ReadInt(botNextWeaponSwitchTime);
	savefile->ReadInt(botStrafeDir);
	savefile->ReadInt(botNextStrafeChangeTime);
}

struct WeaponFuzzyRule {
  const char* weaponName;
  float optimalDist;
  float maxDist;
  float baseWeight;
};

static const WeaponFuzzyRule fuzzyRules[] = {
  { "weapon_shotgun", 150.0f, 600.0f, 10.0f },
  { "weapon_doublebarrel", 100.0f, 400.0f, 11.0f },
  { "weapon_rocketlauncher", 800.0f, 2000.0f, 9.0f },
  { "weapon_chaingun", 500.0f, 1500.0f, 8.5f },
  { "weapon_plasma", 600.0f, 1500.0f, 8.5f },
  { "weapon_machinegun", 800.0f, 1800.0f, 6.0f },
  { "weapon_bfg", 1200.0f, 3000.0f, 12.0f },
  { "weapon_pistol", 500.0f, 1000.0f, 1.0f }
};

/*
==================
idBotState::EvaluateBestWeapon
(Helper method for the bot to choose its weapon based on distance)
==================
*/
int idBotState::EvaluateBestWeapon(float enemyDist) {
  int bestWeapon = client->weapon_fists;
  float bestScore = -1.0f;

  for (int wp = 0; wp < MAX_WEAPONS; wp++) {
    const char *weapName = client->botWeaponNames[wp].c_str();
    if (weapName[0] == '\0') {
      continue;
    }

    if (!(client->inventory.weapons & (1 << wp))) {
      continue;
    }

    if (client->inventory.HasAmmo(weapName, true, client) == 0) {
      continue;
    }

    float score = 0.1f;
    for (int r = 0; r < sizeof(fuzzyRules)/sizeof(fuzzyRules[0]); r++) {
      if (idStr::Icmp(weapName, fuzzyRules[r].weaponName) == 0) {
        float diff = idMath::Fabs(enemyDist - fuzzyRules[r].optimalDist);
        if (diff > fuzzyRules[r].maxDist) {
          score = fuzzyRules[r].baseWeight * 0.1f;
        } else {
          float distFactor = 1.0f - (diff / fuzzyRules[r].maxDist);
          score = fuzzyRules[r].baseWeight * distFactor;
        }
        
        // Safety boundary to prevent bot from splashing itself
        if (idStr::Icmp(weapName, "weapon_rocketlauncher") == 0 && enemyDist < 200.0f) {
           score = 0.0f;
        }
        break;
      }
    }

    if (score > bestScore) {
      bestScore = score;
      bestWeapon = wp;
    }
  }
  return bestWeapon;
}

/*
==================
Helper methods for BotAI to keep functions under 50 lines per ECC guidelines
==================
*/
void idBotState::BotSensoryAcquisition(botFrameState_t& state) {
  state.nearestEnemy = NULL;
  float bestEnemyDistSqr = 1e12f;
  state.enemyVisible = false;
  idVec3 myEye = client->GetEyePosition();
  idVec3 myOrigin = client->GetPhysics()->GetOrigin();

  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i == client->entityNumber) continue;

    idEntity *ent = gameLocal->entities[i];
    if (!ent || !ent->IsType(idPlayer::Type)) continue;

    idPlayer *other = static_cast<idPlayer *>(ent);
    if (other->health <= 0 || other->spectating) continue;
    if (gameLocal->mpGame.IsGametypeTeamBased() && other->team == client->team) continue;

    float distSqr = (other->GetPhysics()->GetOrigin() - myOrigin).LengthSqr();

    if (state.enemyVisible && distSqr >= bestEnemyDistSqr) {
      continue;
    }

    trace_t tr;
    gameLocal->GetClip()->TracePoint(tr, myEye, other->GetEyePosition(), MASK_SHOT_RENDERMODEL, client);
    bool currentEnemyVisible = (tr.fraction >= 1.0f || tr.c.entityNum == other->entityNumber);

    if ((currentEnemyVisible && !state.enemyVisible) ||
        (currentEnemyVisible == state.enemyVisible && distSqr < bestEnemyDistSqr)) {
      bestEnemyDistSqr = distSqr;
      state.nearestEnemy = other;
      state.enemyVisible = currentEnemyVisible;
    }
  }
  state.nearestEnemyDist = (state.nearestEnemy != NULL) ? idMath::Sqrt(bestEnemyDistSqr) : 999999.0f;
}

void idBotState::BotTargetPositionSelection(botFrameState_t& state) {
  idVec3 myEye = client->GetEyePosition();
  idVec3 myOrigin = client->GetPhysics()->GetOrigin();

  idAAS *aas = gameLocal->GetAAS("aas32");
  if (!aas) aas = gameLocal->GetAAS("aas48");
  if (!aas) {
    for (int i = 0; i < 8; ++i) {
      idAAS *a = gameLocal->GetAAS(i);
      if (a && a->GetSettings()) { aas = a; break; }
    }
  }

  bool isCTF = gameLocal->mpGame.IsGametypeFlagBased();
  idItemTeam* friendlyFlag = isCTF ? gameLocal->mpGame.GetTeamFlag(client->team) : NULL;
  idItemTeam* enemyFlag = isCTF ? gameLocal->mpGame.GetTeamFlag(1 - client->team) : NULL;

  if (isCTF && friendlyFlag && enemyFlag) {
    int enemyCarrierIdx = gameLocal->mpGame.GetFlagCarrier(1 - client->team);
    idPlayer* enemyCarrier = (enemyCarrierIdx != -1) ? static_cast<idPlayer*>(gameLocal->entities[enemyCarrierIdx]) : NULL;

    if (client->carryingFlag) {
      botTargetPos = friendlyFlag->GetReturnOrigin();
      botHasTargetPos = true;
    } else if (friendlyFlag->carried && enemyCarrier) {
      botTargetPos = enemyCarrier->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    } else if (friendlyFlag->dropped) {
      botTargetPos = friendlyFlag->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    } else if (enemyFlag->dropped) {
      botTargetPos = enemyFlag->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    } else {
      int teamSize = 0;
      int myTeamRank = 0;
      for (int k = 0; k < gameLocal->numClients; k++) {
        idEntity* ent = gameLocal->entities[k];
        if (ent && ent->IsType(idPlayer::Type)) {
          idPlayer* player = static_cast<idPlayer*>(ent);
          if (player->team == client->team && player->isBot) {
            teamSize++;
            if (k < client->entityNumber) myTeamRank++;
          }
        }
      }

      bool isDefender = (teamSize >= 3) && (myTeamRank % 5 == 0);
      if (isDefender) {
        if (state.enemyVisible && state.nearestEnemy && state.nearestEnemyDist < 600.0f) {
          botTargetPos = state.nearestEnemy->GetPhysics()->GetOrigin();
        } else {
          botTargetPos = friendlyFlag->GetReturnOrigin();
        }
        botHasTargetPos = true;
      } else {
        botTargetPos = enemyFlag->GetReturnOrigin();
        botHasTargetPos = true;
      }
    }
  } else {
    if (state.enemyVisible && state.nearestEnemy) {
      botTargetPos = state.nearestEnemy->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    } else {
      if (gameLocal->time >= botNextTargetSearchTime || !botTargetItem.GetEntity() || botTargetItem->IsHidden()) {
        botNextTargetSearchTime = gameLocal->time + 1000;
        idEntity *bestItem = NULL;
        float bestItemDistSqr = 1e12f;

        for (int j = 0; j < gameLocal->num_entities; j++) {
          idEntity *ent = gameLocal->entities[j];
          if (!ent || ent->IsHidden() || !ent->IsType(idItem::Type)) continue;

          idItem *item = static_cast<idItem *>(ent);
          if (!client->GiveItem(item, 0)) continue;

          float dSqr = (ent->GetPhysics()->GetOrigin() - myOrigin).LengthSqr();
          if (dSqr < bestItemDistSqr) {
            bestItemDistSqr = dSqr;
            bestItem = ent;
          }
        }
        botTargetItem = bestItem;
      }

      if (aas) {
        if (botTargetItem.GetEntity()) {
          botTargetPos = botTargetItem->GetPhysics()->GetOrigin();
          botHasTargetPos = true;
        } else if (state.nearestEnemy) {
          botTargetPos = state.nearestEnemy->GetPhysics()->GetOrigin();
          botHasTargetPos = true;
        } else {
          botHasTargetPos = false;
        }
      } else {
        bool itemVisible = false;
        if (botTargetItem.GetEntity()) {
          trace_t trItem;
          gameLocal->GetClip()->Translation(trItem, myEye, botTargetItem->GetPhysics()->GetOrigin() + idVec3(0, 0, 16), NULL, mat3_identity, MASK_SHOT_RENDERMODEL, client);
          if (trItem.fraction >= 1.0f) itemVisible = true;
        }

        if (itemVisible && botTargetItem.GetEntity()) {
          botTargetPos = botTargetItem->GetPhysics()->GetOrigin();
          botHasTargetPos = true;
        } else {
          botHasTargetPos = false;
        }
      }
    }
  }
}

void idBotState::BotNavigationPathfinding(botFrameState_t& state) {
  state.moveDir = idVec3(0, 0, 0);
  state.hasMoveDir = false;
  state.needJump = false;

  idVec3 myOrigin = client->GetPhysics()->GetOrigin();
  idAAS *aas = gameLocal->GetAAS("aas32");
  if (!aas) aas = gameLocal->GetAAS("aas48");
  if (!aas) {
    for (int i = 0; i < 8; ++i) {
      idAAS *a = gameLocal->GetAAS(i);
      if (a && a->GetSettings()) { aas = a; break; }
    }
  }

  if (gameLocal->time >= botNextPathTime || botCachedMoveDir == vec3_origin) {
    botNextPathTime = gameLocal->time + 100 + gameLocal->random.RandomInt(50);
    botCachedMoveDir.Zero();
    botCachedNeedJump = false;

    if (aas && botHasTargetPos) {
      idBounds myBounds = client->GetPhysics()->GetBounds();
      int myAreaNum = aas->PointReachableAreaNum(myOrigin, myBounds, AREA_REACHABLE_WALK);
      int targetAreaNum = aas->PointReachableAreaNum(botTargetPos, myBounds, AREA_REACHABLE_WALK);

      if (myAreaNum > 0 && targetAreaNum > 0) {
        aasPath_t path;
        idVec3 org = myOrigin;
        aas->PushPointIntoAreaNum(myAreaNum, org);
        idVec3 goal = botTargetPos;
        aas->PushPointIntoAreaNum(targetAreaNum, goal);

        if (aas->WalkPathToGoal(path, myAreaNum, org, targetAreaNum, goal, TFL_WALK | TFL_AIR)) {
          botCachedMoveDir = path.moveGoal - myOrigin;
          if (path.type == PATHTYPE_BARRIERJUMP || path.type == PATHTYPE_JUMP || botCachedMoveDir.z > 24.0f) {
            botCachedNeedJump = true;
          }
        }
      }
    }
  }

  if (botCachedMoveDir == vec3_origin) {
    if (botHasTargetPos) {
      state.moveDir = botTargetPos - myOrigin;
      state.hasMoveDir = true;
    } else {
      idVec3 forwardDir = idAngles(0, botWanderYaw, 0).ToForward();
      trace_t tr;
      gameLocal->GetClip()->Translation(tr, myOrigin + idVec3(0, 0, 16), myOrigin + idVec3(0, 0, 16) + forwardDir * 80.0f, client->GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, client);

      bool stuck = (client->GetPhysics()->GetLinearVelocity().Length() < 5.0f && gameLocal->time > botWanderTime - 1500);

      if (tr.fraction < 1.0f || stuck) {
        float yawLeft = botWanderYaw + 90.0f;
        float yawRight = botWanderYaw - 90.0f;

        idVec3 dirLeft = idAngles(0, yawLeft, 0).ToForward();
        idVec3 dirRight = idAngles(0, yawRight, 0).ToForward();

        trace_t trLeft, trRight;
        gameLocal->GetClip()->Translation(trLeft, myOrigin + idVec3(0, 0, 16), myOrigin + idVec3(0, 0, 16) + dirLeft * 80.0f, client->GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, client);
        gameLocal->GetClip()->Translation(trRight, myOrigin + idVec3(0, 0, 16), myOrigin + idVec3(0, 0, 16) + dirRight * 80.0f, client->GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, client);

        if (trLeft.fraction < 0.3f && trRight.fraction < 0.3f) {
          botWanderYaw += 180.0f + gameLocal->random.CRandomFloat() * 30.0f;
        } else if (trLeft.fraction >= trRight.fraction) {
          botWanderYaw = yawLeft + gameLocal->random.CRandomFloat() * 15.0f;
        } else {
          botWanderYaw = yawRight + gameLocal->random.CRandomFloat() * 15.0f;
        }
        botWanderYaw = idMath::AngleNormalize360(botWanderYaw);
        botWanderTime = gameLocal->time + 1000 + gameLocal->random.RandomInt(1001);

        if (stuck) state.needJump = true;
      } else if (gameLocal->time >= botWanderTime) {
        botWanderTime = gameLocal->time + 2000 + gameLocal->random.RandomInt(2001);
        botWanderYaw = gameLocal->random.RandomFloat() * 360.0f;
      }

      state.moveDir = idAngles(0, botWanderYaw, 0).ToForward();
      state.hasMoveDir = true;
    }
  } else {
    state.moveDir = botCachedMoveDir;
    state.needJump = botCachedNeedJump;
    state.hasMoveDir = true;
  }
}

void idBotState::BotAimingAndRotation(usercmd_t& cmd, botFrameState_t& state) {
  int skill = bot_skill.GetInteger();
  float maxRotationPerFrame = 360.0f;
  
  if (skill == 0) maxRotationPerFrame = 6.0f;
  else if (skill == 1) maxRotationPerFrame = 15.0f;
  else if (skill == 2) maxRotationPerFrame = 45.0f;

  idVec3 myEye = client->GetEyePosition();

  if (state.enemyVisible && state.nearestEnemy) {
    idVec3 enemyPos = state.nearestEnemy->GetEyePosition();
    idVec3 enemyVel = state.nearestEnemy->GetPhysics()->GetLinearVelocity();

    float projSpeed = 99999.0f;
    const char *currentWeaponName = "";
    int curWep = client->idealWeapon.Get(); // using idealWeapon for the bot
    if (curWep >= 0 && curWep < MAX_WEAPONS) {
      currentWeaponName = client->botWeaponNames[curWep].c_str();
    }
    if (idStr::Icmp(currentWeaponName, "weapon_rocketlauncher") == 0) projSpeed = 900.0f;
    else if (idStr::Icmp(currentWeaponName, "weapon_plasma") == 0 || idStr::Icmp(currentWeaponName, "weapon_bfg") == 0) projSpeed = 800.0f;

    float dist = (enemyPos - myEye).Length();
    float timeToHit = dist / projSpeed;
    idVec3 predictedPos = enemyPos + (enemyVel * timeToHit);

    idVec3 dirToEnemy = predictedPos - myEye;
    dirToEnemy.Normalize();
    idAngles faceAngles = dirToEnemy.ToAngles();

    idAngles currentAngles = client->viewAngles;
    idAngles delta = faceAngles - currentAngles;
    delta.Normalize180();

    faceAngles[0] = currentAngles[0] + idMath::ClampFloat(-maxRotationPerFrame, maxRotationPerFrame, delta[0]);
    faceAngles[1] = currentAngles[1] + idMath::ClampFloat(-maxRotationPerFrame, maxRotationPerFrame, delta[1]);
    faceAngles[2] = 0;
    faceAngles.Normalize180();

    cmd.angles[0] = ANGLE2SHORT(faceAngles[0]);
    cmd.angles[1] = ANGLE2SHORT(faceAngles[1]);
    cmd.angles[2] = 0;
  } else if (state.hasMoveDir) {
    idVec3 lookDir = state.moveDir;
    lookDir.z = 0;
    if (lookDir.Length() > 0.1f) {
      lookDir.Normalize();
      idAngles faceAngles = lookDir.ToAngles();

      idAngles currentAngles = client->viewAngles;
      idAngles delta = faceAngles - currentAngles;
      delta.Normalize180();

      faceAngles[1] = currentAngles[1] + idMath::ClampFloat(-15.0f, 15.0f, delta[1]);
      faceAngles[0] = 0;
      faceAngles[2] = 0;
      faceAngles.Normalize180();

      cmd.angles[0] = ANGLE2SHORT(faceAngles[0]);
      cmd.angles[1] = ANGLE2SHORT(faceAngles[1]);
      cmd.angles[2] = 0;
    }
  }
}

void idBotState::BotCombatStrafing(usercmd_t& cmd, botFrameState_t& state) {
  if (state.enemyVisible && state.nearestEnemy) {
    if (gameLocal->time >= botNextStrafeChangeTime) {
      botNextStrafeChangeTime = gameLocal->time + 800 + gameLocal->random.RandomInt(700);
      botStrafeDir = (gameLocal->random.RandomFloat() > 0.5f) ? 1 : -1;
    }

    float idealDist = 400.0f;
    const char *currentWeaponName = "";
    int curWep = client->idealWeapon.Get();
    if (curWep >= 0 && curWep < MAX_WEAPONS) {
      currentWeaponName = client->botWeaponNames[curWep].c_str();
    }
    if (idStr::Icmp(currentWeaponName, "weapon_shotgun") == 0 || idStr::Icmp(currentWeaponName, "weapon_doublebarrel") == 0) {
      idealDist = 120.0f;
    } else if (idStr::Icmp(currentWeaponName, "weapon_rocketlauncher") == 0 || idStr::Icmp(currentWeaponName, "weapon_machinegun") == 0) {
      idealDist = 600.0f;
    }

    int forwardSpeed = 0;
    if (state.nearestEnemyDist > idealDist + 80.0f) forwardSpeed = 127;
    else if (state.nearestEnemyDist < idealDist - 80.0f) forwardSpeed = -127;

    cmd.forwardmove = forwardSpeed;
    cmd.rightmove = botStrafeDir * 127;
  } else if (state.hasMoveDir) {
    idVec3 desiredVelocity = state.moveDir;
    desiredVelocity.z = 0;
    if (desiredVelocity.Length() > 0.1f) {
      desiredVelocity.Normalize();
      idAngles viewYaw(0, client->viewAngles.yaw, 0);
      idVec3 forward, right;
      viewYaw.ToVectors(&forward, &right);

      float forwardSpeed = desiredVelocity * forward;
      float rightSpeed = desiredVelocity * right;

      cmd.forwardmove = idMath::ClampInt(-127, 127, (int)(forwardSpeed * 127.0f));
      cmd.rightmove = idMath::ClampInt(-127, 127, (int)(rightSpeed * 127.0f));
    }
  }
}

void idBotState::BotActionAndEvasion(usercmd_t& cmd, botFrameState_t& state) {
  int skill = bot_skill.GetInteger();
  bool skipShoot = false;
  if (skill == 0) skipShoot = ((gameLocal->time % 10) < 4);
  else if (skill == 1) skipShoot = ((gameLocal->time % 10) < 2);

  idVec3 myOrigin = client->GetPhysics()->GetOrigin();

  if (state.enemyVisible && state.nearestEnemy) {
    if (gameLocal->time >= botNextWeaponSwitchTime) {
      botNextWeaponSwitchTime = gameLocal->time + 500;
      int idealWep = EvaluateBestWeapon(state.nearestEnemyDist);
      if (idealWep != client->idealWeapon.Get()) {
        cmd.impulse = idealWep;
        cmd.impulseSequence = client->oldCmd.impulseSequence + 1;
      }
    }

    if (state.nearestEnemyDist < 1500.0f && !skipShoot) cmd.buttons |= BUTTON_ATTACK;

    if (skill > 0) {
      int jumpInterval = (skill >= 3) ? 1200 : 2500;
      if ((gameLocal->time % jumpInterval) < 150) state.needJump = true;
    }

    if (skill >= 2 && (gameLocal->time % 3000) < 400 && state.nearestEnemyDist > 300.0f) {
      cmd.buttons |= BUTTON_CROUCH;
    }
  } else if (client->GetPhysics()->GetLinearVelocity().Length() < 5.0f && (gameLocal->time % 2000) < 200) {
    idVec3 moveDirVec = client->GetPhysics()->GetLinearVelocity();
    if (moveDirVec.Normalize() < 0.1f) {
      idAngles viewYaw(0, client->viewAngles.yaw, 0);
      idVec3 forward, right;
      viewYaw.ToVectors(&forward, &right);
      moveDirVec = forward * (cmd.forwardmove / 127.0f) + right * (cmd.rightmove / 127.0f);
      moveDirVec.Normalize();
    }
    
    trace_t trForward;
    idVec3 start = myOrigin + idVec3(0, 0, 8);
    idVec3 end = start + moveDirVec * 32.0f;
    gameLocal->GetClip()->TracePoint(trForward, start, end, MASK_PLAYERSOLID, client);
    
    if (trForward.fraction < 1.0f) {
      float maxJumpHeight = 48.0f;
      idVec3 testTopStart = trForward.endpos + idVec3(0, 0, maxJumpHeight);
      idVec3 testTopEnd = trForward.endpos;
      
      trace_t trHeight;
      gameLocal->GetClip()->TracePoint(trHeight, testTopStart, testTopEnd, MASK_PLAYERSOLID, client);
      
      float obstacleHeight = trHeight.endpos.z - myOrigin.z;
      if (obstacleHeight > 0.0f && obstacleHeight <= maxJumpHeight) {
        state.needJump = true;
      } else {
        state.needJump = false;
        botWanderYaw = idMath::AngleNormalize360(botWanderYaw + 90.0f + gameLocal->random.CRandomFloat() * 45.0f);
        botWanderTime = gameLocal->time + 1000;
      }
    } else {
      state.needJump = true;
    }
  }
}

void idBotState::BotLedgeAvoidance(usercmd_t& cmd, botFrameState_t& state) {
  if (client->GetPhysics()->HasGroundContacts() && (cmd.forwardmove != 0 || cmd.rightmove != 0)) {
    idAngles viewYaw(0, client->viewAngles.yaw, 0);
    idVec3 forward, right;
    viewYaw.ToVectors(&forward, &right);
    idVec3 moveDir = forward * (cmd.forwardmove / 127.0f) + right * (cmd.rightmove / 127.0f);
    moveDir.z = 0;
    if (moveDir.Normalize() > 0.1f) {
      bool bypass = false;
      if (botCachedMoveDir != vec3_origin) {
        idVec3 pathDir = botCachedMoveDir;
        pathDir.z = 0;
        if (pathDir.Normalize() > 0.1f && moveDir * pathDir > 0.707f) bypass = true;
      }

      if (!bypass) {
        trace_t tr;
        idVec3 myOrigin = client->GetPhysics()->GetOrigin();
        idVec3 testPos = myOrigin + moveDir * 24.0f;
        idVec3 start = testPos + idVec3(0, 0, 18.0f);
        idVec3 end = testPos - idVec3(0, 0, 64.0f);
        gameLocal->GetClip()->TracePoint(tr, start, end, MASK_PLAYERSOLID, client);

        if (tr.fraction >= 1.0f || (start.z - tr.endpos.z) > (18.0f + 40.0f)) {
          state.needJump = false;
          cmd.buttons &= ~BUTTON_JUMP;

          if (state.enemyVisible && state.nearestEnemy) {
            botStrafeDir = -botStrafeDir;
            botNextStrafeChangeTime = gameLocal->time + 1000;
            cmd.rightmove = botStrafeDir * 127;
            cmd.forwardmove = -127;
          } else {
            botWanderYaw = idMath::AngleNormalize360(botWanderYaw + 180.0f + gameLocal->random.CRandomFloat() * 45.0f);
            botWanderTime = gameLocal->time + 1500;
            cmd.forwardmove = 0;
            cmd.rightmove = 0;
          }
        }
      }
    }
  }
}

/*
==================
idBotState::Think
==================
*/
void idBotState::Think(usercmd_t &cmd) {
  cmd.buttons = 0;
  cmd.forwardmove = 0;
  cmd.rightmove = 0;
  cmd.impulse = 0;

  if (client->spectating || client->health <= 0) {
    cmd.buttons |= BUTTON_ATTACK;
    return;
  }

  botFrameState_t state;
  BotSensoryAcquisition(state);
  BotTargetPositionSelection(state);
  BotNavigationPathfinding(state);
  BotAimingAndRotation(cmd, state);
  BotCombatStrafing(cmd, state);
  BotActionAndEvasion(cmd, state);
  BotLedgeAvoidance(cmd, state);

  if (state.needJump) {
    cmd.buttons |= BUTTON_JUMP;
  }
}