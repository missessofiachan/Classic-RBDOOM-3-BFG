/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "Game_local.h"
#include "../aas/AASFile.h"
#include "Bot.h"

idCVar bot_skill( "bot_skill", "1", CVAR_INTEGER | CVAR_GAME, "Bot skill level (0 = Easy, 1 = Medium, 2 = Hard, 3 = Nightmare)" );

/*
==================
idPlayer::BotAI
==================
*/
void idPlayer::BotAI( usercmd_t& cmd )
{
	// Clear the command
	cmd.buttons = 0;
	cmd.forwardmove = 0;
	cmd.rightmove = 0;
	cmd.impulse = 0;

	// If spectating or dead, we want to respawn/join!
	if ( spectating || health <= 0 ) {
		cmd.buttons |= BUTTON_ATTACK;
		return;
	}

	// 1. Find nearest enemy player
	idPlayer* nearestEnemy = NULL;
	float nearestEnemyDist = 999999.0f;
	bool enemyVisible = false;
	idVec3 myEye = GetEyePosition();
	idVec3 myOrigin = GetPhysics()->GetOrigin();

	for ( int i = 0; i < MAX_PLAYERS; i++ ) {
		if ( i == entityNumber ) {
			continue;
		}
		idEntity* ent = gameLocal->entities[i];
		if ( !ent || !ent->IsType( idPlayer::Type ) ) {
			continue;
		}
		idPlayer* other = static_cast<idPlayer*>( ent );
		if ( other->health <= 0 || other->spectating ) {
			continue;
		}
		// If team play, don't shoot teammates
		if ( gameLocal->mpGame.IsGametypeTeamBased() && other->team == team ) {
			continue;
		}

		float dist = ( other->GetPhysics()->GetOrigin() - myOrigin ).Length();
		if ( dist < nearestEnemyDist ) {
			nearestEnemyDist = dist;
			nearestEnemy = other;
		}
	}

	// Check Line of Sight to nearest enemy
	if ( nearestEnemy != NULL ) {
		trace_t tr;
		gameLocal->GetClip()->Translation( tr, myEye, nearestEnemy->GetEyePosition(), NULL, mat3_identity, MASK_SHOT_RENDERMODEL, this );
		if ( tr.fraction >= 1.0f || tr.c.entityNum == nearestEnemy->entityNumber ) {
			enemyVisible = true;
		}
	}

	// 2. Retrieve AAS & Determine target position
	idAAS* aas = gameLocal->GetAAS( "aas32" );
	if ( !aas ) {
		aas = gameLocal->GetAAS( "aas48" );
	}
	if ( !aas ) {
		for ( int i = 0; i < 8; ++i ) {
			idAAS* a = gameLocal->GetAAS( i );
			if ( a && a->GetSettings() ) {
				aas = a;
				break;
			}
		}
	}

	if ( enemyVisible && nearestEnemy ) {
		botTargetPos = nearestEnemy->GetPhysics()->GetOrigin();
		botHasTargetPos = true;
	} else {
		// Search for nearest active item if we don't have a visible enemy
		if ( gameLocal->time >= botNextTargetSearchTime || !botTargetItem.GetEntity() || botTargetItem->IsHidden() ) {
			botNextTargetSearchTime = gameLocal->time + 1000; // Search once every second
			idEntity* bestItem = NULL;
			float bestItemDist = 999999.0f;

			for ( int j = 0; j < gameLocal->num_entities; j++ ) {
				idEntity* ent = gameLocal->entities[j];
				if ( !ent ) {
					continue;
				}
				if ( ent->IsHidden() ) {
					continue;
				}
				if ( !ent->IsType( idItem::Type ) ) {
					continue;
				}
				float d = ( ent->GetPhysics()->GetOrigin() - myOrigin ).Length();
				if ( d < bestItemDist ) {
					bestItemDist = d;
					bestItem = ent;
				}
			}
			botTargetItem = bestItem;
		}

		if ( aas ) {
			if ( botTargetItem.GetEntity() ) {
				botTargetPos = botTargetItem->GetPhysics()->GetOrigin();
				botHasTargetPos = true;
			} else if ( nearestEnemy ) {
				botTargetPos = nearestEnemy->GetPhysics()->GetOrigin();
				botHasTargetPos = true;
			} else {
				botHasTargetPos = false;
			}
		} else {
			// No AAS: Only target the item if it's directly visible
			bool itemVisible = false;
			if ( botTargetItem.GetEntity() ) {
				trace_t trItem;
				gameLocal->GetClip()->Translation( trItem, myEye, botTargetItem->GetPhysics()->GetOrigin() + idVec3(0,0,16), NULL, mat3_identity, MASK_SHOT_RENDERMODEL, this );
				if ( trItem.fraction >= 1.0f ) {
					itemVisible = true;
				}
			}

			if ( itemVisible && botTargetItem.GetEntity() ) {
				botTargetPos = botTargetItem->GetPhysics()->GetOrigin();
				botHasTargetPos = true;
			} else {
				botHasTargetPos = false;
			}
		}
	}

	// 3. Navigation using AAS pathfinding
	idVec3 moveDir( 0, 0, 0 );
	bool hasMoveDir = false;
	bool needJump = false;

	if ( aas && botHasTargetPos ) {
		idBounds myBounds = GetPhysics()->GetBounds();
		int myAreaNum = aas->PointReachableAreaNum( myOrigin, myBounds, AREA_REACHABLE_WALK );
		int targetAreaNum = aas->PointReachableAreaNum( botTargetPos, myBounds, AREA_REACHABLE_WALK );

		if ( myAreaNum > 0 && targetAreaNum > 0 ) {
			aasPath_t path;
			idVec3 org = myOrigin;
			aas->PushPointIntoAreaNum( myAreaNum, org );
			idVec3 goal = botTargetPos;
			aas->PushPointIntoAreaNum( targetAreaNum, goal );

			if ( aas->WalkPathToGoal( path, myAreaNum, org, targetAreaNum, goal, TFL_WALK | TFL_AIR ) ) {
				moveDir = path.moveGoal - myOrigin;
				hasMoveDir = true;

				if ( path.type == PATHTYPE_BARRIERJUMP || path.type == PATHTYPE_JUMP || moveDir.z > 24.0f ) {
					needJump = true;
				}
			}
		}
	}

	// Fallback to simple direct movement or wander if AAS failed or is not available
	if ( !hasMoveDir ) {
		if ( botHasTargetPos ) {
			moveDir = botTargetPos - myOrigin;
			hasMoveDir = true;
		} else {
			// Wander/Explore using sensory wall-avoidance steering
			idVec3 forwardDir = idAngles( 0, botWanderYaw, 0 ).ToForward();
			trace_t tr;
			gameLocal->GetClip()->Translation( tr, myOrigin + idVec3(0,0,16), myOrigin + idVec3(0,0,16) + forwardDir * 80.0f, GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, this );

			bool stuck = ( physicsObj.GetLinearVelocity().Length() < 5.0f && gameLocal->time > botWanderTime - 1500 );

			if ( tr.fraction < 1.0f || stuck ) {
				// Blocked! Check left and right to steer
				float yawLeft = botWanderYaw + 90.0f;
				float yawRight = botWanderYaw - 90.0f;

				idVec3 dirLeft = idAngles( 0, yawLeft, 0 ).ToForward();
				idVec3 dirRight = idAngles( 0, yawRight, 0 ).ToForward();

				trace_t trLeft, trRight;
				gameLocal->GetClip()->Translation( trLeft, myOrigin + idVec3(0,0,16), myOrigin + idVec3(0,0,16) + dirLeft * 80.0f, GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, this );
				gameLocal->GetClip()->Translation( trRight, myOrigin + idVec3(0,0,16), myOrigin + idVec3(0,0,16) + dirRight * 80.0f, GetPhysics()->GetClipModel(), mat3_identity, MASK_PLAYERSOLID, this );

				if ( trLeft.fraction < 0.3f && trRight.fraction < 0.3f ) {
					// Both blocked, turn around
					botWanderYaw += 180.0f + gameLocal->random.CRandomFloat() * 30.0f;
				} else if ( trLeft.fraction >= trRight.fraction ) {
					botWanderYaw = yawLeft + gameLocal->random.CRandomFloat() * 15.0f;
				} else {
					botWanderYaw = yawRight + gameLocal->random.CRandomFloat() * 15.0f;
				}
				botWanderYaw = idMath::AngleNormalize360( botWanderYaw );
				botWanderTime = gameLocal->time + 1000 + gameLocal->random.RandomInt( 1001 );

				if ( stuck ) {
					needJump = true;
				}
			} else if ( gameLocal->time >= botWanderTime ) {
				// Time to change direction randomly to explore
				botWanderTime = gameLocal->time + 2000 + gameLocal->random.RandomInt( 2001 );
				botWanderYaw = gameLocal->random.RandomFloat() * 360.0f;
			}

			moveDir = idAngles( 0, botWanderYaw, 0 ).ToForward();
			hasMoveDir = true;
		}
	}

	// 4. Aiming / Rotation control
	int skill = bot_skill.GetInteger();
	float maxRotationPerFrame = 360.0f;
	bool skipShoot = false;

	if ( skill == 0 ) {
		maxRotationPerFrame = 6.0f;
		skipShoot = ( ( gameLocal->time % 10 ) < 4 );
	} else if ( skill == 1 ) {
		maxRotationPerFrame = 15.0f;
		skipShoot = ( ( gameLocal->time % 10 ) < 2 );
	} else if ( skill == 2 ) {
		maxRotationPerFrame = 45.0f;
	}

	if ( enemyVisible && nearestEnemy ) {
		// Face enemy eye position
		idVec3 dirToEnemy = nearestEnemy->GetEyePosition() - myEye;
		dirToEnemy.Normalize();
		idAngles faceAngles = dirToEnemy.ToAngles();

		idAngles currentAngles = viewAngles;
		idAngles delta = faceAngles - currentAngles;
		delta.Normalize180();

		faceAngles[0] = currentAngles[0] + idMath::ClampFloat( -maxRotationPerFrame, maxRotationPerFrame, delta[0] );
		faceAngles[1] = currentAngles[1] + idMath::ClampFloat( -maxRotationPerFrame, maxRotationPerFrame, delta[1] );
		faceAngles[2] = 0;
		faceAngles.Normalize180();

		cmd.angles[0] = ANGLE2SHORT( faceAngles[0] );
		cmd.angles[1] = ANGLE2SHORT( faceAngles[1] );
		cmd.angles[2] = 0;
	} else if ( hasMoveDir ) {
		// Face movement direction smoothly if no target
		idVec3 lookDir = moveDir;
		lookDir.z = 0;
		if ( lookDir.Length() > 0.1f ) {
			lookDir.Normalize();
			idAngles faceAngles = lookDir.ToAngles();

			idAngles currentAngles = viewAngles;
			idAngles delta = faceAngles - currentAngles;
			delta.Normalize180();

			faceAngles[1] = currentAngles[1] + idMath::ClampFloat( -15.0f, 15.0f, delta[1] );
			faceAngles[0] = 0; // Look straight
			faceAngles[2] = 0;
			faceAngles.Normalize180();

			cmd.angles[0] = ANGLE2SHORT( faceAngles[0] );
			cmd.angles[1] = ANGLE2SHORT( faceAngles[1] );
			cmd.angles[2] = 0;
		}
	}

	// 5. Movement relative to view angles
	if ( hasMoveDir ) {
		idVec3 desiredVelocity = moveDir;
		desiredVelocity.z = 0;
		if ( desiredVelocity.Length() > 0.1f ) {
			desiredVelocity.Normalize();
			idAngles viewYaw( 0, viewAngles.yaw, 0 );
			idVec3 forward, right;
			viewYaw.ToVectors( &forward, &right );

			float forwardSpeed = desiredVelocity * forward;
			float rightSpeed = desiredVelocity * right;

			cmd.forwardmove = idMath::ClampInt( -127, 127, (int)( forwardSpeed * 127.0f ) );
			cmd.rightmove = idMath::ClampInt( -127, 127, (int)( rightSpeed * 127.0f ) );
		}
	}

	// 6. Action / Shooting Logic
	if ( enemyVisible && nearestEnemyDist < 1200.0f && !skipShoot ) {
		cmd.buttons |= BUTTON_ATTACK;
	}

	// Random jump/dodge in combat, or jump if stuck
	if ( enemyVisible ) {
		int jumpInterval = ( skill >= 3 ) ? 1500 : 3000;
		if ( ( gameLocal->time % jumpInterval ) < 200 ) {
			needJump = true;
		}
	} else if ( physicsObj.GetLinearVelocity().Length() < 5.0f && ( gameLocal->time % 2000 ) < 200 ) {
		needJump = true;
	}

	if ( needJump ) {
		cmd.buttons |= BUTTON_JUMP;
	}
}
