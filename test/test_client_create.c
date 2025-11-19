#include "../include/common.h"

int connect_to_server(const char* ip, int port) {
    int sock;
    struct sockaddr_in addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }
    return sock;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <username> <path_to_create>\n", argv[0]);
        exit(1);
    }
    char* username = argv[1];
    char* path = argv[2];

    // 1. Connect to Name Server
    int ns_sock = connect_to_server("127.0.0.1", NS_LISTEN_PORT);
    if (ns_sock < 0) exit(1);
    printf("TEST_CREATE: Connected to NS.\n");

    // 2. Send Initial Packet
    InitialPacket init_pkt;
    init_pkt.type = CONN_CLIENT;
    strncpy(init_pkt.username, username, MAX_USERNAME_LEN);
    send(ns_sock, &init_pkt, sizeof(InitialPacket), 0);

    // 3. Wait for registration response
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    if (res.status != ERR_OK) {
        printf("TEST_CREATE: NS registration failed: %s\n", res.message);
        close(ns_sock);
        return 1;
    }
    printf("TEST_CREATE: Registered with NS as '%s'.\n", username);

    // 4. Send CREATE request to NS
    ClientRequest create_req;
    memset(&create_req, 0, sizeof(ClientRequest));
    create_req.op = OP_CREATE;
    strncpy(create_req.username, username, MAX_USERNAME_LEN);
    strncpy(create_req.path, path, MAX_PATH_LEN);
    
    printf("TEST_CREATE: Sending CREATE request for '%s'...\n", path);
    send(ns_sock, &create_req, sizeof(ClientRequest), 0);

    // 5. Receive response from NS
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    
    printf("TEST_CREATE: [NS Response]: %s (status: %d)\n", res.message, res.status);
    
    // 6. Disconnect from NS
    create_req.op = OP_EXIT;
    send(ns_sock, &create_req, sizeof(ClientRequest), 0);
    close(ns_sock);
    
    printf("TEST_CREATE: All operations complete.\n");
    return 0;
}