#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <wordexp.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// USERNAMES ARE RESTRICTED TO ASCII UPPER, LOWER AND NUMBERS ONLY

extern char *home_dir;

// takes a pointer to the socket_fd to be modified. Returns server port
int server_socket_init(int *p_socket_fd);

// takes pointer to socket_fd to be modified. Server_ip and server_port. Returns client port
int client_socket_init(int *p_socket_fd, const char* server_ip, int server_port);

void home_dir_init();