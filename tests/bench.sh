#!/bin/sh
# bench.sh - Mede sequencial x paralelo em uma matriz grande.
# Gera results/medicoes.csv (dados brutos), results/resumo.md (tabela
# consolidada) e results/ambiente.txt.
#
# Variaveis: LINHAS COLUNAS DENSIDADE SEMENTE REPS THREADS BIN
# Uso: sh tests/bench.sh   (ou: LINHAS=8000 COLUNAS=8000 sh tests/bench.sh)

BIN=${BIN:-bin}
LINHAS=${LINHAS:-4000}
COLUNAS=${COLUNAS:-4000}
DENSIDADE=${DENSIDADE:-35}
SEMENTE=${SEMENTE:-42}
REPS=${REPS:-7}
THREADS=${THREADS:-"2 4 8"}
DIR=$(dirname "$0")
RES="$DIR/../results"
MAT="$DIR/adicionais/grande.txt"   # ignorada pelo git (ver .gitignore)
NOME="grande_${LINHAS}x${COLUNAS}_d${DENSIDADE}_s${SEMENTE}"

mkdir -p "$RES"
"$BIN/gera-matriz" "$LINHAS" "$COLUNAS" "$DENSIDADE" "$SEMENTE" > "$MAT" || exit 1

{
    echo "data: $(date)"
    echo "kernel: $(uname -a)"
    echo "nucleos_logicos: $( (nproc || sysctl -n hw.ncpu) 2>/dev/null )"
    echo "cpu: $( (grep -m1 'model name' /proc/cpuinfo | cut -d: -f2) 2>/dev/null || sysctl -n machdep.cpu.brand_string 2>/dev/null )"
    echo "compilador: $(cc --version | head -1)"
} > "$RES/ambiente.txt"

CSV="$RES/medicoes.csv"
echo "matriz,linhas,colunas,versao,trabalhadores,repeticao,tempo_ms,objetos,resultado_correto" > "$CSV"

# 1 execucao de aquecimento descartada + REPS medidas, dentro do mesmo processo
saida=$("$BIN/conta-objetos-sequencial" "$MAT" -r $((REPS + 1)))
ref=$(echo "$saida" | awk '/^objetos:/ {print $2}')
echo "$saida" | awk -v n="$NOME" -v l="$LINHAS" -v c="$COLUNAS" -v o="$ref" \
    '/^rep/ && $2 > 1 {printf "%s,%s,%s,sequencial,1,%d,%s,%s,true\n", n, l, c, $2-1, $4, o}' >> "$CSV"

for t in $THREADS; do
    saida=$("$BIN/conta-objetos-paralelo" "$MAT" -t "$t" -r $((REPS + 1)))
    obj=$(echo "$saida" | awk '/^objetos:/ {print $2}')
    ok=false; [ "$obj" = "$ref" ] && ok=true
    echo "$saida" | awk -v n="$NOME" -v l="$LINHAS" -v c="$COLUNAS" -v t="$t" -v o="$obj" -v ok="$ok" \
        '/^rep/ && $2 > 1 {printf "%s,%s,%s,paralela,%s,%d,%s,%s,%s\n", n, l, c, t, $2-1, $4, o, ok}' >> "$CSV"
done

# Resumo: mediana, minimo, maximo, aceleracao e eficiencia
awk -F, 'NR > 1 {
    k = $4 "," $5
    n[k]++; v[k, n[k]] = $7; if (!(k in ord)) { ord[k] = ++nk; chave[nk] = k }
    if ($9 != "true") errado[k] = 1
}
function mediana(k,   i, j, t, a) {
    for (i = 1; i <= n[k]; i++) a[i] = v[k, i]
    for (i = 2; i <= n[k]; i++) { t = a[i]; for (j = i - 1; j > 0 && a[j] > t; j--) a[j+1] = a[j]; a[j+1] = t }
    mn = a[1]; mx = a[n[k]]
    return (n[k] % 2) ? a[(n[k]+1)/2] : (a[n[k]/2] + a[n[k]/2+1]) / 2
}
END {
    print "| Versao | Trabalhadores (p) | Mediana (ms) | Min (ms) | Max (ms) | Aceleracao S(p) | Eficiencia E(p) | Correto? |"
    print "|---|---:|---:|---:|---:|---:|---:|---|"
    ts = mediana("sequencial,1")
    for (i = 1; i <= nk; i++) {
        k = chave[i]; split(k, p, ",")
        m = mediana(k)
        printf "| %s | %s | %.2f | %.2f | %.2f | %.2f | %.2f | %s |\n", p[1], p[2], m, mn, mx, ts / m, ts / m / p[2], (k in errado) ? "NAO" : "sim"
    }
}' "$CSV" > "$RES/resumo.md"

echo "Matriz: $NOME (objetos=$ref)"
cat "$RES/resumo.md"
echo "Dados brutos: $CSV"
