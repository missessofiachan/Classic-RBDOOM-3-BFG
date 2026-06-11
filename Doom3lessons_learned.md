# Classic-RBDOOM-3-BFG Bot Development — Lessons Learned

This document summarizes the key lessons, architectural discoveries, and solutions developed during the implementation of the modular multiplayer AI bot system for Classic-RBDOOM-3-BFG (idTech 4).

---

## 1. Modular Architecture & Clean Extensions
- **Approach**: Extracted all bot AI logic into dedicated `Bot.h` and `Bot.cpp` components, adding hook calls inside `idPlayer::Think` and `idPlayer::ClientThink`.
- **Lesson**: Keeping engine classes (like `idPlayer`) thin by delegates or separate methods makes the codebase far easier to debug and maintain, preventing compile pollution and git conflicts in large C++ engines.

---

## 2. Input Pipelines & Command Preservation
- **Issue**: In multiplayer, the enginepredicted physics loop automatically strips/resets action buttons (like `BUTTON_ATTACK`) for remote entities during prediction checks.
- **Solution**: We bypassed input command filtering inside `idPlayer::ClientThink` specifically for players flagged with `isBot`, allowing the simulated attack button clicks to propagate into the weapon execution scripts.
- **Lesson**: Server-side simulated entities (like bots) must bypass client-side input prediction filters designed to handle packet latency and input extrapolation.

---

## 3. View Angle Feedback Loops ("Spinbots")
- **Issue**: Bots were rotating rapidly in circles because the engine's view updates added raw command angles to the client prediction error delta (`deltaViewAngles`). In server-simulated bots, this prediction delta never cleared and formed a compounding feedback loop with the bot's yaw calculations.
- **Solution**: Updated `idPlayer::UpdateViewAngles()` to bypass prediction and directly assign view angles to bots (`viewAngles = cmdAngles`) while zeroing out `deltaViewAngles`, matching standard monster/AI actor physics.
- **Lesson**: AI-controlled players do not need client-side view prediction smoothing; applying network smoothing to local server AI creates positive feedback loops that break aiming systems.

---

## 4. Multiplayer Hitscan Rejection
- **Issue**: Bots could shoot hitscan weapons (machinegun, shotgun, pistol) and hit targets, but dealt no damage.
- **Solution**: The engine normally discards server-calculated hitscan damage against players, waiting instead for the client to send a reliable network message verifying the hit. Since bots do not have network sockets, we updated `idProjectile::Collide` in `Projectile.cpp` to check if the attacker `isBot`, allowing the server to apply hitscan damage directly.
- **Lesson**: Client-authoritative hitscan systems will silently discard server-simulated hitscan events unless explicitly bypassed for server-hosted AI entities.

---

## 5. Geometric Ledge Avoidance Heuristics
- **Issue**: Bots would walk or strafe off ledges into hazard pits when wandering or fighting.
- **Solution**: Implemented a dynamic ground-lookahead trace (24 units forward, 64 units down). If no floor or a steep drop (>40 units) is detected, the bot intercepts the command to halt movement or change direction. 
- **AAS Integration**: Bypassed this check if the bot is actively following an AAS (Area Awareness System) path in the same direction, ensuring it can still jump down ledges when intended by the pathfinder.
- **Lesson**: Fixed node pathfinders (like AAS) are excellent, but active movement mechanics (combat strafing/wandering) require real-time physical raycasts to handle momentum and prevent dropping off edges.

---

## 6. Capture the Flag (CTF) Role Division
- **Issue**: Bots acted as basic Deathmatch hunters even when playing CTF.
- **Solution**: Added a global CTF loop utilizing `GetTeamFlag` and `GetFlagCarrier`:
  - **Carrier Mode**: If carrying the enemy flag, the target is forced to the friendly flag's home base (`GetReturnOrigin()`).
  - **Retrieve/Rescue**: If the friendly flag is dropped or stolen, bots hunt the enemy carrier or run to recover the flag.
  - **Defenders vs. Attackers**: When flags are safe, bots partition their duties based on entity ID, half guarding the friendly stand and the other half pushing the enemy base.
- **Lesson**: High-level game state queries allow simple state machines to coordinate complex group roles (offense/defense/escort) without requiring expensive communication protocols.
