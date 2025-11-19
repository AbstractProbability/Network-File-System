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
        fprintf(stderr, "Usage: %s <username> <path_to_read>\n", argv[0]);
        exit(1);
    }
    char* username = argv[1];
    char* path = argv[2];

    // 1. Connect to Name Server
    int ns_sock = connect_to_server("127.0.0.1", NS_LISTEN_PORT);
    if (ns_sock < 0) exit(1);
    printf("TEST: Connected to NS.\n");

    // 2. Send Initial Packet
    InitialPacket init_pkt;
    init_pkt.type = CONN_CLIENT;
    strncpy(init_pkt.username, username, MAX_USERNAME_LEN);
    send(ns_sock, &init_pkt, sizeof(InitialPacket), 0);

    // 3. Wait for registration response
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    if (res.status != ERR_OK) {
        printf("TEST: NS registration failed: %s\n", res.message);
        close(ns_sock);
        return 1;
    }
    printf("TEST: Registered with NS as '%s'.\n", username);

    // 4. Send READ request to NS
    ClientRequest read_req;
    memset(&read_req, 0, sizeof(ClientRequest));
    read_req.op = OP_READ;
    strncpy(read_req.username, username, MAX_USERNAME_LEN);
    strncpy(read_req.path, path, MAX_PATH_LEN);
    
    printf("TEST: Sending READ request for '%s' to NS...\n", path);
    send(ns_sock, &read_req, sizeof(ClientRequest), 0);

    // 5. Receive redirect from NS
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    if (res.status != ERR_OK) {
        printf("TEST: NS returned error for READ: %s\n", res.message);
        close(ns_sock);
        return 1;
    }
    if (res.ss_port == 0) {
        printf("TEST: NS did not redirect. Exiting.\n");
        close(ns_sock);
        return 1;
    }
    
    printf("TEST: NS redirecting to SS at %s:%d\n", res.ss_ip, res.ss_port);

    // 6. Connect to Storage Server
    int ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (ss_sock < 0) exit(1);
    printf("TEST: Connected to SS.\n");

    // 7. Send the *same* READ request to the SS
    send(ss_sock, &read_req, sizeof(ClientRequest), 0);

    // 8. Receive file data
    char buffer[MAX_BUFFER_LEN];
    printf("--- File Content ---\n");
    while (1) {
        int bytes = recv(ss_sock, buffer, MAX_BUFFER_LEN - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        
        // Check for our simple STOP packet
        if (strstr(buffer, "STOP") != NULL) {
            *strstr(buffer, "STOP") = '\0';
            printf("%s", buffer);
            break;
        }
        printf("%s", buffer);
    }
    printf("\n--- End of File ---\n");

    close(ss_sock);
    
    // 9. Disconnect from NS
    read_req.op = OP_EXIT;
    send(ns_sock, &read_req, sizeof(ClientRequest), 0);
    close(ns_sock);
    
    printf("TEST: All operations complete.\n");
    return 0;
}