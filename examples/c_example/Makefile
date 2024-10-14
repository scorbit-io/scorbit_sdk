# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -I../.. -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lcrypto -lscorbit_sdk

# Source files
SRC = main.c ../signer_function/scorbit_crypt.c

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

