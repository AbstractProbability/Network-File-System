#!/bin/bash

# Test script for Naming Server
# This script demonstrates the NS functionality by:
# 1. Starting the NS
# 2. Registering a storage server
# 3. Running client operations

echo "==================================="
echo "Naming Server Test Script"
echo "==================================="
echo ""

# Clean up any previous test artifacts
rm -f ns_log.txt test_*.log

# Start the Naming Server in background
echo "Step 1: Starting Naming Server..."
./bin/naming_server > ns_output.log 2>&1 &
NS_PID=$!
echo "Naming Server started (PID: $NS_PID)"

# Wait for NS to start and get the port
sleep 2

# Extract port from log (assuming it prints "Naming Server started on port XXXX")
NS_PORT=$(grep "Naming Server started on port" ns_output.log | grep -oP '\d+$')

if [ -z "$NS_PORT" ]; then
    echo "ERROR: Could not determine NS port"
    cat ns_output.log
    kill $NS_PID
    exit 1
fi

echo "Naming Server listening on port: $NS_PORT"
echo ""

# Register a Storage Server
echo "Step 2: Registering Storage Server..."
./bin/test_ss 127.0.0.1 $NS_PORT 127.0.0.1 6000 6001 << EOF
EOF

echo ""
sleep 1

# Show what was logged
echo "Step 3: Check NS log..."
echo "--- NS Log Contents ---"
tail -20 ns_log.txt
echo "--- End of Log ---"
echo ""

# Instructions for manual testing
echo "==================================="
echo "Manual Testing Instructions"
echo "==================================="
echo ""
echo "The Naming Server is running on port $NS_PORT"
echo ""
echo "To test with a client, open a new terminal and run:"
echo "  cd /home/akshatg/cp/course-project/cp-code/NamingServer"
echo "  ./bin/test_client 127.0.0.1 $NS_PORT alice"
echo ""
echo "Example commands to try in the client:"
echo "  LIST          - See all users"
echo "  VIEW          - See files you can access"
echo "  VIEW -a       - See all files"
echo "  INFO file1.txt - Get file info"
echo ""
echo "To stop the Naming Server:"
echo "  kill $NS_PID"
echo ""
echo "Press Ctrl+C to stop this script and the NS"
echo "==================================="

# Wait for user to stop
trap "echo ''; echo 'Shutting down...'; kill $NS_PID 2>/dev/null; exit 0" INT TERM

# Keep script running
wait $NS_PID
