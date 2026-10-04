/*
 * conta-objetos-sequencial.c
 *
 * Versao SEQUENCIAL (referencia de correcao e de desempenho).
 * Conta objetos (componentes conexos de 1s) com conectividade 8 usando
 * flood fill iterativo com pilha explicita e um vetor de celulas visitadas.
 *
 * Uso: conta-objetos-sequencial ARQUIVO [-r REPETICOES]
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "matriz.h"
#include "pilha.h"

/* Os 8 vizinhos (conectividade 8): horizontais, verticais e diagonais. */
static const int DL[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
static const int DC[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };

/*
 * Conta os objetos da matriz. Retorna 0 em sucesso, -1 em erro.
 * O tempo medido inclui a alocacao do vetor de visitados e todo o calculo,
 * mas nao a leitura do arquivo.
 */
static int conta_sequencial(const Matriz *m, unsigned long *objetos,
                            double *tempo_ms)
{
    const size_t linhas = m->linhas;
    const size_t colunas = m->colunas;
    const size_t total = linhas * colunas;
    unsigned char *visitado;
    Pilha pilha;
    unsigned long cont = 0;
    size_t i;
    double t0 = tempo_agora_ms();

    visitado = (unsigned char *)calloc(total, 1); /* 0 = nao visitada */
    if (visitado == NULL) {
        fprintf(stderr, "erro: memoria insuficiente para o vetor de visitados\n");
        return -1;
    }
    pilha_inicia(&pilha);

    for (i = 0; i < total; i++) {
        unsigned int atual;

        if (m->celulas[i] == 0 || visitado[i])
            continue;

        /* Celula de primeiro plano ainda nao visitada: novo objeto. */
        cont++;
        visitado[i] = 1;
        if (pilha_empilha(&pilha, (unsigned int)i) != 0)
            goto sem_memoria;

        while (pilha_desempilha(&pilha, &atual)) {
            size_t l = atual / colunas;
            size_t c = atual % colunas;
            int k;

            for (k = 0; k < 8; k++) {
                ptrdiff_t nl = (ptrdiff_t)l + DL[k];
                ptrdiff_t nc = (ptrdiff_t)c + DC[k];
                size_t v;

                if (nl < 0 || nl >= (ptrdiff_t)linhas ||
                    nc < 0 || nc >= (ptrdiff_t)colunas)
                    continue;
                v = (size_t)nl * colunas + (size_t)nc;
                if (m->celulas[v] && !visitado[v]) {
                    visitado[v] = 1; /* marca ao empilhar: cada celula entra 1 vez */
                    if (pilha_empilha(&pilha, (unsigned int)v) != 0)
                        goto sem_memoria;
                }
            }
        }
    }

    pilha_libera(&pilha);
    free(visitado);
    *objetos = cont;
    *tempo_ms = tempo_agora_ms() - t0;
    return 0;

sem_memoria:
    fprintf(stderr, "erro: memoria insuficiente para a pilha do flood fill\n");
    pilha_libera(&pilha);
    free(visitado);
    return -1;
}

static void uso(const char *prog)
{
    fprintf(stderr, "uso: %s ARQUIVO [-r REPETICOES]\n", prog);
}

int main(int argc, char **argv)
{
    const char *arquivo = NULL;
    unsigned long reps = 1, r, objetos = 0, esperado = 0;
    Matriz m;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            if (parse_ulong(argv[++i], 1, 100000UL, &reps) != 0) {
                uso(argv[0]);
                return 1;
            }
        } else if (argv[i][0] != '-' && arquivo == NULL) {
            arquivo = argv[i];
        } else {
            uso(argv[0]);
            return 1;
        }
    }
    if (arquivo == NULL) {
        uso(argv[0]);
        return 1;
    }
    if (matriz_carrega(arquivo, &m) != 0)
        return 1;

    printf("versao: sequencial\n");
    printf("arquivo: %s\n", arquivo);
    printf("dimensoes: %lu x %lu\n", (unsigned long)m.linhas,
           (unsigned long)m.colunas);

    for (r = 0; r < reps; r++) {
        double tempo;
        if (conta_sequencial(&m, &objetos, &tempo) != 0) {
            matriz_libera(&m);
            return 2;
        }
        if (r > 0 && objetos != esperado) {
            fprintf(stderr, "erro: resultado variou entre repeticoes\n");
            matriz_libera(&m);
            return 2;
        }
        esperado = objetos;
        printf("rep %lu tempo_ms %.3f\n", r + 1, tempo);
    }
    printf("objetos: %lu\n", objetos);

    matriz_libera(&m);
    return 0;
}
