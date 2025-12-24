# Makefile for ZMQ examples

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lzmq -lcjson

# Java configuration
JAVAC = javac
JAVA = java
JAVA_FLAGS = -cp "java/target/classes:$$(cd java && mvn dependency:build-classpath -Dmdep.outputFile=/dev/stdout -q 2>/dev/null)"
MVN = mvn

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

all: zmq_client java-compile

zmq_client: zmq_client.c
	$(CC) $(CFLAGS) -o zmq_client zmq_client.c $(LDFLAGS)

# Java targets
java-compile: java/pom.xml
	@echo "Compiling Java examples..."
	@cd java && $(MVN) compile

java-clean:
	@echo "Cleaning Java build..."
	@cd java && $(MVN) clean

java-package: java/pom.xml
	@echo "Packaging Java examples..."
	@cd java && $(MVN) package

java-run-simple: java-compile
	@echo "Running SimpleKlawedClient..."
	@$(JAVA) $(JAVA_FLAGS) SimpleKlawedClient || echo "Make sure Klawed is running with: ./build/klawed --zmq tcp://127.0.0.1:5555"

java-run-robust: java-compile
	@echo "Running RobustKlawedClient..."
	@$(JAVA) $(JAVA_FLAGS) RobustKlawedClient || echo "Make sure Klawed is running with: ./build/klawed --zmq tcp://127.0.0.1:5555"

# Clean all
clean: java-clean
	rm -f zmq_client

# Test targets
test-python:
	@echo "Testing Python client..."
	@python3 zmq_interactive.py tcp://127.0.0.1:5555 || echo "Make sure Klawed is running with: ./build/klawed --zmq tcp://127.0.0.1:5555"

test-c: zmq_client
	@echo "Testing C client..."
	@./zmq_client tcp://127.0.0.1:5555 || echo "Make sure Klawed is running with: ./build/klawed --zmq tcp://127.0.0.1:5555"

test-java: java-run-robust
	@echo "Java client test completed"

test-all: test-c test-python test-java
	@echo "All tests completed"

# Help
help:
	@echo "Available targets:"
	@echo "  all              - Build all examples (default)"
	@echo "  zmq_client       - Build the C interactive client"
	@echo "  java-compile     - Compile Java examples (requires Maven)"
	@echo "  java-clean       - Clean Java build"
	@echo "  java-package     - Package Java examples into JAR"
	@echo "  java-run-simple  - Run SimpleKlawedClient"
	@echo "  java-run-robust  - Run RobustKlawedClient"
	@echo "  clean            - Remove all built files"
	@echo "  test-python      - Test Python client (requires pyzmq)"
	@echo "  test-c           - Test C client (requires libzmq, libcjson)"
	@echo "  test-java        - Test Java client (requires Maven)"
	@echo "  test-all         - Test all clients"
	@echo ""
	@echo "First, start Klawed in ZMQ daemon mode:"
	@echo "  cd .. && ./build/klawed --zmq tcp://127.0.0.1:5555"
	@echo ""
	@echo "Then test the client in another terminal:"
	@echo "  cd examples && make test-python"
	@echo "  or"
	@echo "  cd examples && make test-c"
	@echo "  or"
	@echo "  cd examples && make test-java"
	@echo ""
	@echo "Java prerequisites:"
	@echo "  - Java 8 or higher"
	@echo "  - Maven installed"
	@echo "  - Dependencies will be downloaded automatically via Maven"

.PHONY: all clean test-python test-c test-java test-all help \
        java-compile java-clean java-package java-run-simple java-run-robust