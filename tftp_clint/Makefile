
CC = gcc
CFLAGS = -Wall -g
SRC =udp_client_2.c
OUT = build/app

all: build $(OUT)

build:
	mkdir -p build

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -rf build
