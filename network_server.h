#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "comm.h"

int create_listener();
void recv_from_client(int connect_socket_fd, char buffer_out[1024]);
void send_to_client(int connect_socket_fd, MSG_TYPE msg_type, const char *data);