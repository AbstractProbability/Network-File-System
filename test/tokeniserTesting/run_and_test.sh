#!/bin/bash

# A script to run the tokenizer test on a batch of files.

DIR=/home/mayank-kar/OSN/course-project-real/test/tokeniserTesting

echo "--- Running Tokenizer Test ---"
echo "------------------------------------------"

all_passed=true

# Loop from 1 to 6
for i in {1..6}; do
    # Correctly construct file paths using the DIR variable
    IN_FILE="$DIR/test$i"
    OUT_FILE="$DIR/test${i}_out"

    # Check if the input file exists
    if [ ! -f "$IN_FILE" ]; then
        echo "Warning: Input file '$IN_FILE' not found. Skipping."
        continue
    fi

    echo "Processing '$IN_FILE'  ->  '$OUT_FILE'..."
    
    # Run the testroundtrip program
    ./tokenisertests "$IN_FILE" "$OUT_FILE"
    
    # --- ADDED DIFF LOGIC ---
    # Use diff -q (quiet mode) to just get an exit code
    if diff -q "$IN_FILE" "$OUT_FILE" > /dev/null; then
        echo "  [SUCCESS] Files are identical."
    else
        echo "  [FAILURE] Files are DIFFERENT. See diff below:"
        # Run diff again, without -q, to show the actual differences
        diff "$IN_FILE" "$OUT_FILE"
        all_passed=false
    fi
    echo "" # Add a newline for spacing
    # --- END OF ADDED LOGIC ---
done

echo "------------------------------------------"

# --- ADDED FINAL SUMMARY ---
if $all_passed; then
    echo "All tests passed! Input and output files are identical."
else
    echo "Some tests FAILED. Input and output files differ."
fi
echo "Test run complete."