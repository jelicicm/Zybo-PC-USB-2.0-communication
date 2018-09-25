# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall -D_POSIX_C_SOURCE=199309L -lusb-1.0 -std=c99

# the build target executable:
TARGET = lib_usb_app

.PHONY: default linux_deps
 
default: all

linux_deps:
	sudo apt-get install libusb-1.0-0-dev

all: $(TARGET)

  $(TARGET): $(TARGET).c
	$(CC) -o $(TARGET) $(TARGET).c $(CFLAGS)

clean:
	$(RM) $(TARGET)