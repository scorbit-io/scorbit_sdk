# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -Wall -I../.. -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lscorbit_sdk -lrt

# Source files
SRC = main.c

# Output binary
OUT = c_example

# Default target
all: $(OUT)

# Compile the executable
$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

# Clean the build
clean:
	rm -f $(OUT)

