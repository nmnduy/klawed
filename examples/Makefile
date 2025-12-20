# Makefile for ZMQ examples

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lzmq -lcjson

# Detect macOS and Homebrew paths
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # Homebrew paths
    ZMQ_PREFIX := $(shell brew --prefix zeromq 2>/dev/null || echo "")
    CJSON_PREFIX := $(shell brew --prefix cjson 2>/dev/null || echo "")
    
    ifneq ($(ZMQ_PREFIX),)
        CFLAGS += -I$(ZMQ_PREFIX)/include
        LDFLAGS += -L$(ZMQ_PREFIX)/lib
    endif
    
    ifneq ($(CJSON_PREFIX),)
        CFLAGS += -I$(CJSON_PREFIX)/include
        LDFLAGS += -L$(CJSON_PREFIX)/lib
    endif
endif

all: zmq_client

zmq_client: zmq_client.c
	$(CC) $(CFLAGS) -o zmq_client zmq_client.c $(LDFLAGS)

clean:
	rm -f zmq_client

test-python:
	@echo "Testing Python client..."
	@python3 zmq_interactive.py tcp://127.0.0.1:5555 || echo "Make sure Klawed is running with: ./build/klawed --zmq tcp://127.0.0.1:5555"

test-c: zmq_client
	@echo "Testing C client..."
	@./zmq_client tcp://127.0.0.1:5555 || echo "Make sure Klawed is running with: ./build/klawed --zmq tcp://127.0.0.1:5555"

help:
	@echo "Available targets:"
	@echo "  all          - Build all examples (default)"
	@echo "  zmq_client   - Build the C interactive client"
	@echo "  clean        - Remove built files"
	@echo "  test-python  - Test Python client (requires pyzmq)"
	@echo "  test-c       - Test C client (requires libzmq, libcjson)"
	@echo ""
	@echo "First, start Klawed in ZMQ daemon mode:"
	@echo "  cd .. && ./build/klawed --zmq tcp://127.0.0.1:5555"
	@echo ""
	@echo "Then test the client in another terminal:"
	@echo "  cd examples && make test-python"
	@echo "  or"
	@echo "  cd examples && make test-c"

.PHONY: all clean test-python test-c help