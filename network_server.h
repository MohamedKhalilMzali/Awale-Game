#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "comm.h"

int create_listener(int port);
int recv_from_client(int connect_socket_fd, char buffer_out[1024]);
void send_to_client(int connect_socket_fd, MSG_TYPE msg_type, const void *data, uint32_t data_length);