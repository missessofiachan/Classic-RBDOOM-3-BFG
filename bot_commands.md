# Multiplayer Bot Configuration & Commands

We have implemented a native Quake 3 Arena-style bot system in **Classic-RBDOOM-3-BFG** multiplayer. Below are the commands, settings, and difficulty variations.

---

## 1. Bot Console Commands

Run these commands directly in the in-game console (`~`):

| Command | Usage | Description |
|---|---|---|
| `addbot <name>` | `addbot Crash` | Spawns a single bot player with the specified name into the match. |
| `fillbots` | `fillbots` | Automatically populates the match up to the server's slot limit with bots from the default list. |

---

## 2. Bot Configuration Variables (CVars)

You can set these CVars to adjust bot behaviors:

| CVar | Default | Values | Description |
|---|---|---|---|
| `bot_skill` | `1` | `0` to `3` | Adjusts the bot AI skill level globally. (See difficulty scaling details below). |
| `mp_bot_input_override` | `-1` | `-1` or client index | Overrides inputs for debugging bot routing behavior. |

---

## 3. Bot Difficulty Levels (`bot_skill`)

The AI difficulty is dynamically scaled according to the `bot_skill` CVar value:

### 🟢 `bot_skill 0` — Easy
- **Aiming Speed**: Low (max 6 degrees per frame rotation). Bots turn and aim sluggishly.
- **Reaction Time**: Slow (40% chance of hesitation/skipping shooting frames).
- **Movement**: Standard tracking and wandering. Bots jump/dodge infrequently (every 3 seconds).

### 🟡 `bot_skill 1` — Medium (Default)
- **Aiming Speed**: Moderate (max 15 degrees per frame rotation).
- **Reaction Time**: Standard (20% chance of hesitating/skipping shooting frames).
- **Movement**: Standard tracking and dodging (jumping every 3 seconds).

### 🔴 `bot_skill 2` — Hard
- **Aiming Speed**: Fast (max 45 degrees per frame rotation). Bots snap to targets quickly.
- **Reaction Time**: Instant (no delays or skipped shooting frames).
- **Movement**: Dodging (jumping every 3 seconds).

### 💀 `bot_skill 3` — Nightmare
- **Aiming Speed**: Instant (instant aim lock-on, no speed limit).
- **Reaction Time**: Instant.
- **Movement**: Highly evasive (frequent jumping every 1.5 seconds).
