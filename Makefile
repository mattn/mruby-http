MRUBY_ROOT = ..

INCLUDES = -I$(MRUBY_ROOT)/include -I$(MRUBY_ROOT)/src -I.
CFLAGS = $(INCLUDES) -O3 -g -Wall -Werror-implicit-function-declaration

CC = gcc
LL = gcc
AR = ar

all : libmrb_http.a
	@echo done

picohttpparser.o : picohttpparser.c picohttpparser.h
	gcc -c -I. picohttpparser.c

mrb_http.o : mrb_http.c mrb_http.h
	gcc -c $(CFLAGS) mrb_http.c

libmrb_http.a : mrb_http.o picohttpparser.o
	$(AR) r libmrb_http.a mrb_http.o picohttpparser.o

clean :
	rm -f *.o libmrb_http.a
