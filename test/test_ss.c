#include "../include/common.h"

int main() {
    int sock;
    struct sockaddr_in ns_addr;
    
    // --- Create socket ---
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    ns_addr.sin_family = AF_INET;
    ns_addr.sin_port = htons(NS_LISTEN_PORT);
    inet_pton(AF_INET, "127.0.0.1", &ns_addr.sin_addr);

    // --- Connect to NS ---
    if (connect(sock, (struct sockaddr *)&ns_addr, sizeof(ns_addr)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    // 1. Send Initial Packet
    InitialPacket init_pkt;
    init_pkt.type = CONN_SS;
    send(sock, &init_pkt, sizeof(InitialPacket), 0);

    // 2. Send SS Info Packet
    SS_Info_Packet info_pkt;
    strcpy(info_pkt.ip, "127.0.0.1");
    info_pkt.port_for_clients = 9001; // This SS listens on 9001
    info_pkt.is_empty = 1; 
    send(sock, &info_pkt, sizeof(SS_Info_Packet), 0);
    
    printf("SS Stub: Sent registration to NS. (empty: %d)\n", info_pkt.is_empty);

    // 3. TODO: Send file list
    
    // 4. Listen for heartbeats
    printf("SS Stub: Listening for heartbeats...\n");
    NSOpCode op;
    while (recv(sock, &op, sizeof(NSOpCode), 0) > 0) {
        if (op == NS_SS_HEARTBEAT) {
            printf("SS Stub: Received heartbeat. Sending ACK.\n");
            // TODO: Send an ACK response
        }
    }

    printf("SS Stub: NS disconnected.\n");
    close(sock);
    return 0;
}