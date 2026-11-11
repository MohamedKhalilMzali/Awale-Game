all: server client
server: server.c network_server.c network_server.h server.h cJSON/cJSON.h game.h 
	gcc server.c network_server.c cJSON/cJSON.c game.c -pthread -o server
client:  client.c network_client.c network_client.h cJSON/cJSON.h game.h
	gcc client.c network_client.c cJSON/cJSON.c game.c -pthread -o client
clean:
	rm -rf client server
