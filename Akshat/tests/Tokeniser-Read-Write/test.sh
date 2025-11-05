#!/bin/bash

# This command makes the script exit immediately if any command fails
# (e.g., if compilation breaks or 'diff' finds a mismatch).
set -e

# --- 1. Compile ---
echo "--- Compiling ---"
gcc -c -o file.o ../../File/src/file.c
gcc -o tester tester.c file.o

# --- 2. Run ---
echo "--- Running tester ---"
./tester

# --- 3. Verify ---
echo "--- Verifying outputs ---"

# Loop through every file that ends with '_out'
for outfile in test*_out; do
    # Create the 'infile' name by removing the '_out' suffix
    infile="${outfile%_out}"
    
    echo "Comparing $infile and $outfile..."
    
    # Compare the two files.
    # If they are different, 'diff' will print the difference and
    # 'set -e' will cause the script to stop here with an error.
    diff "$infile" "$outfile"
done

# If the script gets this far, all 'diff' commands succeeded.
echo "--- All tests passed! ---"