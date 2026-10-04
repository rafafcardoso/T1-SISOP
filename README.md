# Contagem paralela de objetos em uma matriz binária

**Disciplina:** Sistemas Operacionais – 2026/II – PUCRS, Escola Politécnica (Prof. Filipo Mór)
**Autoria:** [PREENCHER nome(s) e matrícula(s)]
**Linguagem:** ANSI C (C89/C90) + POSIX (Pthreads) · **Plataforma:** Linux / macOS

Conta *objetos* (regiões de células `1` conectadas por arestas ou cantos – **conectividade 8**) em uma
matriz binária, com duas implementações equivalentes:

| Versão | Arquivo | Descrição |
|---|---|---|
| Sequencial | `src/conta-objetos-sequencial.c` | Flood fill iterativo + vetor de visitados. Referência de correção e de desempenho. |
| Paralela | `src/conta-objetos-paralelo.c` | Pthreads: blocos + fila dinâmica de tarefas + consolidação das fronteiras com union-find. |

Módulos comuns: `src/matriz.[ch]` (leitura/validação, relógio, parsing), `src/pilha.[ch]` (pilha dinâmica).
Ferramenta: `src/gera-matriz.c` (matrizes aleatórias reprodutíveis para testes e desempenho).
Relatório completo: [RELATORIO_TECNICO.md](RELATORIO_TECNICO.md).

## Compilação

```bash
make          # gera bin/conta-objetos-sequencial, bin/conta-objetos-paralelo, bin/gera-matriz
make clean
```

Flags: `-std=c89 -Wall -Wextra -pedantic -O2` (+ `-pthread` na versão paralela). Compila **sem avisos**.
Sem `make`:

```bash
cc -std=c89 -Wall -Wextra -pedantic src/conta-objetos-sequencial.c src/matriz.c src/pilha.c -o conta-objetos-sequencial
cc -std=c89 -Wall -Wextra -pedantic -pthread src/conta-objetos-paralelo.c src/matriz.c src/pilha.c -o conta-objetos-paralelo
```

## Execução

```bash
bin/conta-objetos-sequencial ARQUIVO [-r REPETICOES]
bin/conta-objetos-paralelo   ARQUIVO [-t THREADS] [-b BLOCOS_L BLOCOS_C] [-r REPETICOES] [-v]
```

| Opção | Significado |
|---|---|
| `-t N` | número de threads (padrão 2; máx. 256) |
| `-b L C` | grade de blocos `L x C` (padrão: `N x 1`, uma faixa de linhas por thread). Pode haver mais blocos que threads. |
| `-r N` | repete o cálculo N vezes e imprime o tempo de cada uma (excluindo a leitura do arquivo) |
| `-v` | mostra blocos, ids locais, equivalências e a numeração final (matrizes até 64 x 64) |

**Exemplo** (encontro de quatro blocos, exemplo 3 do enunciado):

```bash
bin/conta-objetos-sequencial tests/obrigatorios/exemplo3.txt
bin/conta-objetos-paralelo   tests/obrigatorios/exemplo3.txt -t 4 -b 2 2 -v
```

**Formato do arquivo:** primeira linha `LINHAS COLUNAS`; depois as células `0`/`1`, com ou sem espaços.

```
5 5
1 1 0 0 0
...
```

Saída: `objetos: N` (mais tempos, e na versão paralela: componentes locais, uniões nas fronteiras e
quantas tarefas cada thread executou). Código de saída: `0` ok, `1` uso/entrada inválida, `2` erro de execução.

## Testes e desempenho

```bash
make test     # obrigatórias + adicionais + 20 aleatórias x 12 configs + determinismo + entradas inválidas
make tsan     # suíte sob ThreadSanitizer (condições de corrida)
make asan     # suíte sob Address+UBSan (memória e comportamento indefinido)
make bench    # gera matriz grande, mede seq x paralelo -> results/medicoes.csv e results/resumo.md
python3 tests/graficos.py   # (opcional, requer matplotlib) gera os gráficos em results/
```

Parâmetros do `bench` por variáveis de ambiente, ex.: `LINHAS=8000 COLUNAS=8000 THREADS="2 4 8 16" make bench`.

## Arquitetura da versão paralela

1. **Fase 1 – paralela.** A matriz é dividida em `NBL x NBC` blocos (sobras distribuídas às primeiras faixas).
   Os blocos formam uma **fila dinâmica**; cada thread retira um por vez (mutex) e rotula os componentes
   locais com flood fill *restrito ao bloco*. Cada thread escreve só nas células do seu bloco: sem disputa.
2. **Soma-prefixo – sequencial.** Deslocamento por bloco ⇒ *id global* único para cada componente local.
3. **Fase 2 – paralela.** Cada tarefa percorre o **perímetro** do bloco e, para cada célula `1`, examina 4
   vizinhos "para frente" (direita, abaixo-esq., abaixo, abaixo-dir.). Vizinho `1` em outro bloco ⇒
   equivalência `(id_a, id_b)`. Cobre horizontal, vertical, diagonal e o encontro de 4 blocos. As
   equivalências vão para um **union-find compartilhado** sob **um mutex**, em lotes de 512 (menos contenção).
4. **Fase 3 – sequencial.** Objetos = número de raízes do union-find. Verificação cruzada:
   `objetos == componentes_locais − uniões_efetivas`.

`pthread_join` entre as fases faz o papel de barreira (portável: macOS não tem `pthread_barrier`).
Os dois mutexes (fila e union-find) **nunca são mantidos ao mesmo tempo** ⇒ sem deadlock.
Detalhes e justificativas no relatório.

## Estrutura

```
README.md  RELATORIO_TECNICO.md  Makefile
src/        código-fonte
tests/      obrigatorios/ (5 matrizes do enunciado), adicionais/, run_tests.sh, bench.sh, graficos.py
results/    testes.txt, sanitizers.txt (+ medicoes.csv, resumo.md e gráficos após `make bench`)
```

## Ferramentas, referências e código externo

- Nenhuma biblioteca externa; apenas libc e POSIX Threads.
- Algoritmos clássicos: flood fill e union-find (disjoint-set) com compressão de caminho.
- `tests/graficos.py` (opcional) usa matplotlib apenas para desenhar gráficos.
- [PREENCHER: declare aqui o uso de ferramentas de IA, referências e como os resultados foram verificados.]
