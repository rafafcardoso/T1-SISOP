/*
 * matriz.h - Matriz binaria, leitura de arquivo e utilitarios de entrada.
 *
 * Modulo compartilhado pelas versoes sequencial e paralela.
 * Formato do arquivo de entrada (texto):
 *     LINHAS COLUNAS
 *     celulas '0' ou '1', linha a linha (separadas ou nao por espacos)
 */
#ifndef MATRIZ_H
#define MATRIZ_H

#include <stddef.h>

typedef struct {
    size_t linhas;
    size_t colunas;
    unsigned char *celulas; /* linhas * colunas valores (0 ou 1), por linha */
} Matriz;

/* Limite de celulas: os rotulos usam unsigned int. */
#define MATRIZ_MAX_CELULAS ((unsigned long)(~0U) - 1UL)

/* Le a matriz do arquivo. Retorna 0 em sucesso e -1 em erro (mensagem em stderr). */
int matriz_carrega(const char *caminho, Matriz *m);

/* Libera a memoria da matriz. */
void matriz_libera(Matriz *m);

/* Relogio monotonico em milissegundos (clock_gettime). */
double tempo_agora_ms(void);

/* Converte string em inteiro sem sinal dentro de [min, max]. 0 = ok, -1 = erro. */
int parse_ulong(const char *s, unsigned long min, unsigned long max,
                unsigned long *saida);

#endif
