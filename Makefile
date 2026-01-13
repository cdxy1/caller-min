.PHONY: build, build-server, build-client, fmt

DEFAULT_GOAL := build

BUILD_DIR = "./bin/"

build: fmt build-server build-client

build-server:
	[ -d "$(BUILD_DIR)" ] || mkdir -p $(BUILD_DIR)
	go build -o $(BUILD_DIR) ./cmd/server/main.go

build-client:
	[ -d "$(BUILD_DIR)" ] || mkdir -p $(BUILD_DIR)
	cc cmd/client_c/main.c -o $(BUILD_DIR)/client_c -lportaudio -lpthread

fmt:
	go fmt ./...
