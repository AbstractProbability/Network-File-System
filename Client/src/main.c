#include "../include/client.h"

// client port for server to client comms
int G_ns_to_client_socket = 0;
int G_ns_to_client_port = 0;
int G_ss_to_client_socket = 0;
int G_ss_to_client_port = 0;

// client port for client to server comms
int G_client_to_ns_socket = 0;
int G_client_to_ns_port = 0;
int G_client_to_ss_socket = 0;
int G_client_to_ss_port = 0;

// server port for server to client comms
int G_ns_port = 0;
int G_ss_port = 0;

int clientinit() {
    // make a server socket to rcv from ns
    G_ns_rcv_port = server_socket_init(&G_ns_rcv_socket);
    printf("NS reception on port: %d", G_ns_rcv_port);

    // make a server socket to rcv from ss
    G_ss_rcv_port = server_socker_init(&G_ss_rcv_socket);
    printf("SS reception on port: %d", G_ss_rcv_port);
    
    // make a client to send to ns
    printf("");
    // make a client to send to ss
}

int main() {
    if (clientinit() < 0) {
        printf("Error initing client. Exiting.\n");
        exit(0);
    }
}