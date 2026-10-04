/*
 * gera-matriz.c - Gera matrizes binarias pseudoaleatorias reprodutiveis.
 *
 * Uso: gera-matriz LINHAS COLUNAS DENSIDADE_PERCENTUAL SEMENTE > arquivo.txt
 * Usa xorshift32 proprio, entao a mesma semente gera a mesma matriz em
 * qualquer plataforma.
 */
#include <stdio.h>
#include <stdlib.h>

#include "matriz.h"

int main(int argc, char **argv)
{
    unsigned long l, c, dens, sem, x, i, j;
    char *linha;

    if (argc != 5 ||
        parse_ulong(argv[1], 1, 1000000UL, &l) != 0 ||
        parse_ulong(argv[2], 1, 1000000UL, &c) != 0 ||
        parse_ulong(argv[3], 0, 100, &dens) != 0 ||
        parse_ulong(argv[4], 1, 4294967295UL, &sem) != 0) {
        fprintf(stderr, "uso: %s LINHAS COLUNAS DENSIDADE(0-100) SEMENTE(>=1)\n",
                argv[0]);
        return 1;
    }
    if (c > MATRIZ_MAX_CELULAS / l) {
        fprintf(stderr, "erro: matriz grande demais\n");
        return 1;
    }
    linha = (char *)malloc(c + 2);
    if (linha == NULL) {
        fprintf(stderr, "erro: memoria insuficiente\n");
        return 1;
    }

    x = sem & 0xFFFFFFFFUL;
    printf("%lu %lu\n", l, c);
    for (i = 0; i < l; i++) {
        for (j = 0; j < c; j++) {
            x ^= (x << 13) & 0xFFFFFFFFUL;
            x ^= x >> 17;
            x ^= (x << 5) & 0xFFFFFFFFUL;
            linha[j] = ((x % 100UL) < dens) ? '1' : '0';
        }
        linha[c] = '\n';
        linha[c + 1] = '\0';
        if (fputs(linha, stdout) == EOF) {
            fprintf(stderr, "erro de escrita\n");
            free(linha);
            return 1;
        }
    }
    free(linha);
    return 0;
}
