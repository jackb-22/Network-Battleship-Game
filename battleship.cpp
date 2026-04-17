#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <string>
#include <sstream>
#include <string_view>
#include "game_state.h"
#include <sys/stat.h>
#include <fcntl.h>


volatile sig_atomic_t exit_signaled = 0;

struct Client {
    int sock = -1;
    std::string buffer;
};

void handle_sigint(int) { exit_signaled = 1; }

static void die(const char *message) {
    perror(message);
    exit(1);
}

static int create_server_socket(unsigned short port) {
    int serv_sock;
    struct sockaddr_in serv_addr;

    if ((serv_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) die("socket() failed");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) die("bind() failed");
    if (listen(serv_sock, 4) < 0) die("listen() failed");

    return serv_sock;
}

Command parseCommand(std::string_view line) {
    Command cmd{};
    cmd.type = CommandType::UNKNOWN;

    size_t first_space = line.find(' ');
    std::string_view action = line.substr(0, first_space);

    if (action == "DONE"){
        cmd.type = CommandType::DONE;
    } else if (action == "RESET") {
        cmd.type = CommandType::RESET;
    } else if (action == "STATUS") {
        cmd.type = CommandType::STATUS;
    } else if (action == "HISTORY") {
        cmd.type = CommandType::HISTORY;
    } else if (action == "PLACE" && first_space != std::string_view::npos) {
        cmd.type = CommandType::PLACE;
        std::string args(line.substr(first_space + 1));
        std::istringstream iss(args);
        std::string shipStr;
        if (!(iss >> shipStr >> cmd.row >> cmd.col >> cmd.orientation)) {
            cmd.type = CommandType::UNKNOWN;
        } else if (!parse_ship_type(shipStr, cmd.ship_type)) {
            cmd.type = CommandType::UNKNOWN;
        }
    } else if (action == "ATTACK" && first_space != std::string_view::npos) {
        cmd.type = CommandType::ATTACK;
        std::string args(line.substr(first_space + 1));
        std::istringstream iss(args);
        if (!(iss >> cmd.row >> cmd.col)) {
            cmd.type = CommandType::UNKNOWN;
        }
    }

    return cmd;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa{};
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int serv_sock = create_server_socket(atoi(argv[1]));
    Client clients[MAX_PLAYERS];
    Game game;

    printf("Battleship Server Running on Port %s\n", argv[1]);
    fflush(stdout);

    while (!exit_signaled) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(serv_sock, &read_set);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (clients[i].sock != -1) FD_SET(clients[i].sock, &read_set);
        }

        int max_fd = serv_sock;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (clients[i].sock > 0) {
                max_fd = std::max(max_fd, clients[i].sock);
            }
        }

        if (select(max_fd + 1, &read_set, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            die("select");
        }

        if (FD_ISSET(serv_sock, &read_set)) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            int sock = accept(serv_sock, (struct sockaddr *)&addr, &len);

            if (sock >= 0) {
                int free_idx = -1;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (clients[i].sock == -1) { free_idx = i; break; }
                }

                if (free_idx != -1) {
                    clients[free_idx].sock = sock;
                    clients[free_idx].buffer.clear();
                    game.addPlayer(free_idx, sock);
                    int rules = open("rules.txt", O_RDONLY);
                    if (rules < 0){
                        perror("Warning: Could not open rules.txt");
                        const char* err_msg = "Server warning: Rules unavailable.\n";
                        send(sock, err_msg, strlen(err_msg), 0);
                    }
                    char buffer[4096];
                    ssize_t bytes_read;
                    while ((bytes_read = read(rules, buffer, sizeof(buffer))) > 0) {
                        if (send(sock, buffer, bytes_read, 0) < 0) {
                            perror("Warning: rules send failed");
                            break;
                        }
                    }
                    if (bytes_read < 0)
                        perror("Warning: rules read failed");

                    close(rules);
                    printf("Player connected on slot %d (sock %d)\n", free_idx, sock);
                    fflush(stdout);
                } else {
                    const char* msg = "ERROR server full\n";
                    send(sock, msg, strlen(msg), 0);
                    close(sock);
                }
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (clients[i].sock == -1 || !FD_ISSET(clients[i].sock, &read_set)) continue;

            char raw_buffer[1024];
            ssize_t r = recv(clients[i].sock, raw_buffer, sizeof(raw_buffer), 0);

            if (r <= 0) {
                printf("Player slot %d disconnected.\n", i);
                close(clients[i].sock);
                clients[i].sock = -1;
                clients[i].buffer.clear();
                game.removePlayer(i);
                continue;
            }

            clients[i].buffer.append(raw_buffer, r);

            if (clients[i].buffer.length() > MAX_LINE_SIZE) {
                printf("Player slot %d exceeded buffer limits. Kicking.\n", i);
                fflush(stdout);
                close(clients[i].sock);
                clients[i].sock = -1;
                clients[i].buffer.clear();
                game.removePlayer(i);
                continue;
            }

            size_t pos;
            while ((pos = clients[i].buffer.find('\n')) != std::string::npos) {
                std::string_view line(clients[i].buffer.data(), pos);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

                Command cmd = parseCommand(line);
                game.handleCommand(i, cmd);

                clients[i].buffer.erase(0, pos + 1);

                if (game.getState() == Game::State::GAME_OVER) {
                    printf("Game over. Resetting server for next players.\n");
                    fflush(stdout);
                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (clients[j].sock != -1) close(clients[j].sock);
                        clients[j].sock = -1;
                        clients[j].buffer.clear();
                    }
                    game.resetAll();
                    break;
                }
            }
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].sock != -1) close(clients[i].sock);
    }
    close(serv_sock);
    return 0;
}
