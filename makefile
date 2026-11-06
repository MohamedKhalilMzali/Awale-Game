all: server client

server: server.c network_server.c network_server.h server.h cJSON/cJSON.h 
	gcc -Wall -Wextra server.c network_server.c cJSON/cJSON.c -pthread -o server
client:  client.c network_client.c network_client.h cJSON/cJSON.h
	gcc client.c network_client.c cJSON/cJSON.c -o client
clean:
	rm -rf client server
