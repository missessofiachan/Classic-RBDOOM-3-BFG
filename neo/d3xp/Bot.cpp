/* Statically linked to:                      │
│   └── bot.lib (The AI Library)
===========================================================================
Doom 3 BFG Edition GPL Source Code
===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "Bot.h"
#include "../aas/AASFile.h"
#include "Game_local.h"

idCVar bot_skill(
    "bot_skill", "1", CVAR_INTEGER | CVAR_GAME,
    "Bot skill level (0 = Easy, 1 = Medium, 2 = Hard, 3 = Nightmare)");

/*
==================
idPlayer::EvaluateBestWeapon
(Helper method for the bot to choose its weapon based on distance)
==================
*/
int idPlayer::EvaluateBestWeapon(float enemyDist) {
  int bestWeapon = weapon_fists;
  float bestScore = -1.0f;

  for (int wp = 0; wp < MAX_WEAPONS; wp++) {
    const char *weapName = spawnArgs.GetString(va("def_weapon%d", wp));
    if (weapName[0] == '\0') {
      continue;
    }

    if (!(inventory.weapons & (1 << wp))) {
      continue;
    }

    if (inventory.HasAmmo(weapName, true, this) == 0) {
      continue;
    }

    float score = 0.0f;
    if (idStr::Icmp(weapName, "weapon_shotgun") == 0 ||
        idStr::Icmp(weapName, "weapon_doublebarrel") == 0) {
      if (enemyDist < 250.0f)
        score = 10.0f;
      else if (enemyDist < 500.0f)
        score = 4.0f;
      else
        score = 0.5f;
    } else if (idStr::Icmp(weapName, "weapon_rocketlauncher") == 0) {
      if (enemyDist < 200.0f)
        score = 0.0f;
      else if (enemyDist < 1000.0f)
        score = 9.0f;
      else
        score = 5.0f;
    } else if (idStr::Icmp(weapName, "weapon_chaingun") == 0 ||
               idStr::Icmp(weapName, "weapon_plasma") == 0) {
      if (enemyDist < 800.0f)
        score = 8.5f;
      else
        score = 3.0f;
    } else if (idStr::Icmp(weapName, "weapon_machinegun") == 0) {
      if (enemyDist < 1200.0f)
        score = 6.0f;
      else
        score = 2.0f;
    } else if (idStr::Icmp(weapName, "weapon_bfg") == 0) {
      if (enemyDist > 300.0f)
        score = 12.0f;
    } else if (idStr::Icmp(weapName, "weapon_pistol") == 0) {
      score = 1.0f;
    } else {
      score = 0.1f;
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
idPlayer::BotAI
==================
*/
void idPlayer::BotAI(usercmd_t &cmd) {
  // Clear the command
  cmd.buttons = 0;
  cmd.forwardmove = 0;
  cmd.rightmove = 0;
  cmd.impulse = 0;

  if (spectating || health <= 0) {
    cmd.buttons |= BUTTON_ATTACK;
    return;
  }

  // 1. SENSORY & TARGET ACQUISITION (Optimized Squared Distance)
  idPlayer *nearestEnemy = NULL;
  float bestEnemyDistSqr = 1e12f;
  bool enemyVisible = false;
  idVec3 myEye = GetEyePosition();
  idVec3 myOrigin = GetPhysics()->GetOrigin();

  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i == entityNumber)
      continue;

    idEntity *ent = gameLocal->entities[i];
    if (!ent || !ent->IsType(idPlayer::Type))
      continue;

    idPlayer *other = static_cast<idPlayer *>(ent);
    if (other->health <= 0 || other->spectating)
      continue;
    if (gameLocal->mpGame.IsGametypeTeamBased() && other->team == team)
      continue;

    float distSqr = (other->GetPhysics()->GetOrigin() - myOrigin).LengthSqr();

    trace_t tr;
    gameLocal->GetClip()->Translation(tr, myEye, other->GetEyePosition(), NULL,
                                      mat3_identity, MASK_SHOT_RENDERMODEL,
                                      this);
    bool currentEnemyVisible =
        (tr.fraction >= 1.0f || tr.c.entityNum == other->entityNumber);

    // Line-of-Sight Priority Selection
    if ((currentEnemyVisible && !enemyVisible) ||
        (currentEnemyVisible == enemyVisible && distSqr < bestEnemyDistSqr)) {
      bestEnemyDistSqr = distSqr;
      nearestEnemy = other;
      enemyVisible = currentEnemyVisible;
    }
  }

  float nearestEnemyDist =
      (nearestEnemy != NULL) ? idMath::Sqrt(bestEnemyDistSqr) : 999999.0f;

  // 2. TARGET POSITION SELECTION
  idAAS *aas = gameLocal->GetAAS("aas32");
  if (!aas)
    aas = gameLocal->GetAAS("aas48");
  if (!aas) {
    for (int i = 0; i < 8; ++i) {
      idAAS *a = gameLocal->GetAAS(i);
      if (a && a->GetSettings()) {
        aas = a;
        break;
      }
    }
  }

  bool isCTF = gameLocal->mpGame.IsGametypeFlagBased();
  idItemTeam* friendlyFlag = isCTF ? gameLocal->mpGame.GetTeamFlag(team) : NULL;
  idItemTeam* enemyFlag = isCTF ? gameLocal->mpGame.GetTeamFlag(1 - team) : NULL;

  if (isCTF && friendlyFlag && enemyFlag) {
    int enemyCarrierIdx = gameLocal->mpGame.GetFlagCarrier(1 - team);
    idPlayer* enemyCarrier = (enemyCarrierIdx != -1) ? static_cast<idPlayer*>(gameLocal->entities[enemyCarrierIdx]) : NULL;

    if (carryingFlag) {
      // Scenario A: Carrying enemy flag -> RUN HOME
      botTargetPos = friendlyFlag->GetReturnOrigin();
      botHasTargetPos = true;
    }
    else if (friendlyFlag->carried && enemyCarrier) {
      // Scenario B: Friendly flag is carried by an enemy -> HUNT THE CARRIER
      botTargetPos = enemyCarrier->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    }
    else if (friendlyFlag->dropped) {
      // Scenario C: Friendly flag is dropped -> RETRIEVE IT
      botTargetPos = friendlyFlag->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    }
    else if (enemyFlag->dropped) {
      // Scenario D: Enemy flag is dropped -> PICK IT UP
      botTargetPos = enemyFlag->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    }
    else {
      // Scenario E: Both flags at base
      // Calculate how many bots are on our team and our team-specific rank
      int teamSize = 0;
      int myTeamRank = 0;
      for (int k = 0; k < gameLocal->numClients; k++) {
        idEntity* ent = gameLocal->entities[k];
        if (ent && ent->IsType(idPlayer::Type)) {
          idPlayer* player = static_cast<idPlayer*>(ent);
          if (player->team == team && player->isBot) {
            teamSize++;
            if (k < entityNumber) {
              myTeamRank++;
            }
          }
        }
      }

      // 20% defenders (1 in 5). If team size is small, we prioritize attacking.
      bool isDefender = false;
      if (teamSize >= 3) {
        isDefender = (myTeamRank % 5 == 0);
      }

      if (isDefender) {
        // Defend our flag base
        if (enemyVisible && nearestEnemy && nearestEnemyDist < 600.0f) {
          botTargetPos = nearestEnemy->GetPhysics()->GetOrigin();
        } else {
          botTargetPos = friendlyFlag->GetReturnOrigin();
        }
        botHasTargetPos = true;
      } else {
        // Attack enemy flag base
        botTargetPos = enemyFlag->GetReturnOrigin();
        botHasTargetPos = true;
      }
    }
  } else {
    if (enemyVisible && nearestEnemy) {
      botTargetPos = nearestEnemy->GetPhysics()->GetOrigin();
      botHasTargetPos = true;
    } else {
      if (gameLocal->time >= botNextTargetSearchTime ||
          !botTargetItem.GetEntity() || botTargetItem->IsHidden()) {
        botNextTargetSearchTime = gameLocal->time + 1000;
        idEntity *bestItem = NULL;
        float bestItemDistSqr = 1e12f;

        for (int j = 0; j < gameLocal->num_entities; j++) {
          idEntity *ent = gameLocal->entities[j];
          if (!ent || ent->IsHidden() || !ent->IsType(idItem::Type))
            continue;

          const char* clsName = ent->GetClassname();
          if (idStr::Icmpn(clsName, "item_medkit", 11) == 0 || idStr::Icmpn(clsName, "item_health", 11) == 0) {
            if (this->health >= this->inventory.maxHealth) continue;
          }
          if (idStr::Icmpn(clsName, "item_armor", 10) == 0) {
            if (this->inventory.armor >= this->inventory.maxarmor) continue;
          }

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
        } else if (nearestEnemy) {
          botTargetPos = nearestEnemy->GetPhysics()->GetOrigin();
          botHasTargetPos = true;
        } else {
          botHasTargetPos = false;
        }
      } else {
        bool itemVisible = false;
        if (botTargetItem.GetEntity()) {
          trace_t trItem;
          gameLocal->GetClip()->Translation(
              trItem, myEye,
              botTargetItem->GetPhysics()->GetOrigin() + idVec3(0, 0, 16), NULL,
              mat3_identity, MASK_SHOT_RENDERMODEL, this);
          if (trItem.fraction >= 1.0f)
            itemVisible = true;
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

  // 3. NAVIGATION USING AAS PATHFINDING (Throttled via botState)
  idVec3 moveDir(0, 0, 0);
  bool hasMoveDir = false;
  bool needJump = false;

  if (gameLocal->time >= botState.nextPathTime ||
      botState.cachedMoveDir == vec3_origin) {
    botState.nextPathTime =
        gameLocal->time + 100 + gameLocal->random.RandomInt(50);
    botState.cachedMoveDir.Zero();
    botState.cachedNeedJump = false;

    if (aas && botHasTargetPos) {
      idBounds myBounds = GetPhysics()->GetBounds();
      int myAreaNum =
          aas->PointReachableAreaNum(myOrigin, myBounds, AREA_REACHABLE_WALK);
      int targetAreaNum = aas->PointReachableAreaNum(botTargetPos, myBounds,
                                                     AREA_REACHABLE_WALK);

      if (myAreaNum > 0 && targetAreaNum > 0) {
        aasPath_t path;
        idVec3 org = myOrigin;
        aas->PushPointIntoAreaNum(myAreaNum, org);
        idVec3 goal = botTargetPos;
        aas->PushPointIntoAreaNum(targetAreaNum, goal);

        if (aas->WalkPathToGoal(path, myAreaNum, org, targetAreaNum, goal,
                                TFL_WALK | TFL_AIR)) {
          botState.cachedMoveDir = path.moveGoal - myOrigin;
          if (path.type == PATHTYPE_BARRIERJUMP || path.type == PATHTYPE_JUMP ||
              botState.cachedMoveDir.z > 24.0f) {
            botState.cachedNeedJump = true;
          }
        }
      }
    }
  }

  if (botState.cachedMoveDir == vec3_origin) {
    if (botHasTargetPos) {
      moveDir = botTargetPos - myOrigin;
      hasMoveDir = true;
    } else {
      // Wander Fallback
      idVec3 forwardDir = idAngles(0, botWanderYaw, 0).ToForward();
      trace_t tr;
      gameLocal->GetClip()->Translation(
          tr, myOrigin + idVec3(0, 0, 16),
          myOrigin + idVec3(0, 0, 16) + forwardDir * 80.0f,
          GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, this);

      bool stuck = (physicsObj.GetLinearVelocity().Length() < 5.0f &&
                    gameLocal->time > botWanderTime - 1500);

      if (tr.fraction < 1.0f || stuck) {
        float yawLeft = botWanderYaw + 90.0f;
        float yawRight = botWanderYaw - 90.0f;

        idVec3 dirLeft = idAngles(0, yawLeft, 0).ToForward();
        idVec3 dirRight = idAngles(0, yawRight, 0).ToForward();

        trace_t trLeft, trRight;
        gameLocal->GetClip()->Translation(
            trLeft, myOrigin + idVec3(0, 0, 16),
            myOrigin + idVec3(0, 0, 16) + dirLeft * 80.0f,
            GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID,
            this);
        gameLocal->GetClip()->Translation(
            trRight, myOrigin + idVec3(0, 0, 16),
            myOrigin + idVec3(0, 0, 16) + dirRight * 80.0f,
            GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID,
            this);

        if (trLeft.fraction < 0.3f && trRight.fraction < 0.3f) {
          botWanderYaw += 180.0f + gameLocal->random.CRandomFloat() * 30.0f;
        } else if (trLeft.fraction >= trRight.fraction) {
          botWanderYaw = yawLeft + gameLocal->random.CRandomFloat() * 15.0f;
        } else {
          botWanderYaw = yawRight + gameLocal->random.CRandomFloat() * 15.0f;
        }
        botWanderYaw = idMath::AngleNormalize360(botWanderYaw);
        botWanderTime =
            gameLocal->time + 1000 + gameLocal->random.RandomInt(1001);

        if (stuck) {
          needJump = true;
        }
      } else if (gameLocal->time >= botWanderTime) {
        botWanderTime =
            gameLocal->time + 2000 + gameLocal->random.RandomInt(2001);
        botWanderYaw = gameLocal->random.RandomFloat() * 360.0f;
      }

      moveDir = idAngles(0, botWanderYaw, 0).ToForward();
      hasMoveDir = true;
    }
  } else {
    moveDir = botState.cachedMoveDir;
    needJump = botState.cachedNeedJump;
    hasMoveDir = true;
  }

  // 4. AIMING & ROTATION CONTROL
  int skill = bot_skill.GetInteger();
  float maxRotationPerFrame = 360.0f;
  bool skipShoot = false;

  if (skill == 0) {
    maxRotationPerFrame = 6.0f;
    skipShoot = ((gameLocal->time % 10) < 4);
  } else if (skill == 1) {
    maxRotationPerFrame = 15.0f;
    skipShoot = ((gameLocal->time % 10) < 2);
  } else if (skill == 2) {
    maxRotationPerFrame = 45.0f;
  }

  if (enemyVisible && nearestEnemy) {
    idVec3 enemyPos = nearestEnemy->GetEyePosition();
    idVec3 enemyVel = nearestEnemy->GetPhysics()->GetLinearVelocity();

    float projSpeed = 99999.0f; // hitscan
    const char *currentWeaponName = spawnArgs.GetString(va("def_weapon%d", currentWeapon));
    if (idStr::Icmp(currentWeaponName, "weapon_rocketlauncher") == 0) {
      projSpeed = 900.0f;
    } else if (idStr::Icmp(currentWeaponName, "weapon_plasma") == 0 || idStr::Icmp(currentWeaponName, "weapon_bfg") == 0) {
      projSpeed = 800.0f;
    }

    float dist = (enemyPos - myEye).Length();
    float timeToHit = dist / projSpeed;
    idVec3 predictedPos = enemyPos + (enemyVel * timeToHit);

    idVec3 dirToEnemy = predictedPos - myEye;
    dirToEnemy.Normalize();
    idAngles faceAngles = dirToEnemy.ToAngles();

    idAngles currentAngles = viewAngles;
    idAngles delta = faceAngles - currentAngles;
    delta.Normalize180();

    faceAngles[0] =
        currentAngles[0] +
        idMath::ClampFloat(-maxRotationPerFrame, maxRotationPerFrame, delta[0]);
    faceAngles[1] =
        currentAngles[1] +
        idMath::ClampFloat(-maxRotationPerFrame, maxRotationPerFrame, delta[1]);
    faceAngles[2] = 0;
    faceAngles.Normalize180();

    cmd.angles[0] = ANGLE2SHORT(faceAngles[0]);
    cmd.angles[1] = ANGLE2SHORT(faceAngles[1]);
    cmd.angles[2] = 0;
  } else if (hasMoveDir) {
    idVec3 lookDir = moveDir;
    lookDir.z = 0;
    if (lookDir.Length() > 0.1f) {
      lookDir.Normalize();
      idAngles faceAngles = lookDir.ToAngles();

      idAngles currentAngles = viewAngles;
      idAngles delta = faceAngles - currentAngles;
      delta.Normalize180();

      faceAngles[1] =
          currentAngles[1] + idMath::ClampFloat(-15.0f, 15.0f, delta[1]);
      faceAngles[0] = 0;
      faceAngles[2] = 0;
      faceAngles.Normalize180();

      cmd.angles[0] = ANGLE2SHORT(faceAngles[0]);
      cmd.angles[1] = ANGLE2SHORT(faceAngles[1]);
      cmd.angles[2] = 0;
    }
  }

  // 5. COMBAT STRAFING vs NAVIGATION POSITIONING
  if (enemyVisible && nearestEnemy) {
    if (gameLocal->time >= botState.nextStrafeChangeTime) {
      botState.nextStrafeChangeTime =
          gameLocal->time + 800 + gameLocal->random.RandomInt(700);
      botState.strafeDir = (gameLocal->random.RandomFloat() > 0.5f) ? 1 : -1;
    }

    float idealDist = 400.0f;
    const char *currentWeaponName =
        spawnArgs.GetString(va("def_weapon%d", currentWeapon));
    if (idStr::Icmp(currentWeaponName, "weapon_shotgun") == 0 ||
        idStr::Icmp(currentWeaponName, "weapon_doublebarrel") == 0) {
      idealDist = 120.0f;
    } else if (idStr::Icmp(currentWeaponName, "weapon_rocketlauncher") == 0 ||
               idStr::Icmp(currentWeaponName, "weapon_machinegun") == 0) {
      idealDist = 600.0f;
    }

    int forwardSpeed = 0;
    if (nearestEnemyDist > idealDist + 80.0f) {
      forwardSpeed = 127;
    } else if (nearestEnemyDist < idealDist - 80.0f) {
      forwardSpeed = -127;
    }

    cmd.forwardmove = forwardSpeed;
    cmd.rightmove = botState.strafeDir * 127;

  } else if (hasMoveDir) {
    idVec3 desiredVelocity = moveDir;
    desiredVelocity.z = 0;
    if (desiredVelocity.Length() > 0.1f) {
      desiredVelocity.Normalize();
      idAngles viewYaw(0, viewAngles.yaw, 0);
      idVec3 forward, right;
      viewYaw.ToVectors(&forward, &right);

      float forwardSpeed = desiredVelocity * forward;
      float rightSpeed = desiredVelocity * right;

      cmd.forwardmove =
          idMath::ClampInt(-127, 127, (int)(forwardSpeed * 127.0f));
      cmd.rightmove = idMath::ClampInt(-127, 127, (int)(rightSpeed * 127.0f));
    }
  }

  // 6. ACTION CODE & TACTICAL EVASIONS
  if (enemyVisible && nearestEnemy) {
    if (gameLocal->time >= botState.nextWeaponSwitchTime) {
      botState.nextWeaponSwitchTime = gameLocal->time + 500;
      int idealWeapon = EvaluateBestWeapon(nearestEnemyDist);
      if (idealWeapon != currentWeapon) {
        cmd.impulse = idealWeapon;
        cmd.impulseSequence = oldCmd.impulseSequence + 1;
      }
    }

    if (nearestEnemyDist < 1500.0f && !skipShoot) {
      cmd.buttons |= BUTTON_ATTACK;
    }

    if (skill > 0) {
      int jumpInterval = (skill >= 3) ? 1200 : 2500;
      if ((gameLocal->time % jumpInterval) < 150) {
        needJump = true;
      }
    }

    if (skill >= 2 && (gameLocal->time % 3000) < 400 &&
        nearestEnemyDist > 300.0f) {
      cmd.buttons |= BUTTON_CROUCH;
    }
  } else if (physicsObj.GetLinearVelocity().Length() < 5.0f &&
             (gameLocal->time % 2000) < 200) {
    idVec3 moveDirVec = physicsObj.GetLinearVelocity();
    if (moveDirVec.Normalize() < 0.1f) {
      idAngles viewYaw(0, viewAngles.yaw, 0);
      idVec3 forward, right;
      viewYaw.ToVectors(&forward, &right);
      moveDirVec = forward * (cmd.forwardmove / 127.0f) + right * (cmd.rightmove / 127.0f);
      moveDirVec.Normalize();
    }
    
    trace_t trForward;
    idVec3 start = myOrigin + idVec3(0, 0, 8);
    idVec3 end = start + moveDirVec * 32.0f;
    gameLocal->GetClip()->TracePoint(trForward, start, end, MASK_PLAYERSOLID, this);
    
    if (trForward.fraction < 1.0f) {
      float maxJumpHeight = 48.0f;
      idVec3 testTopStart = trForward.endpos + idVec3(0, 0, maxJumpHeight);
      idVec3 testTopEnd = trForward.endpos;
      
      trace_t trHeight;
      gameLocal->GetClip()->TracePoint(trHeight, testTopStart, testTopEnd, MASK_PLAYERSOLID, this);
      
      float obstacleHeight = trHeight.endpos.z - myOrigin.z;
      if (obstacleHeight > 0.0f && obstacleHeight <= maxJumpHeight) {
        needJump = true;
      } else {
        needJump = false;
        botWanderYaw = idMath::AngleNormalize360(botWanderYaw + 90.0f + gameLocal->random.CRandomFloat() * 45.0f);
        botWanderTime = gameLocal->time + 1000;
      }
    } else {
      needJump = true;
    }
  }

  // 6.5 LEDGE AVOIDANCE
  if (GetPhysics()->HasGroundContacts() &&
      (cmd.forwardmove != 0 || cmd.rightmove != 0)) {
    idAngles viewYaw(0, viewAngles.yaw, 0);
    idVec3 forward, right;
    viewYaw.ToVectors(&forward, &right);
    idVec3 moveDir =
        forward * (cmd.forwardmove / 127.0f) + right * (cmd.rightmove / 127.0f);
    moveDir.z = 0;
    if (moveDir.Normalize() > 0.1f) {
      bool bypass = false;
      if (botState.cachedMoveDir != vec3_origin) {
        idVec3 pathDir = botState.cachedMoveDir;
        pathDir.z = 0;
        if (pathDir.Normalize() > 0.1f) {
          if (moveDir * pathDir > 0.707f) {
            bypass = true;
          }
        }
      }

      if (!bypass) {
        trace_t tr;
        idVec3 testPos = myOrigin + moveDir * 24.0f;
        idVec3 start = testPos + idVec3(0, 0, 18.0f);
        idVec3 end = testPos - idVec3(0, 0, 64.0f);
        gameLocal->GetClip()->TracePoint(tr, start, end, MASK_PLAYERSOLID,
                                         this);

        if (tr.fraction >= 1.0f || (start.z - tr.endpos.z) > (18.0f + 40.0f)) {
          // Ledge detected! Prevent movement and jumping in this direction
          needJump = false;
          cmd.buttons &= ~BUTTON_JUMP;

          if (enemyVisible && nearestEnemy) {
            // Flip strafe direction in combat
            botState.strafeDir = -botState.strafeDir;
            botState.nextStrafeChangeTime = gameLocal->time + 1000;
            cmd.rightmove = botState.strafeDir * 127;
            cmd.forwardmove = -127; // back up
          } else {
            // Wander fallback / navigate: back up and change direction
            botWanderYaw = idMath::AngleNormalize360(
                botWanderYaw + 180.0f +
                gameLocal->random.CRandomFloat() * 45.0f);
            botWanderTime = gameLocal->time + 1500;
            cmd.forwardmove = 0;
            cmd.rightmove = 0;
          }
        }
      }
    }
  }

  if (needJump) {
    cmd.buttons |= BUTTON_JUMP;
  }
}