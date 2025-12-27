# Rescue Bot: Snake on a Minefield

A terminal-based arcade game where you guide a snake-like robot to rescue people in a dangerous minefield. Written in C using `ncurses`.

## 🎮 Game Description

Navigate through a grid filled with hidden dangers. Your mission is to rescue people ('P') while avoiding mines ('X'), walls, and obstacles ('#'). 

What makes this unique? **Your lives are your body.** The robot's length represents its remaining lives. As you take damage, you lose segments; as you progress, you can regrow them.

## ✨ Features

*   **Dual Modes:** Play manually or watch the **AI** (Breadth-First Search) play itself!
*   **Dynamic Difficulty:** Game speed increases and more mines appear as you level up.
*   **Visual Life System:** The robot's body length directly corresponds to its current lives.
*   **Special Abilities:**
    *   **Invincibility:** momentarily invincible after taking a hit.
    *   **Mine Bomb:** (Level > 10) Sacrifice levels to clear a safe zone.
*   **Leaderboard:** Saves your high scores locally.

## 🕹️ Controls

| Key | Action |
| :--- | :--- |
| **Arrow Keys / WASD** | Move the robot (Manual Mode) |
| **m** | Toggle AI / Manual Mode |
| **Space** | Detonate Bomb (Requires Level > 10, costs 5 levels) |
| **q** | Quit Game |
| **y** | Continue after losing a life |

## 📜 Rules & Mechanics

### Scoring & Levels
*   **Rescue Person (P):** +10 Score.
*   **Level Up:** Occurs every **5 people** rescued.
*   **Difficulty:**
    *   Speed increases with every level.
    *   **+2 Mines** are added to the field every level.

### Lives & Health
*   **Starting Lives:** 3.
*   **Taking Damage:** Hitting a mine, wall, or obstacle removes 1 life and 1 body segment.
*   **Regaining Lives:** Every **5 levels**, you gain +1 Life (and grow a segment).
*   **Max Lives:** 20.

### The Bomb Ability
Once you reach **Level 11**, you can use the **Spacebar** to detonate a bomb.
*   **Effect:** Destroys all mines within a 5-block radius.
*   **Cost:** You lose **5 Levels** (difficulty decreases, but score remains).

## 🛠️ How to Compile & Run

### Prerequisites
You need a C compiler (like `gcc`) and the `ncurses` library installed on your system.

**Linux / WSL / macOS:**
```bash
# Install ncurses (Ubuntu/Debian)
sudo apt-get install libncurses5-dev libncursesw5-dev

# Compile
gcc game.c -o game -lncurses

# Run
./game
```

**Windows:**
You will need an environment that supports `ncurses` (like MinGW with PDcurses, Cygwin, or WSL).
```bash
gcc game.c -o game.exe -lncurses
./game.exe
```