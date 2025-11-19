#!/usr/bin/env python3
"""
Script to add serialization includes and create wrapper macros for send/recv
"""

import re
import sys

# Files to process
files_to_fix = [
    'user_client/client.c',
    'ss/ss.c',
    'ss/ss_comms.c',
    'ns/ns.c',
    'ns/ns_sessions.c'
]

for filepath in files_to_fix:
    full_path = f'/home/mayank-kar/OSN/course-project-real/{filepath}'
    
    try:
        with open(full_path, 'r') as f:
            content = f.read()
        
        # Add serialize.h include if not present
        if '../include/serialize.h' not in content and '"serialize.h"' not in content:
            # Find the first #include and add after it
            content = re.sub(
                r'(#include\s+[<"]common\.h[>"])',
                r'\1\n#include "../include/serialize.h"',
                content,
                count=1
            )
            print(f"Added serialize.h include to {filepath}")
        
        with open(full_path, 'w') as f:
            f.write(content)
            
    except FileNotFoundError:
        print(f"File not found: {filepath}")
        continue

print("Done adding includes")
