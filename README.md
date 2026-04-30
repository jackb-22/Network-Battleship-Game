# Terminal Battleship Multiplayer Server

A custom, TCP-socket-based multiplayer Battleship game server written in C++17. This server allows two players to connect remotely via a raw TCP connection (such as `netcat` or `telnet`), place their fleets, and compete in a classic game of Battleship directly from their terminals.

## Features
* **Custom TCP Server:** Uses native POSIX sockets and `select()` for non-blocking client multiplexing and connection handling.
* **State Machine Logic:** Enforces strict game states (`WAITING`, `SETUP`, `PLAYING`, `GAME_OVER`) to prevent out-of-turn actions and invalid commands.
* **Real-Time Multiplayer:** Instantaneous feedback on hits, misses, and sunk ships broadcasted to both the attacker and the opponent.
* **Move History:** Players can request the entire game history mid-match to strategize their next strikes.

---

## Installation & Setup

### Prerequisites
* A POSIX-compliant operating system (Linux, macOS)
* `g++` compiler with C++17 support
* `make`

### 1. Compile the Server
Clone the repository and run `make` in the root directory to compile the `battleship` executable:
```bash
make
```

### 2. Create the Rules File
The server attempts to read and send a `rules.txt` file to players as soon as they connect. Save the rules text (provided at the bottom of this README) into a file named `rules.txt` in the same directory as your executable.

### 3. Run the Server
Start the server by providing a port number as an argument:
```bash
./battleship 8080
```

### 4. Connect as a Client
Players can connect to the server using `netcat` (or `telnet`) in a separate terminal window:
```bash
nc localhost 8080
```
*(Replace `localhost` with the server's IP address if playing across different machines).*

---

## How to Play (Game Rules)

Welcome to Battleship!

The rules are as follows:
When you connect to the server, you will begin in the **SETUP** phase. Your job is to place all your ships in your desired positions. You have the following ships:

* **1 CARRIER** of size 5
* **1 BATTLESHIP** of size 4
* **1 CRUISER** of size 3
* **2 DESTROYERS** of size 2
* **2 SUBMARINES** of size 1

### Placing Your Ships
To place your ships, utilize the following format:
```text
PLACE SHIPNAME ROW_NUMBER (0 - 9) COLUMN_NUMBER (0 - 9) ORIENTATION (V or H)
```

**Example:**
```text
PLACE DESTROYER 0 0 H
```
The orientation assumes that the row and column number provided indicate the leftmost point for `H` (horizontal) and the top-most point for `V` (vertical). The board goes from 0 on the left to 9 on the right, and 0 at the top to 9 on the bottom.

### Setup Commands
You also have access to the following commands during setup:
* `RESET` - Allows you to reset your board during the SETUP phase. Be cautious! This will remove all of your currently placed ships.
* `DONE` - How you confirm your board and exit the SETUP phase. 
* `STATUS` - Allows you to see the phase of the game you are in, the number of ships you have on the board, and the number of ships your opponent has on the board. (This command remains available in later stages).

The game will begin when *both* players submit `DONE`. Please note that if you submit `DONE` before the other player, you will enter a **WAITING** state. No commands will be valid inside the WAITING state.

---

### The PLAYING Phase
Once both players have exited the SETUP phase, you will enter the **PLAYING** state! Inside the PLAYING state, you will have access to the following commands:

* `STATUS` - Presents the same information as during the SETUP state, updating the count of remaining ships as the game progresses. It will now also contain the player whose turn it currently is.
* `HISTORY` - Presents you with the entire move set so far! It will include the moves each player has made, as well as if that move resulted in a MISS, HIT, or SUNK a ship!
* `ATTACK` - How you strike your opponent's board.

**How to Attack:**
Follow this format to launch an attack:
```text
ATTACK ROW_NUMBER (0 - 9) COLUMN_NUMBER (0 - 9)
```

**Example:**
```text
ATTACK 0 0
```

The attack command will notify you whether you hit, miss, or sunk a ship! You will only know which specific ship was hit in the event that you sink that ship.

***Note:** If either player exits the game or disconnects during any state, the entire game will immediately reset for both players.*

Good luck!
