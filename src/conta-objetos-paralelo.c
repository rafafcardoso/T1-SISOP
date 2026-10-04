/*
 * conta-objetos-paralelo.c
 *
 * Versao PARALELA (Pthreads) da contagem de objetos com conectividade 8.
 *
 * Estrategia (3 etapas):
 *
 *  FASE 1 (paralela)  A matriz e dividida em uma grade de NBL x NBC blocos.
 *    Os blocos formam uma fila dinamica de tarefas; cada thread retira um bloco
 *    por vez (mutex) e rotula os componentes locais com flood fill restrito ao
 *    bloco. Cada thread escreve apenas nas celulas do proprio bloco, logo nao
 *    ha disputa nos rotulos. Rotulo local: 1..n do bloco (0 = fundo).
 *
 *  ENTRE AS FASES (sequencial, main)  Soma-prefixo dos componentes por bloco
 *    gera o deslocamento de cada bloco. Id global = deslocamento + rotulo - 1.
 *    Assim rotulos de blocos diferentes nunca colidem antes da consolidacao.
 *
 *  FASE 2 (paralela)  Cada tarefa percorre apenas o PERIMETRO do bloco e, para
 *    cada celula 1, examina 4 vizinhos "para frente" (direita, abaixo-esquerda,
 *    abaixo, abaixo-direita). Se o vizinho esta em outro bloco e tambem e 1,
 *    registra a equivalencia (id_a, id_b). Como todo par de vizinhos e visto
 *    exatamente uma vez nessa varredura, horizontais, verticais, diagonais e o
 *    encontro de 4 blocos ficam cobertos. As equivalencias sao acumuladas em
 *    um lote local (sem lock) e aplicadas a um union-find COMPARTILHADO sob um
 *    unico mutex, um lote por vez (reduz contencao).
 *
 *  FASE 3 (sequencial, main)  Conta as raizes do union-find = numero de objetos.
 *    Verificacao cruzada: objetos == componentes_locais - unioes efetivas.
 *
 * Sincronizacao: join entre as fases funciona como barreira (portavel; macOS
 * nao possui pthread_barrier). Ha so dois mutexes, nunca adquiridos ao mesmo
 * tempo (fila de tarefas e union-find), o que elimina deadlock.
 *
 * Uso: conta-objetos-paralelo ARQUIVO [-t THREADS] [-b BLOCOS_L BLOCOS_C]
 *                                     [-r REPETICOES] [-v]
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "matriz.h"
#include "pilha.h"

#define MAX_THREADS 256
#define TAM_LOTE 512         /* equivalencias acumuladas antes de pegar o lock */
#define VERBOSE_MAX_DIM 64   /* -v so imprime grades ate esta dimensao */

/* 8 vizinhos para o flood fill local. */
static const int DL[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
static const int DC[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
/* 4 vizinhos "para frente": cada par de celulas vizinhas e visto uma so vez. */
static const int FL[4] = { 0, 1, 1, 1 };
static const int FC[4] = { 1, -1, 0, 1 };

/* ------------------------------------------------------------------ */
/* Estruturas                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    /* --- dados somente leitura durante as fases --- */
    size_t linhas, colunas;
    const unsigned char *mat;
    size_t nbl, nbc, nblocos;   /* grade de blocos */
    size_t *ini_l;              /* ini_l[i]..ini_l[i+1]-1: linhas da faixa i */
    size_t *ini_c;              /* idem para colunas */
    size_t *faixa_l;            /* linha  -> indice da faixa de blocos */
    size_t *faixa_c;            /* coluna -> indice da faixa de blocos */
    unsigned int *desloc;       /* deslocamento de ids globais por bloco */
    unsigned int total;         /* total de componentes locais */

    /* --- escritos por UM unico bloco/thread (sem disputa) --- */
    unsigned int *rot;          /* rotulo local por celula (0 = fundo) */
    unsigned int *ncomp;        /* componentes locais por bloco */

    /* --- fila dinamica de tarefas (protegida por mtx_fila) --- */
    pthread_mutex_t mtx_fila;
    size_t proxima;

    /* --- union-find compartilhado (protegido por mtx_dsu) --- */
    pthread_mutex_t mtx_dsu;
    unsigned int *pai;
    unsigned long unioes;       /* unioes que realmente juntaram 2 conjuntos */
} Contexto;

typedef struct {
    Contexto *ctx;
    int status;                 /* 0 = ok, -1 = erro */
    unsigned long tarefas;      /* blocos processados por esta thread */
} Trabalhador;

typedef struct {
    unsigned int a[TAM_LOTE];
    unsigned int b[TAM_LOTE];
    size_t n;
} Lote;

typedef struct {
    unsigned long threads;      /* threads solicitadas */
    unsigned long nbl, nbc;     /* 0 = escolher automaticamente */
    int verbose;
} Config;

typedef struct {
    unsigned long objetos;
    unsigned long locais;       /* componentes locais antes da consolidacao */
    unsigned long unioes;
    double tempo_ms;
    size_t nbl, nbc;
    size_t threads_efetivas;
    unsigned long tarefas_f1[MAX_THREADS]; /* blocos por thread na fase 1 */
    unsigned long tarefas_f2[MAX_THREADS]; /* blocos por thread na fase 2 */
} Resultado;

/* ------------------------------------------------------------------ */
/* Union-find (so e chamado com mtx_dsu adquirido, ou sem threads)      */
/* ------------------------------------------------------------------ */

static unsigned int dsu_find(unsigned int *pai, unsigned int x)
{
    while (pai[x] != x) {
        pai[x] = pai[pai[x]]; /* compressao de caminho (path halving) */
        x = pai[x];
    }
    return x;
}

/* A menor raiz vence: representante canonico e deterministico. */
static int dsu_union(unsigned int *pai, unsigned int a, unsigned int b)
{
    unsigned int ra = dsu_find(pai, a);
    unsigned int rb = dsu_find(pai, b);

    if (ra == rb)
        return 0;
    if (ra < rb)
        pai[rb] = ra;
    else
        pai[ra] = rb;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Utilitarios                                                         */
/* ------------------------------------------------------------------ */

static unsigned int id_global(const Contexto *ctx, size_t l, size_t c)
{
    size_t b = ctx->faixa_l[l] * ctx->nbc + ctx->faixa_c[c];
    return ctx->desloc[b] + ctx->rot[l * ctx->colunas + c] - 1U;
}

/* Divide n itens em k faixas; as primeiras (n % k) recebem 1 item a mais. */
static void divide_faixas(size_t n, size_t k, size_t *ini, size_t *faixa)
{
    size_t base = n / k, resto = n % k, i, j;

    ini[0] = 0;
    for (i = 0; i < k; i++)
        ini[i + 1] = ini[i] + base + (i < resto ? 1 : 0);
    for (i = 0; i < k; i++)
        for (j = ini[i]; j < ini[i + 1]; j++)
            faixa[j] = i;
}

/* Fila de tarefas. Retorna 1 (tarefa em *b), 0 (acabou) ou -1 (erro de lock). */
static int proxima_tarefa(Contexto *ctx, size_t *b)
{
    int rc, achou = 0;

    rc = pthread_mutex_lock(&ctx->mtx_fila);
    if (rc != 0) {
        fprintf(stderr, "erro: pthread_mutex_lock(fila): %s\n", strerror(rc));
        return -1;
    }
    if (ctx->proxima < ctx->nblocos) {
        *b = ctx->proxima++;
        achou = 1;
    }
    rc = pthread_mutex_unlock(&ctx->mtx_fila);
    if (rc != 0) {
        fprintf(stderr, "erro: pthread_mutex_unlock(fila): %s\n", strerror(rc));
        return -1;
    }
    return achou;
}

/* ------------------------------------------------------------------ */
/* Fase 1: rotulagem local de um bloco                                  */
/* ------------------------------------------------------------------ */

static int rotula_bloco(Contexto *ctx, Pilha *pilha, size_t b)
{
    const size_t colunas = ctx->colunas;
    const size_t bl = b / ctx->nbc, bc = b % ctx->nbc;
    const size_t l0 = ctx->ini_l[bl], l1 = ctx->ini_l[bl + 1];
    const size_t c0 = ctx->ini_c[bc], c1 = ctx->ini_c[bc + 1];
    unsigned int local = 0, atual;
    size_t l, c;

    for (l = l0; l < l1; l++) {
        for (c = c0; c < c1; c++) {
            size_t i = l * colunas + c;

            if (!ctx->mat[i] || ctx->rot[i] != 0)
                continue;

            local++;
            ctx->rot[i] = local;
            if (pilha_empilha(pilha, (unsigned int)i) != 0)
                return -1;

            while (pilha_desempilha(pilha, &atual)) {
                size_t al = atual / colunas, ac = atual % colunas;
                int k;

                for (k = 0; k < 8; k++) {
                    ptrdiff_t nl = (ptrdiff_t)al + DL[k];
                    ptrdiff_t nc = (ptrdiff_t)ac + DC[k];
                    size_t v;

                    /* o flood fill nunca sai do bloco */
                    if (nl < (ptrdiff_t)l0 || nl >= (ptrdiff_t)l1 ||
                        nc < (ptrdiff_t)c0 || nc >= (ptrdiff_t)c1)
                        continue;
                    v = (size_t)nl * colunas + (size_t)nc;
                    if (ctx->mat[v] && ctx->rot[v] == 0) {
                        ctx->rot[v] = local;
                        if (pilha_empilha(pilha, (unsigned int)v) != 0)
                            return -1;
                    }
                }
            }
        }
    }
    ctx->ncomp[b] = local;
    return 0;
}

static void *fase1(void *arg)
{
    Trabalhador *w = (Trabalhador *)arg;
    Pilha pilha;
    size_t b;
    int rc;

    w->status = 0;
    pilha_inicia(&pilha);
    while ((rc = proxima_tarefa(w->ctx, &b)) == 1) {
        if (rotula_bloco(w->ctx, &pilha, b) != 0) {
            fprintf(stderr, "erro: memoria insuficiente para a pilha (bloco %lu)\n",
                    (unsigned long)b);
            w->status = -1;
            break;
        }
        w->tarefas++;
    }
    if (rc < 0)
        w->status = -1;
    pilha_libera(&pilha);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Fase 2: equivalencias nas fronteiras                                 */
/* ------------------------------------------------------------------ */

/* Aplica o lote ao union-find compartilhado: UMA secao critica por lote. */
static int descarrega_lote(Contexto *ctx, Lote *lote)
{
    size_t i;
    int rc;

    if (lote->n == 0)
        return 0;
    rc = pthread_mutex_lock(&ctx->mtx_dsu);
    if (rc != 0) {
        fprintf(stderr, "erro: pthread_mutex_lock(dsu): %s\n", strerror(rc));
        return -1;
    }
    for (i = 0; i < lote->n; i++)
        ctx->unioes += (unsigned long)dsu_union(ctx->pai, lote->a[i], lote->b[i]);
    rc = pthread_mutex_unlock(&ctx->mtx_dsu);
    if (rc != 0) {
        fprintf(stderr, "erro: pthread_mutex_unlock(dsu): %s\n", strerror(rc));
        return -1;
    }
    lote->n = 0;
    return 0;
}

/* Examina os 4 vizinhos "para frente" de (l,c); registra os que cruzam blocos. */
static int trata_celula(Contexto *ctx, Lote *lote, size_t l, size_t c)
{
    const size_t colunas = ctx->colunas;
    int k;

    if (!ctx->mat[l * colunas + c])
        return 0;

    for (k = 0; k < 4; k++) {
        ptrdiff_t nl = (ptrdiff_t)l + FL[k];
        ptrdiff_t nc = (ptrdiff_t)c + FC[k];
        unsigned int ia, ib;

        if (nl < 0 || nl >= (ptrdiff_t)ctx->linhas ||
            nc < 0 || nc >= (ptrdiff_t)colunas)
            continue;
        if (!ctx->mat[(size_t)nl * colunas + (size_t)nc])
            continue;
        if (ctx->faixa_l[l] == ctx->faixa_l[nl] &&
            ctx->faixa_c[c] == ctx->faixa_c[nc])
            continue; /* mesmo bloco: ja unificado na fase 1 */

        ia = id_global(ctx, l, c);
        ib = id_global(ctx, (size_t)nl, (size_t)nc);
        if (lote->n > 0 && lote->a[lote->n - 1] == ia &&
            lote->b[lote->n - 1] == ib)
            continue; /* descarta repeticao imediata */
        lote->a[lote->n] = ia;
        lote->b[lote->n] = ib;
        lote->n++;
        if (lote->n == TAM_LOTE && descarrega_lote(ctx, lote) != 0)
            return -1;
    }
    return 0;
}

/*
 * Percorre o perimetro do bloco: ultima linha, primeira e ultima colunas.
 * Todo par de vizinhos que cruza blocos tem uma das celulas nesse conjunto
 * (ver comentario no topo do arquivo).
 */
static int analisa_fronteira(Contexto *ctx, Lote *lote, size_t b)
{
    const size_t bl = b / ctx->nbc, bc = b % ctx->nbc;
    const size_t l0 = ctx->ini_l[bl], l1 = ctx->ini_l[bl + 1];
    const size_t c0 = ctx->ini_c[bc], c1 = ctx->ini_c[bc + 1];
    size_t l, c;

    for (l = l0; l < l1; l++) {
        if (l == l1 - 1) {
            for (c = c0; c < c1; c++)
                if (trata_celula(ctx, lote, l, c) != 0)
                    return -1;
        } else {
            if (trata_celula(ctx, lote, l, c0) != 0)
                return -1;
            if (c1 - 1 != c0 && trata_celula(ctx, lote, l, c1 - 1) != 0)
                return -1;
        }
    }
    return 0;
}

static void *fase2(void *arg)
{
    Trabalhador *w = (Trabalhador *)arg;
    Lote lote;
    size_t b;
    int rc;

    w->status = 0;
    lote.n = 0;
    while ((rc = proxima_tarefa(w->ctx, &b)) == 1) {
        if (analisa_fronteira(w->ctx, &lote, b) != 0) {
            w->status = -1;
            return NULL;
        }
        w->tarefas++;
    }
    if (rc < 0 || descarrega_lote(w->ctx, &lote) != 0)
        w->status = -1;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Criacao e juncao de threads                                          */
/* ------------------------------------------------------------------ */

/* Executa 'fn' em n threads e espera todas (o join e a barreira da fase). */
static int executa_fase(Contexto *ctx, size_t n, void *(*fn)(void *),
                        unsigned long *tarefas)
{
    Trabalhador *ws = (Trabalhador *)calloc(n, sizeof(Trabalhador));
    pthread_t *th = (pthread_t *)malloc(n * sizeof(pthread_t));
    size_t i, criadas = 0;
    int erro = 0, rc;

    if (ws == NULL || th == NULL) {
        fprintf(stderr, "erro: memoria insuficiente para as threads\n");
        free(ws);
        free(th);
        return -1;
    }

    ctx->proxima = 0; /* nenhuma thread ativa neste ponto */
    for (i = 0; i < n; i++) {
        ws[i].ctx = ctx;
        rc = pthread_create(&th[i], NULL, fn, &ws[i]);
        if (rc != 0) {
            fprintf(stderr, "erro: pthread_create: %s\n", strerror(rc));
            erro = 1;
            break;
        }
        criadas++;
    }
    for (i = 0; i < criadas; i++) {
        rc = pthread_join(th[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "erro: pthread_join: %s\n", strerror(rc));
            erro = 1;
        } else if (ws[i].status != 0) {
            erro = 1;
        }
        tarefas[i] = ws[i].tarefas;
    }
    free(ws);
    free(th);
    return erro ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* Saida detalhada (-v): grades de ids antes e depois da consolidacao   */
/* ------------------------------------------------------------------ */

static int largura_num(unsigned long v)
{
    int w = 1;
    while (v >= 10) {
        v /= 10;
        w++;
    }
    return w;
}

static void imprime_grade(const Contexto *ctx, const unsigned int *valor,
                          int largura)
{
    size_t l, c, k, tam;

    tam = ctx->colunas * (size_t)(largura + 1) + (ctx->nbc - 1) * 2;
    for (l = 0; l < ctx->linhas; l++) {
        if (l > 0 && ctx->faixa_l[l] != ctx->faixa_l[l - 1]) {
            printf("  ");
            for (k = 0; k < tam; k++)
                putchar('-');
            putchar('\n');
        }
        printf("  ");
        for (c = 0; c < ctx->colunas; c++) {
            if (c > 0 && ctx->faixa_c[c] != ctx->faixa_c[c - 1])
                printf(" |");
            if (ctx->mat[l * ctx->colunas + c])
                printf(" %*u", largura, valor[l * ctx->colunas + c]);
            else
                printf(" %*s", largura, ".");
        }
        putchar('\n');
    }
}

static void imprime_detalhes(Contexto *ctx)
{
    const size_t n = ctx->linhas * ctx->colunas;
    int w = largura_num(ctx->total);
    unsigned int *ids = (unsigned int *)malloc(n * sizeof(unsigned int));
    unsigned int *novo = (unsigned int *)calloc((size_t)ctx->total + 1,
                                                sizeof(unsigned int));
    unsigned int proximo = 0;
    size_t l, c, b, i;

    if (ids == NULL || novo == NULL) {
        fprintf(stderr, "aviso: sem memoria para a saida detalhada\n");
        free(ids);
        free(novo);
        return;
    }

    printf("\n[blocos] grade %lu x %lu\n", (unsigned long)ctx->nbl,
           (unsigned long)ctx->nbc);
    for (b = 0; b < ctx->nblocos; b++) {
        size_t bl = b / ctx->nbc, bc = b % ctx->nbc;
        printf("  bloco %lu (%lu,%lu): linhas %lu-%lu, colunas %lu-%lu, "
               "componentes locais: %u\n",
               (unsigned long)b, (unsigned long)bl, (unsigned long)bc,
               (unsigned long)ctx->ini_l[bl],
               (unsigned long)(ctx->ini_l[bl + 1] - 1),
               (unsigned long)ctx->ini_c[bc],
               (unsigned long)(ctx->ini_c[bc + 1] - 1), ctx->ncomp[b]);
    }

    /* ids globais antes da consolidacao (exibidos a partir de 1) */
    for (l = 0; l < ctx->linhas; l++)
        for (c = 0; c < ctx->colunas; c++)
            if (ctx->mat[l * ctx->colunas + c])
                ids[l * ctx->colunas + c] = id_global(ctx, l, c) + 1U;
    printf("\n[antes da consolidacao] id global de cada componente local\n");
    imprime_grade(ctx, ids, w);

    printf("\n[equivalencias] id -> representante (menor id do objeto)\n");
    for (i = 0; i < ctx->total; i++) {
        unsigned int r = dsu_find(ctx->pai, (unsigned int)i);
        if (r != (unsigned int)i)
            printf("  %u -> %u\n", (unsigned int)i + 1U, r + 1U);
    }

    /* numeracao final 1..N pela ordem de aparicao na varredura */
    for (i = 0; i < n; i++) {
        if (ctx->mat[i]) {
            unsigned int r = dsu_find(ctx->pai,
                                      id_global(ctx, i / ctx->colunas,
                                                i % ctx->colunas));
            if (novo[r] == 0)
                novo[r] = ++proximo;
            ids[i] = novo[r];
        }
    }
    printf("\n[depois da consolidacao] objeto de cada celula (1..%u)\n", proximo);
    imprime_grade(ctx, ids, largura_num(proximo));

    free(ids);
    free(novo);
}

/* ------------------------------------------------------------------ */
/* Orquestracao                                                         */
/* ------------------------------------------------------------------ */

static int conta_paralelo(const Matriz *m, const Config *cfg, Resultado *res)
{
    Contexto ctx;
    size_t nbl, nbc, b, i, nthreads;
    unsigned long objetos = 0, acum = 0;
    int mtx_fila_ok = 0, mtx_dsu_ok = 0, rc, ret = -1;
    double t0 = tempo_agora_ms();

    memset(&ctx, 0, sizeof(ctx));
    ctx.linhas = m->linhas;
    ctx.colunas = m->colunas;
    ctx.mat = m->celulas;

    /* grade de blocos: padrao = uma faixa de linhas por thread */
    nbl = (cfg->nbl != 0) ? (size_t)cfg->nbl : (size_t)cfg->threads;
    nbc = (cfg->nbc != 0) ? (size_t)cfg->nbc : 1;
    if (nbl > ctx.linhas)
        nbl = ctx.linhas;
    if (nbc > ctx.colunas)
        nbc = ctx.colunas;
    ctx.nbl = nbl;
    ctx.nbc = nbc;
    ctx.nblocos = nbl * nbc;
    nthreads = (size_t)cfg->threads;
    if (nthreads > ctx.nblocos)
        nthreads = ctx.nblocos; /* thread sem bloco nao teria o que fazer */

    ctx.ini_l = (size_t *)malloc((nbl + 1) * sizeof(size_t));
    ctx.ini_c = (size_t *)malloc((nbc + 1) * sizeof(size_t));
    ctx.faixa_l = (size_t *)malloc(ctx.linhas * sizeof(size_t));
    ctx.faixa_c = (size_t *)malloc(ctx.colunas * sizeof(size_t));
    ctx.desloc = (unsigned int *)malloc(ctx.nblocos * sizeof(unsigned int));
    ctx.ncomp = (unsigned int *)calloc(ctx.nblocos, sizeof(unsigned int));
    ctx.rot = (unsigned int *)calloc(ctx.linhas * ctx.colunas,
                                     sizeof(unsigned int));
    if (!ctx.ini_l || !ctx.ini_c || !ctx.faixa_l || !ctx.faixa_c ||
        !ctx.desloc || !ctx.ncomp || !ctx.rot) {
        fprintf(stderr, "erro: memoria insuficiente\n");
        goto fim;
    }
    divide_faixas(ctx.linhas, nbl, ctx.ini_l, ctx.faixa_l);
    divide_faixas(ctx.colunas, nbc, ctx.ini_c, ctx.faixa_c);

    rc = pthread_mutex_init(&ctx.mtx_fila, NULL);
    if (rc != 0) {
        fprintf(stderr, "erro: pthread_mutex_init(fila): %s\n", strerror(rc));
        goto fim;
    }
    mtx_fila_ok = 1;
    rc = pthread_mutex_init(&ctx.mtx_dsu, NULL);
    if (rc != 0) {
        fprintf(stderr, "erro: pthread_mutex_init(dsu): %s\n", strerror(rc));
        goto fim;
    }
    mtx_dsu_ok = 1;

    /* FASE 1 (paralela): rotulagem local por bloco */
    if (executa_fase(&ctx, nthreads, fase1, res->tarefas_f1) != 0)
        goto fim;

    /* Soma-prefixo (sequencial): deslocamento de ids de cada bloco */
    for (b = 0; b < ctx.nblocos; b++) {
        ctx.desloc[b] = (unsigned int)acum;
        acum += ctx.ncomp[b];
    }
    ctx.total = (unsigned int)acum;

    ctx.pai = (unsigned int *)malloc(((size_t)ctx.total + 1) * sizeof(unsigned int));
    if (ctx.pai == NULL) {
        fprintf(stderr, "erro: memoria insuficiente para o union-find\n");
        goto fim;
    }
    for (i = 0; i < ctx.total; i++)
        ctx.pai[i] = (unsigned int)i;

    /* FASE 2 (paralela): equivalencias nas fronteiras entre blocos */
    if (executa_fase(&ctx, nthreads, fase2, res->tarefas_f2) != 0)
        goto fim;

    /* FASE 3 (sequencial): cada raiz do union-find e um objeto */
    for (i = 0; i < ctx.total; i++)
        if (ctx.pai[i] == (unsigned int)i)
            objetos++;

    /* invariante: cada uniao efetiva reduz em 1 o numero de componentes */
    if (objetos != (unsigned long)ctx.total - ctx.unioes) {
        fprintf(stderr, "erro interno: contagem inconsistente (%lu != %lu - %lu)\n",
                objetos, (unsigned long)ctx.total, ctx.unioes);
        goto fim;
    }

    res->tempo_ms = tempo_agora_ms() - t0; /* nao inclui a saida detalhada */
    res->objetos = objetos;
    res->locais = ctx.total;
    res->unioes = ctx.unioes;
    res->nbl = nbl;
    res->nbc = nbc;
    res->threads_efetivas = nthreads;

    if (cfg->verbose && ctx.linhas <= VERBOSE_MAX_DIM &&
        ctx.colunas <= VERBOSE_MAX_DIM)
        imprime_detalhes(&ctx);

    ret = 0;

fim:
    if (mtx_dsu_ok && (rc = pthread_mutex_destroy(&ctx.mtx_dsu)) != 0)
        fprintf(stderr, "aviso: pthread_mutex_destroy(dsu): %s\n", strerror(rc));
    if (mtx_fila_ok && (rc = pthread_mutex_destroy(&ctx.mtx_fila)) != 0)
        fprintf(stderr, "aviso: pthread_mutex_destroy(fila): %s\n", strerror(rc));
    free(ctx.ini_l);
    free(ctx.ini_c);
    free(ctx.faixa_l);
    free(ctx.faixa_c);
    free(ctx.desloc);
    free(ctx.ncomp);
    free(ctx.rot);
    free(ctx.pai);
    return ret;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

static void uso(const char *prog)
{
    fprintf(stderr,
            "uso: %s ARQUIVO [-t THREADS] [-b BLOCOS_L BLOCOS_C] [-r REPETICOES] [-v]\n"
            "  -t  numero de threads (padrao 2)\n"
            "  -b  grade de blocos (padrao: THREADS x 1, faixas de linhas)\n"
            "  -r  repeticoes do calculo (para medir tempo)\n"
            "  -v  mostra blocos, rotulos e equivalencias (matrizes ate %d x %d)\n",
            prog, VERBOSE_MAX_DIM, VERBOSE_MAX_DIM);
}

int main(int argc, char **argv)
{
    const char *arquivo = NULL;
    Config cfg;
    unsigned long reps = 1, r, esperado = 0;
    Resultado res;
    Matriz m;
    size_t k;
    int i;

    cfg.threads = 2;
    cfg.nbl = 0;
    cfg.nbc = 0;
    cfg.verbose = 0;
    memset(&res, 0, sizeof(res));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            if (parse_ulong(argv[++i], 1, MAX_THREADS, &cfg.threads) != 0)
                goto uso_erro;
        } else if (strcmp(argv[i], "-b") == 0 && i + 2 < argc) {
            if (parse_ulong(argv[i + 1], 1, 1000000UL, &cfg.nbl) != 0 ||
                parse_ulong(argv[i + 2], 1, 1000000UL, &cfg.nbc) != 0)
                goto uso_erro;
            i += 2;
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            if (parse_ulong(argv[++i], 1, 100000UL, &reps) != 0)
                goto uso_erro;
        } else if (strcmp(argv[i], "-v") == 0) {
            cfg.verbose = 1;
        } else if (argv[i][0] != '-' && arquivo == NULL) {
            arquivo = argv[i];
        } else {
            goto uso_erro;
        }
    }
    if (arquivo == NULL)
        goto uso_erro;

    if (cfg.threads < 2)
        fprintf(stderr, "aviso: com 1 thread nao ha paralelismo (apenas medicao de sobrecarga)\n");

    if (matriz_carrega(arquivo, &m) != 0)
        return 1;

    printf("versao: paralela (pthreads)\n");
    printf("arquivo: %s\n", arquivo);
    printf("dimensoes: %lu x %lu\n", (unsigned long)m.linhas,
           (unsigned long)m.colunas);

    for (r = 0; r < reps; r++) {
        Config c = cfg;
        if (r > 0)
            c.verbose = 0; /* detalhes so na primeira repeticao */
        if (conta_paralelo(&m, &c, &res) != 0) {
            matriz_libera(&m);
            return 2;
        }
        if (r > 0 && res.objetos != esperado) {
            fprintf(stderr, "erro: resultado variou entre repeticoes\n");
            matriz_libera(&m);
            return 2;
        }
        esperado = res.objetos;
        if (r == 0) {
            printf("threads: %lu (efetivas: %lu)\n", cfg.threads,
                   (unsigned long)res.threads_efetivas);
            printf("blocos: %lu x %lu\n", (unsigned long)res.nbl,
                   (unsigned long)res.nbc);
            printf("tarefas_por_thread_fase1:");
            for (k = 0; k < res.threads_efetivas; k++)
                printf(" %lu", res.tarefas_f1[k]);
            printf("\ntarefas_por_thread_fase2:");
            for (k = 0; k < res.threads_efetivas; k++)
                printf(" %lu", res.tarefas_f2[k]);
            printf("\n");
        }
        printf("rep %lu tempo_ms %.3f\n", r + 1, res.tempo_ms);
    }
    printf("componentes_locais: %lu\n", res.locais);
    printf("unioes_nas_fronteiras: %lu\n", res.unioes);
    printf("objetos: %lu\n", res.objetos);

    matriz_libera(&m);
    return 0;

uso_erro:
    uso(argv[0]);
    return 1;
}
