#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

int connect_to_server(struct sockaddr_in server_addr);
char* recv_from_server(int connect_client_fd, char buffer_out[1024]);
char* convert_to_uppercase(char input[1024]);
