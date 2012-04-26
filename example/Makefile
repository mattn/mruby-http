MRUBY_ROOT = ../..
TARGET := $(MRUBY_ROOT)/lib/ritevm
ifeq ($(OS),Windows_NT)
MRUBY_LIB := $(TARGET).lib
LIB = $(MRUBY_LIB)
else
MRUBY_LIB := $(TARGET).a
LIB = $(MRUBY_LIB)
endif

INCLUDES = -I$(MRUBY_ROOT)/include -I$(MRUBY_ROOT)/src -I..
CFLAGS = $(INCLUDES) -O3 -g -Wall -Werror-implicit-function-declaration

CC = gcc
LL = gcc
AR = ar

all : main
	@echo done

main : main.c
	gcc $(CFLAGS) -o main main.c ../libmrb_http.a $(LIB)

clean :
	rm -f *.o main
