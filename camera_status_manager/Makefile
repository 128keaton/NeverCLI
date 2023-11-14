#
# Makefile
#
INCLUDE=-I/usr/include/dbus-1.0
OPTIONS = -lsystemd -ldbus-1 -L/usr/lib
all: client
#%.o: %.cpp
#	gcc -Wall $(INCLUDE) -o $@ $< $(OPTIONS)

server:
	g++ $(INCLUDE) -o camera_status_manager camera_status_manager.cpp $(OPTIONS)

client:server
	g++ $(INCLUDE) -o never_cli never_cli.cpp $(OPTIONS)

.PHONY: clean
clean:
	rm camera_status_manager never_cli
