.PHONY: all clean

all: server client
server: server.c utils/base64.c
	gcc -Wall -Wno-char-subscripts -Wno-unused-variable -o server server.c utils/base64.c
client: client.c utils/base64.c
	gcc -Wall -Wno-char-subscripts -Wno-unused-variable -o client client.c utils/base64.c
clean:
	@rm -rf server client 
