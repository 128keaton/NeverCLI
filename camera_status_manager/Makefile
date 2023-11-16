#
# Makefile
#
INCLUDE=-I/usr/include/dbus-1.0
OPTIONS = -lsystemd -ldbus-1 -L/usr/lib
all: server
#%.o: %.cpp
#	gcc -Wall $(INCLUDE) -o $@ $< $(OPTIONS)

server:
	g++ $(INCLUDE) -o nvr_camera camera_status_manager.cpp $(OPTIONS)

.PHONY: clean
clean:
	rm nvr_camera
