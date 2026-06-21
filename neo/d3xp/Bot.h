#ifndef __BOT_H__
#define __BOT_H__

class idPlayer;
class idItem;
class idEntity;

/*
===============================================================================

        Bot Personality
        
===============================================================================
*/
struct BotPersonality {
	float accuracy;       // 0.0 (terrible) to 1.0 (perfect)
	float aggression;     // 0.0 (coward) to 1.0 (pusher)
	float jumpFrequency;  // 0.0 (grounded) to 1.0 (kangaroo)
	int preferredWeapon;  // Weapon index, or -1 for none
};

/*
===============================================================================

        Bot State

===============================================================================
*/
class idBotState {
public:
	idBotState(idPlayer* owner);
	~idBotState();

	void Think(usercmd_t& cmd);
	void Save(idSaveGame *savefile) const;
	void Restore(idRestoreGame *savefile);

	BotPersonality personality;
	idEntityPtr<idItem> currentGoalItem;

private:
	idPlayer* client;

	struct botFrameState_t {
		idPlayer* nearestEnemy;
		float nearestEnemyDist;
		bool enemyVisible;
		idVec3 moveDir;
		bool hasMoveDir;
		bool needJump;
	};

	void BotSensoryAcquisition(botFrameState_t& state);
	void EvaluateMapGoals(botFrameState_t& state);
	void BotTargetPositionSelection(botFrameState_t& state);
	void BotNavigationPathfinding(botFrameState_t& state);
	void BotAimingAndRotation(usercmd_t& cmd, botFrameState_t& state);
	void BotCombatStrafing(usercmd_t& cmd, botFrameState_t& state);
	void BotActionAndEvasion(usercmd_t& cmd, botFrameState_t& state);
	void BotLedgeAvoidance(usercmd_t& cmd, botFrameState_t& state);

	int EvaluateBestWeapon(float enemyDist);

	// Bot AI state variables
	idEntityPtr<idEntity> botTargetItem;
	int botNextTargetSearchTime;
	idVec3 botTargetPos;
	bool botHasTargetPos;
	float botWanderYaw;
	int botWanderTime;

	int botNextPathTime;
	idVec3 botCachedMoveDir;
	bool botCachedNeedJump;
	int botNextWeaponSwitchTime;
	int botStrafeDir;
	int botNextStrafeChangeTime;

	int botNextFireTime;
	bool enemyWasVisible;
};

#endif // __BOT_H__
