#!/bin/bash
echo "Starting script execution..."
sleep 1
echo "Current date: $(date)"
sleep 1
echo "Listing files:"
ls -la /tmp | head -5
sleep 1
echo "System uptime:"
uptime
echo "Script execution complete!"
