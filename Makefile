# ============================================================================
# ENGLISH LEARNING APP - Makefile
# ============================================================================
# Compile và chạy server/client trên Linux/WSL
#
# Cách dùng:
#   make all      - Compile cả server và client
#   make server   - Compile server
#   make client   - Compile client
#   make clean    - Xóa các file binary
#   make run-server  - Chạy server
#   make run-client  - Chạy client
# ============================================================================

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2

# Targets
all: server client

server: server.cpp
	$(CXX) $(CXXFLAGS) -o server server.cpp
	@echo "Server compiled successfully!"

client: client.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp
	@echo "Client compiled successfully!"

clean:
	rm -f server client server.log
	@echo "Cleaned!"

run-server: server
	./server 8888

run-client: client
	./client 127.0.0.1 8888

.PHONY: all clean run-server run-client
