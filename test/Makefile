# MiniFee/MiniNvm 宿主机测试 Makefile（gcc）
# 用法: make        # 编译并运行测试
#       make build  # 仅编译
#       make run    # 仅运行
#       make clean

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wno-unused -O2 -g \
            -Iinclude -Iconfig -Itest
LDFLAGS ?=

INCDIR  = -Iinclude -Iconfig -Itest
SRC = src/MiniFee.c src/MiniNvm.c test/FlashDrv_Stub.c test/MiniFee_Test.c test/MiniNvm_Test.c test/Test_Main.c
OBJ = $(SRC:.c=.o)
BIN = minifee_mininvm_test

.PHONY: all build run clean

all: build run

build: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(INCDIR) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(OBJ)
