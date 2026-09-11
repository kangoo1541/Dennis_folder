# PC 에서 CLI 를 그대로 돌려보기 위한 Makefile
#
#   make          : 빌드
#   make run      : 실행 (직접 키보드로 조작)
#   make test     : 자동 테스트 실행
#   make clean    : 정리

TARGET  := cli_sim
BUILD   := build

ROOT    := ..
SRC_DIR := $(ROOT)/src

INCLUDES := \
	-I$(SRC_DIR) \
	-I$(SRC_DIR)/ap \
	-I$(SRC_DIR)/bsp \
	-I$(SRC_DIR)/hw \
	-I$(SRC_DIR)/common/core \
	-I$(SRC_DIR)/common/hw/include

SOURCES := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/ap/ap.c \
	$(SRC_DIR)/hw/hw.c \
	$(SRC_DIR)/common/hw/driver/cli.c \
	$(SRC_DIR)/common/hw/driver/led.c \
	sim_port.c

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SOURCES)))
VPATH   := $(sort $(dir $(SOURCES)))

CC      := gcc
# -no-pie : 링크 주소를 고정해서 md 명령어 테스트를 쉽게 하기 위함 (PC 전용)
LDFLAGS := -no-pie
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O0 -g $(INCLUDES)

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: all
	@./$(BUILD)/$(TARGET)

test: all
	@./run_test.sh

clean:
	rm -rf $(BUILD)

.PHONY: all run test clean
