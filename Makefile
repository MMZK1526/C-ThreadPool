all: test

test: test.o mmzkthreadpool.o
	clang test.o mmzkthreadpool.o -o test

test.o: test.c mmzkthreadpool.h

mmzkthreadpool.o: mmzkthreadpool.c

clean:
	rm -rf test *.o *.dSYM

.PHONY: all clean
