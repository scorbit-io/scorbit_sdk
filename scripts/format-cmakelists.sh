#!/bin/env bash

# Format all CMakeLists.txt files in the current directory and subdirectories

# Get current script's directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo "Script directory: $DIR"

# Collect all CMakeLists.txt files except in _deps directories
cmake_files=$(find "$DIR/.." -name _deps -prune -o -name CMakeLists.txt -print)

# Loop through and format each file
for file in $cmake_files; do
    echo "Formatting $file"
    gersemi -i "$file"
done

