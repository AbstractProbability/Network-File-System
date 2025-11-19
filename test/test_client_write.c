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
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <username> <path> <sentence_index>\n", argv[0]);
        exit(1);
    }
    char* username = argv[1];
    char* path = argv[2];
    int sentence_index = atoi(argv[3]);

    // 1. Connect to Name Server
    int ns_sock = connect_to_server("127.0.0.1", NS_LISTEN_PORT);
    if (ns_sock < 0) exit(1);
    printf("TEST_WRITE: Connected to NS.\n");

    // 2. Send Initial Packet
    InitialPacket init_pkt;
    init_pkt.type = CONN_CLIENT;
    strncpy(init_pkt.username, username, MAX_USERNAME_LEN);
    send(ns_sock, &init_pkt, sizeof(InitialPacket), 0);

    // 3. Wait for registration response
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    if (res.status != ERR_OK) {
        printf("TEST_WRITE: NS registration failed: %s\n", res.message);
        close(ns_sock);
        return 1;
    }
    printf("TEST_WRITE: Registered with NS as '%s'.\n", username);

    // 4. Send WRITE request to NS
    ClientRequest write_req;
    memset(&write_req, 0, sizeof(ClientRequest));
    write_req.op = OP_WRITE;
    strncpy(write_req.username, username, MAX_USERNAME_LEN);
    strncpy(write_req.path, path, MAX_PATH_LEN);
    write_req.index = sentence_index;
    
    printf("TEST_WRITE: Sending WRITE request for '%s' (sentence %d) to NS...\n", path, sentence_index);
    send(ns_sock, &write_req, sizeof(ClientRequest), 0);

    // 5. Receive redirect from NS
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    if (res.status != ERR_OK) {
        printf("TEST_WRITE: NS returned error for WRITE: %s\n", res.message);
        close(ns_sock);
        return 1;
    }
    
    printf("TEST_WRITE: NS redirecting to SS at %s:%d\n", res.ss_ip, res.ss_port);

    // 6. Connect to Storage Server
    int ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (ss_sock < 0) exit(1);
    printf("TEST_WRITE: Connected to SS.\n");

    // 7. Send the *same* WRITE request to the SS
    send(ss_sock, &write_req, sizeof(ClientRequest), 0);

    // 8. Wait for "lock acquired" ACK
    if (recv(ss_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("TEST_WRITE: SS failed to lock sentence: %s\n", res.message);
        close(ss_sock);
        close(ns_sock);
        return 1;
    }
    printf("TEST_WRITE: [SS Response]: %s\n", res.message);

    // 9. Send words to write
    ClientWritePacket pkt;
    
    pkt.op = WRITE_OP_INSERT_WORD;
    pkt.word_index = 0; // Simplified: just appending
    strcpy(pkt.content, "NEWLY");
    send(ss_sock, &pkt, sizeof(ClientWritePacket), 0);
    
    pkt.op = WRITE_OP_INSERT_WORD;
    pkt.word_index = 1;
    strcpy(pkt.content, "WRITTEN");
    send(ss_sock, &pkt, sizeof(ClientWritePacket), 0);
    
    printf("TEST_WRITE: Sent 2 words.\n");

    // 10. Send ETIRW
    pkt.op = WRITE_OP_ETIRW;
    send(ss_sock, &pkt, sizeof(ClientWritePacket), 0);
    printf("TEST_WRITE: Sent ETIRW.\n");

    // 11. Wait for final commit ACK
    if (recv(ss_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("TEST_WRITE: SS failed to commit write: %s\n", res.message);
        close(ss_sock);
        close(ns_sock);
        return 1;
    }
    printf("TEST_WRITE: [SS Response]: %s\n", res.message);

    close(ss_sock);
    
    // 12. Disconnect from NS
    write_req.op = OP_EXIT;
    send(ns_sock, &write_req, sizeof(ClientRequest), 0);
    close(ns_sock);
    
    printf("TEST_WRITE: All operations complete.\n");
    return 0;
}