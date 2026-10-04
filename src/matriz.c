/* matriz.c - Leitura da matriz binaria e utilitarios (ANSI C + POSIX clock). */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "matriz.h"

#define TAM_BUFFER 65536

int matriz_carrega(const char *caminho, Matriz *m)
{
    FILE *f;
    unsigned long l, c;
    unsigned char *celulas = NULL;
    unsigned char *buf = NULL;
    size_t total, n = 0, i, lidos;
    int ret = -1;

    m->linhas = 0;
    m->colunas = 0;
    m->celulas = NULL;

    f = fopen(caminho, "rb");
    if (f == NULL) {
        fprintf(stderr, "erro: nao foi possivel abrir '%s'\n", caminho);
        return -1;
    }
    if (fscanf(f, "%lu %lu", &l, &c) != 2 || l == 0 || c == 0) {
        fprintf(stderr, "erro: cabecalho invalido em '%s' (esperado: LINHAS COLUNAS)\n",
                caminho);
        goto fim;
    }
    if (c > MATRIZ_MAX_CELULAS / l) {
        fprintf(stderr, "erro: matriz %lu x %lu excede o limite de celulas\n", l, c);
        goto fim;
    }
    total = (size_t)l * (size_t)c;

    celulas = (unsigned char *)malloc(total);
    buf = (unsigned char *)malloc(TAM_BUFFER);
    if (celulas == NULL || buf == NULL) {
        fprintf(stderr, "erro: memoria insuficiente para a matriz\n");
        goto fim;
    }

    while ((lidos = fread(buf, 1, TAM_BUFFER, f)) > 0) {
        for (i = 0; i < lidos; i++) {
            unsigned char ch = buf[i];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                continue;
            if (ch != '0' && ch != '1') {
                fprintf(stderr, "erro: caractere invalido '%c' na celula %lu\n",
                        ch, (unsigned long)n);
                goto fim;
            }
            if (n >= total) {
                fprintf(stderr, "erro: ha mais celulas do que %lu x %lu\n", l, c);
                goto fim;
            }
            celulas[n++] = (unsigned char)(ch - '0');
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "erro: falha de leitura em '%s'\n", caminho);
        goto fim;
    }
    if (n != total) {
        fprintf(stderr, "erro: esperadas %lu celulas, lidas %lu\n",
                (unsigned long)total, (unsigned long)n);
        goto fim;
    }

    m->linhas = (size_t)l;
    m->colunas = (size_t)c;
    m->celulas = celulas;
    celulas = NULL; /* posse transferida para a matriz */
    ret = 0;

fim:
    free(buf);
    free(celulas);
    fclose(f);
    return ret;
}

void matriz_libera(Matriz *m)
{
    free(m->celulas);
    m->celulas = NULL;
    m->linhas = 0;
    m->colunas = 0;
}

double tempo_agora_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        return 0.0;
    }
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

int parse_ulong(const char *s, unsigned long min, unsigned long max,
                unsigned long *saida)
{
    char *fim;
    unsigned long v;

    if (s == NULL || *s == '\0' || *s == '-')
        return -1;
    errno = 0;
    v = strtoul(s, &fim, 10);
    if (errno != 0 || *fim != '\0' || v < min || v > max)
        return -1;
    *saida = v;
    return 0;
}
