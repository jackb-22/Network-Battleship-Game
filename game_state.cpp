#include "game_state.h"
#include <sys/socket.h>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

int ship_size(ShipType type) {
    switch (type) {
        case ShipType::CARRIER: return 5;
        case ShipType::BATTLESHIP: return 4;
        case ShipType::CRUISER: return 3;
        case ShipType::DESTROYER: return 2;
        case ShipType::SUBMARINE: return 1;
    }
    return 0;
}

int fleet_limit(ShipType t) {
    switch (t) {
        case ShipType::CARRIER: return 1;
        case ShipType::BATTLESHIP: return 1;
        case ShipType::CRUISER: return 1;
        case ShipType::DESTROYER: return 2;
        case ShipType::SUBMARINE: return 2;
    }
    return 0;
}

bool parse_ship_type(std::string_view s, ShipType& type) {
    if (s == "CARRIER") type = ShipType::CARRIER;
    else if (s == "BATTLESHIP") type = ShipType::BATTLESHIP;
    else if (s == "CRUISER") type = ShipType::CRUISER;
    else if (s == "DESTROYER") type = ShipType::DESTROYER;
    else if (s == "SUBMARINE") type = ShipType::SUBMARINE;
    else return false;
    return true;
}

bool Ship::isSunk() const { return hits == size; }

Board::Board() : ship_count(0), ships_alive(0) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            grid[i][j] = Cell();
        }
    }
}

bool Board::placeShip(ShipType type, int row, int col, char orientation) {
    orientation = toupper(orientation);
    if (orientation != 'H' && orientation != 'V') return false;
    if (ship_count >= MAX_SHIPS) return false;
    if (row < 0 || col < 0) return false;
    int size = ship_size(type);

    for (int i = 0; i < size; i++) {
        int r = row + (orientation == 'V' ? i : 0);
        int c = col + (orientation == 'H' ? i : 0);
        if (r >= BOARD_SIZE || c >= BOARD_SIZE || grid[r][c].hasShip) return false;
    }

    int shipId = ship_count;
    ships[shipId] = Ship(type);

    for (int i = 0; i < size; i++) {
        int r = row + (orientation == 'V' ? i : 0);
        int c = col + (orientation == 'H' ? i : 0);
        grid[r][c].hasShip = true;
        grid[r][c].shipId = shipId;
    }
    ship_count++;
    ships_alive++;
    return true;
}

AttackOutcome Board::attack(int row, int col) {
    if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) return {AttackResult::INVALID, std::nullopt};

    Cell& cell = grid[row][col];
    if (cell.hit) return {AttackResult::INVALID, std::nullopt};

    cell.hit = true;
    if (!cell.hasShip) return {AttackResult::MISS, std::nullopt};

    Ship& ship = ships[cell.shipId];
    ship.hits++;
    if (ship.isSunk()) {
        ships_alive--;
        return {AttackResult::SUNK, ship.type};
    }
    return {AttackResult::HIT, std::nullopt};
}

bool Board::allSunk() const { return ships_alive == 0; }

Game::Game() : state(State::WAITING), currentTurn(0) { resetAll(); }

void Game::GamePlayer::reset() {
    ownBoard = Board();
    memset(guesses, 0, sizeof(guesses));
    ready = false;
    placed_ships.fill(0);
}

void Game::addPlayer(int playerIdx, int socket) {
    players[playerIdx].socket = socket;
    players[playerIdx].reset();

    if (players[0].socket != -1 && players[1].socket != -1) {
        state = State::SETUP;
    }
}

void Game::removePlayer(int playerIdx) {
    players[playerIdx].socket = -1;
    players[playerIdx].reset();

    int opp = 1 - playerIdx;
    if (players[opp].socket != -1) {
        sendTo(opp, "ERROR opponent disconnected, resetting to WAITING\n");
        players[opp].reset();
    }
    state = State::WAITING;
}

void Game::sendTo(int playerIdx, const char* msg) {
    if (players[playerIdx].socket != -1) {
        send(players[playerIdx].socket, msg, strlen(msg), 0);
    }
}

void Game::broadcast(const char* msg) {
    sendTo(0, msg);
    sendTo(1, msg);
}

void Game::handleCommand(int playerIdx, const Command& cmd) {
    if (cmd.type == CommandType::STATUS) {
        std::string status_msg = "STATUS STATE=";
        switch(state) {
            case State::WAITING: status_msg += "WAITING"; break;
            case State::SETUP: status_msg += "SETUP"; break;
            case State::PLAYING: status_msg += "PLAYING TURN=" + std::to_string(currentTurn); break;
            case State::GAME_OVER: status_msg += "GAME_OVER"; break;
        }
        status_msg += " P0_SHIPS=" + std::to_string(players[0].ownBoard.ships_alive);
        status_msg += " P1_SHIPS=" + std::to_string(players[1].ownBoard.ships_alive);
        status_msg += "\n";

        sendTo(playerIdx, status_msg.c_str());
        return;
    }
    if (state == State::SETUP) {
        if (cmd.type == CommandType::RESET) {
            players[playerIdx].reset();
            sendTo(playerIdx, "RESET_OK\n");
        } else if (!players[playerIdx].ready) {
            handleSetup(playerIdx, cmd);
        } else {
            sendTo(playerIdx, "ERROR waiting for game to begin\n");
        }
    } else if (state == State::PLAYING) {
        if (cmd.type == CommandType::HISTORY) {
            std::string hist_msg = "HISTORY_START\n";
            for (const auto& log_entry : move_log) {
                hist_msg += log_entry + "\n";
            }
            hist_msg += "HISTORY_END\n";

            sendTo(playerIdx, hist_msg.c_str());
            return;
        } else {
            handlePlay(playerIdx, cmd);
        }
    } else if (state == State::WAITING) {
        sendTo(playerIdx, "ERROR waiting for other player...\n");
    } else {
        sendTo(playerIdx, "ERROR invalid state for commands\n");
    }
}

void Game::handleSetup(int playerIdx, const Command& cmd) {
    if (cmd.type == CommandType::DONE) {
        bool valid = true;
        for (int t = 0; t <= static_cast<int>(ShipType::SUBMARINE); t++) {
            if (players[playerIdx].placed_ships[t] != fleet_limit(static_cast<ShipType>(t))) {
                valid = false; break;
            }
        }

        if (!valid) {
            sendTo(playerIdx, "ERROR incomplete fleet\n");
        } else {
            players[playerIdx].ready = true;
            sendTo(playerIdx, "READY\n");

            if (players[0].ready && players[1].ready) {
                state = State::PLAYING;
                currentTurn = 0;
                broadcast("START\n");
                sendTo(currentTurn, "YOUR_TURN\n");
            }
        }
    } else if (cmd.type == CommandType::PLACE) {
        int t_idx = static_cast<int>(cmd.ship_type);
        if (players[playerIdx].placed_ships[t_idx] >= fleet_limit(cmd.ship_type)) {
            sendTo(playerIdx, "ERROR too many ships\n");
            return;
        }

        if (players[playerIdx].ownBoard.placeShip(cmd.ship_type, cmd.row, cmd.col, cmd.orientation)) {
            players[playerIdx].placed_ships[t_idx]++;
            sendTo(playerIdx, "OK\n");
        } else {
            sendTo(playerIdx, "ERROR invalid placement\n");
        }
    } else if (cmd.type == CommandType::RESET) {
        players[playerIdx].reset();
        sendTo(playerIdx, "RESET_OK\n");
    } else {
        sendTo(playerIdx, "ERROR unknown command\n");
    }
}

void Game::logMove(const std::string& move) {
    move_log.push_back(move);
    printf("GAME LOG: %s\n", move.c_str());
    fflush(stdout);
}

void Game::resetAll() {
    state = State::WAITING;
    currentTurn = 0;
    move_log.clear();
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].reset();
    }
}

void Game::handlePlay(int playerIdx, const Command& cmd) {

    if (cmd.type != CommandType::ATTACK) {
        sendTo(playerIdx, "ERROR unknown command\n");
        return;
    }
    if (playerIdx != currentTurn) {
        sendTo(playerIdx, "ERROR not your turn\n");
        return;
    }

    if (cmd.row < 0 || cmd.row >= BOARD_SIZE || cmd.col < 0 || cmd.col >= BOARD_SIZE) {
        sendTo(playerIdx, "ERROR out of bounds\n");
        return;
    }

    if (players[playerIdx].guesses[cmd.row][cmd.col]) {
        sendTo(playerIdx, "ERROR already guessed\n");
        return;
    }

    int opp = 1 - playerIdx;
    players[playerIdx].guesses[cmd.row][cmd.col] = true;

    AttackOutcome outcome = players[opp].ownBoard.attack(cmd.row, cmd.col);
    AttackResult res = outcome.result;

    if (res == AttackResult::INVALID) {
        sendTo(playerIdx, "ERROR invalid attack\n");
        return;
    }

    auto get_ship_name = [](ShipType t) -> std::string {
        switch(t) {
            case ShipType::CARRIER: return "CARRIER";
            case ShipType::BATTLESHIP: return "BATTLESHIP";
            case ShipType::CRUISER: return "CRUISER";
            case ShipType::DESTROYER: return "DESTROYER";
            case ShipType::SUBMARINE: return "SUBMARINE";
        }
        return "UNKNOWN";
    };

    std::string result_msg, opp_msg;
    if (res == AttackResult::MISS) {
        result_msg = "MISS\n";
        opp_msg = "OPP_MISS " + std::to_string(cmd.row) + " " + std::to_string(cmd.col) + "\n";
    } else if (res == AttackResult::HIT) {
        result_msg = "HIT\n";
        opp_msg = "OPP_HIT " + std::to_string(cmd.row) + " " + std::to_string(cmd.col) + "\n";
    } else if (res == AttackResult::SUNK) {
        std::string s_name = outcome.sunkType.has_value() ? get_ship_name(outcome.sunkType.value()) : "UNKNOWN";
        result_msg = "SUNK " + s_name + "\n";
        opp_msg = "OPP_SUNK " + s_name + " " + std::to_string(cmd.row) + " " + std::to_string(cmd.col) + "\n";
    }

    std::string log_entry = "Player " + std::to_string(playerIdx) +
                            " attacked (" + std::to_string(cmd.row) + "," + std::to_string(cmd.col) + "): ";

    if (res == AttackResult::MISS) {
        log_entry += "MISS";
    } else if (res == AttackResult::HIT) {
        log_entry += "HIT";
    } else if (res == AttackResult::SUNK) {
        log_entry += "SUNK " + (outcome.sunkType ? get_ship_name(*outcome.sunkType) : std::string("UNKNOWN"));
    }

    logMove(log_entry);

    sendTo(playerIdx, result_msg.c_str());
    sendTo(opp, opp_msg.c_str());

    if (players[opp].ownBoard.allSunk()) {
        sendTo(playerIdx, "WIN\n");
        sendTo(opp, "LOSE\n");
        state = State::GAME_OVER;
    } else {
        currentTurn = opp;
        sendTo(currentTurn, "YOUR_TURN\n");
    }
}
