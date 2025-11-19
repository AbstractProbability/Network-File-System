#!/bin/bash

# Fix NSOpCode sends
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/send(\([^,]*\), *&\(op\|opcode\|heartbeat_op\|info_op\|repl_op\), *sizeof(NSOpCode), *0)/SEND_OPCODE(\1, \2)/g' {} \;

# Fix SSOpCode sends  
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/send(\([^,]*\), *&\(hb_response\|notify\|opcode\|complete\), *sizeof(SSOpCode), *0)/SEND_OPCODE(\1, \2)/g' {} \;

# Fix SSSyncOpCode sends
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/send(\([^,]*\), *&opcode, *sizeof(opcode), *0)/SEND_OPCODE(\1, opcode)/g' {} \;

# Fix NSOpCode recvs
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/recv(\([^,]*\), *&\(op\|opcode\), *sizeof(NSOpCode), *0)/RECV_OPCODE(\1, \&\2)/g' {} \;

# Fix SSOpCode recvs
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/recv(\([^,]*\), *&\(opcode\|op\), *sizeof(SSOpCode), *0)/RECV_OPCODE(\1, \&\2)/g' {} \;

# Fix SSSyncOpCode recvs  
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/recv(\([^,]*\), *&\(op\|opcode\), *sizeof(op), *0)/RECV_OPCODE(\1, \&\2)/g' {} \;

# Fix raw int sends (partner_port, should_send_sync)
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/send(\([^,]*\), *&\(partner_port\|should_send_sync\), *sizeof(int), *0)/SEND_INT(\1, \2)/g' {} \;

# Fix raw int recvs  
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/recv(\([^,]*\), *&\(partner_port\|should_send_sync\|dummy_res\), *sizeof(int), *0)/RECV_INT(\1, \&\2)/g' {} \;

# Fix recv of info_res (ServerResponse)
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/recv(\([^,]*\), *&info_res, *sizeof(ServerResponse), *0)/RECV_SERVER_RESPONSE(\1, \&info_res)/g' {} \;

# Fix recv of ss_req (ClientRequest)
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/send(\([^,]*\), *&ss_req, *sizeof(ClientRequest), *0)/SEND_CLIENT_REQUEST(\1, \&ss_req)/g' {} \;

# Fix send of ss_res (ServerResponse)
find . -name "*.c" ! -path "./test/*" -type f -exec \
    sed -i 's/send(\([^,]*\), *&ss_res, *sizeof(ServerResponse), *0)/SEND_SERVER_RESPONSE(\1, \&ss_res)/g' {} \;

echo "Done fixing opcodes and int sends/recvs"
