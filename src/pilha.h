/*
 * pilha.h - Pilha dinamica de indices (unsigned int).
 *
 * Usada no flood fill iterativo, o que evita recursao profunda em matrizes
 * grandes (um objeto pode ter milhoes de celulas).
 * Nao e thread-safe: cada thread usa a sua propria pilha.
 */
#ifndef PILHA_H
#define PILHA_H

#include <stddef.h>

typedef struct {
    unsigned int *dados;
    size_t tam;
    size_t cap;
} Pilha;

void pilha_inicia(Pilha *p);
void pilha_libera(Pilha *p);
/* 0 = ok, -1 = sem memoria */
int pilha_empilha(Pilha *p, unsigned int v);
/* 1 = desempilhou em *v, 0 = pilha vazia */
int pilha_desempilha(Pilha *p, unsigned int *v);

#endif
