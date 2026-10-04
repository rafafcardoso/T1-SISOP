/* pilha.c - Pilha dinamica com crescimento por dobra. */
#include <stdlib.h>

#include "pilha.h"

#define CAP_INICIAL 1024

void pilha_inicia(Pilha *p)
{
    p->dados = NULL;
    p->tam = 0;
    p->cap = 0;
}

void pilha_libera(Pilha *p)
{
    free(p->dados);
    pilha_inicia(p);
}

int pilha_empilha(Pilha *p, unsigned int v)
{
    if (p->tam == p->cap) {
        size_t nova = (p->cap == 0) ? CAP_INICIAL : p->cap * 2;
        unsigned int *novo;
        if (p->cap > ((size_t)-1) / 2 / sizeof(unsigned int))
            return -1;
        novo = (unsigned int *)realloc(p->dados, nova * sizeof(unsigned int));
        if (novo == NULL)
            return -1;
        p->dados = novo;
        p->cap = nova;
    }
    p->dados[p->tam++] = v;
    return 0;
}

int pilha_desempilha(Pilha *p, unsigned int *v)
{
    if (p->tam == 0)
        return 0;
    *v = p->dados[--p->tam];
    return 1;
}
