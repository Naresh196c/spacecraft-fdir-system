CC = gcc

all:
	mkdir -p bin

	gcc main.c ipc/*.c signals/*.c -o bin/obc -lpthread -lrt

	gcc power/*.c ipc/*.c signals/*.c -o bin/power -lpthread -lrt

	gcc thermal/*.c ipc/*.c signals/*.c -o bin/thermal -lpthread -lrt

	gcc comms/*.c ipc/*.c signals/*.c -o bin/comms -lpthread -lrt

	gcc logger/*.c ipc/*.c signals/*.c -o bin/logger -lpthread -lrt


run: all
	./bin/obc


clean:
	rm -rf bin fdr.log /tmp/groundstation
