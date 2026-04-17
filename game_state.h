#ifndef __GAME_STATE_H__
#define __GAME_STATE_H__

#include <cstdio>
#include <string_view>
#include <array>
#include <vector>
#include <cctype>
#include <optional>

static constexpr int BOARD_SIZE = 10;
static constexpr int MAX_SHIPS = 7;

static constexpr int MAX_LINE_SIZE = 4096;
static constexpr int MAX_PLAYERS = 2;


enum class ShipType {
    CARRIER = 0,
    BATTLESHIP,
    CRUISER,
    DESTROYER,
    SUBMARINE
};

enum class AttackResult {
    INVALID,
    MISS,
    HIT,
    SUNK
};

enum class CommandType {
    PLACE,
    ATTACK,
    DONE,
    RESET,
    HISTORY,
    STATUS,
    UNKNOWN
};

class AttackOutcome {
public:
    AttackResult result;
    std::optional<ShipType> sunkType;
};

struct Command {
    CommandType type;
    ShipType ship_type;
    int row, col;
    char orientation;
};

int ship_size(ShipType type);
int fleet_limit(ShipType t);
bool parse_ship_type(std::string_view s, ShipType& type);

class Cell {
public:
    bool hasShip;
    bool hit;
    int shipId;
    Cell() : hasShip(false), hit(false), shipId(-1) {}
};

class Ship {
public:
    ShipType type;
    int hits;
    int size;

    Ship() : type(ShipType::CARRIER), hits(0), size(0) {}
    Ship(ShipType t) : type(t), hits(0) { size = ship_size(t); }
    bool isSunk() const;
};

class Board {
public:
    Cell grid[BOARD_SIZE][BOARD_SIZE];
    Ship ships[MAX_SHIPS];
    int ship_count;
    int ships_alive;

    Board();
    bool placeShip(ShipType type, int row, int col, char orientation);
    AttackOutcome attack(int row, int col);
    bool allSunk() const;
};

class Game {
public:
    enum class State { WAITING, SETUP, PLAYING, GAME_OVER };

    Game();
    void addPlayer(int playerIdx, int socket);
    void removePlayer(int playerIdx);
    void handleCommand(int playerIdx, const Command& cmd);
    void resetAll();

    State getState() const { return state; }

private:
    State state;
    int currentTurn;
    std::vector<std::string> move_log;

    void logMove(const std::string& move);

    struct GamePlayer {
        int socket = -1;
        Board ownBoard;
        bool guesses[BOARD_SIZE][BOARD_SIZE];
        bool ready = false;
        std::array<int, static_cast<size_t>(ShipType::SUBMARINE) + 1> placed_ships{};
        void reset();
    } players[2];

    void handleSetup(int playerIdx, const Command& cmd);
    void handlePlay(int playerIdx, const Command& cmd);
    void sendTo(int playerIdx, const char* msg);
    void broadcast(const char* msg);
};

#endif
