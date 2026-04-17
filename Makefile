CC = g++
CXX = g++
CXXFLAGS = -g -Wall -std=c++17

TARGETS = battleship
OBJS = battleship.o game_state.o

$(TARGETS): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGETS) $(OBJS)

PHONY += clean
clean:
	rm -rf $(TARGETS) *.o

.PHONY: $(PHONY)
