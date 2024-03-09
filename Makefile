# Makefile

# 编译器
CC=gcc

# 编译选项
CFLAGS=-I./

# 链接选项
LDFLAGS=-lpaho-mqtt3c -lwiringPi 

# 目标程序名
TARGET=iotMonitor

# 源文件
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

# 默认目标
all: $(TARGET)

# 编译目标
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# 编译对象文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(TARGET) $(OBJS)

# 安装
install:
	cp iotMonitor /usr/bin/iotmonitor
	cp ./lib/libpaho-mqtt* /usr/local/lib/
	cp ./lib/libwiringPi*   /lib/
# 帮助信息
help:
	@echo "Makefile for compiling the test program"
	@echo "Usage:"
	@echo " make all        - to compile the program"
	@echo " make clean      - to clean up the build"
	@echo " make help       - to show this help message"