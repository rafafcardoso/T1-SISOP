# Makefile - Contagem paralela de objetos em matriz binaria (ANSI C89 + POSIX)
#
# Alvos:
#   make          compila tudo em bin/
#   make test     roda a suite de testes (bin/)
#   make bench    gera matriz grande e mede desempenho (results/medicoes.csv)
#   make tsan     compila com ThreadSanitizer e roda os testes (bin-tsan/)
#   make asan     compila com Address+UndefinedBehaviorSanitizer e roda os testes
#   make clean

CC      ?= cc
CSTD    = -std=c89 -Wall -Wextra -pedantic
OPT     ?= -O2
CFLAGS  = $(CSTD) $(OPT)
LDLIBS_PAR = -pthread

BIN     = bin
COMUM   = src/matriz.c src/pilha.c

all: $(BIN)/conta-objetos-sequencial $(BIN)/conta-objetos-paralelo $(BIN)/gera-matriz

$(BIN):
	mkdir -p $(BIN)

$(BIN)/conta-objetos-sequencial: src/conta-objetos-sequencial.c $(COMUM) src/matriz.h src/pilha.h | $(BIN)
	$(CC) $(CFLAGS) src/conta-objetos-sequencial.c $(COMUM) -o $@

$(BIN)/conta-objetos-paralelo: src/conta-objetos-paralelo.c $(COMUM) src/matriz.h src/pilha.h | $(BIN)
	$(CC) $(CFLAGS) -pthread src/conta-objetos-paralelo.c $(COMUM) -o $@

$(BIN)/gera-matriz: src/gera-matriz.c src/matriz.c src/matriz.h | $(BIN)
	$(CC) $(CFLAGS) src/gera-matriz.c src/matriz.c -o $@

test: all
	BIN=$(BIN) sh tests/run_tests.sh

bench: all
	BIN=$(BIN) sh tests/bench.sh

tsan:
	$(MAKE) BIN=bin-tsan OPT="-O1 -g -fsanitize=thread" all
	BIN=bin-tsan sh tests/run_tests.sh

asan:
	$(MAKE) BIN=bin-asan OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" all
	BIN=bin-asan sh tests/run_tests.sh

clean:
	rm -rf bin bin-tsan bin-asan

.PHONY: all test bench tsan asan clean
