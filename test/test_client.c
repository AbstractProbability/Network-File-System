#include "../include/common.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        exit(1);
    }
    char* username = argv[1];

    int sock;
    struct sockaddr_in ns_addr;
    
    // --- Create socket ---
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    ns_addr.sin_family = AF_INET;
    ns_addr.sin_port = htons(NS_LISTEN_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &ns_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }

    // --- Connect to NS ---
    if (connect(sock, (struct sockaddr *)&ns_addr, sizeof(ns_addr)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    // 1. Send Initial Packet
    InitialPacket init_pkt;
    init_pkt.type = CONN_CLIENT;
    strncpy(init_pkt.username, username, MAX_USERNAME_LEN);
    send(sock, &init_pkt, sizeof(InitialPacket), 0);

    // 2. Wait for registration response
    ServerResponse res;
    if (recv(sock, &res, sizeof(ServerResponse), 0) <= 0) {
        printf("NS disconnected.\n");
        close(sock);
        return 1;
    }

    printf("[NS Response]: %s\n", res.message);
    if (res.status != ERR_OK) {
        close(sock);
        return 1;
    }

    // 3. Send a LIST command
    printf("\nSending LIST command...\n");
    ClientRequest list_req;
    memset(&list_req, 0, sizeof(ClientRequest));
    list_req.op = OP_LIST;
    strncpy(list_req.username, username, MAX_USERNAME_LEN);
    
    send(sock, &list_req, sizeof(ClientRequest), 0);

    // 4. Receive LIST response
    if (recv(sock, &res, sizeof(ServerResponse), 0) <= 0) {
        printf("NS disconnected.\n");
        close(sock);
        return 1;
    }

    printf("[NS Response]:\n%s\n", res.message);

    // 5. Send Exit command
    list_req.op = OP_EXIT;
    send(sock, &list_req, sizeof(ClientRequest), 0);
    
    printf("\nDisconnecting.\n");
    close(sock);
    return 0;
}