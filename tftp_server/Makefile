CC = gcc
CFLAGS = -Wall -g
SRC =udp_server_2.c
OUT = build/app

all: build $(OUT)

build:
	mkdir -p build

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -rf build
