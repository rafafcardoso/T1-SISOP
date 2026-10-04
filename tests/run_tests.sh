#!/bin/sh
# run_tests.sh - Suite de testes: obrigatorios, adicionais, estresse aleatorio
# e determinismo. Compara a versao paralela com a sequencial em varias
# configuracoes de threads e de grade de blocos.
#
# Uso: BIN=bin sh tests/run_tests.sh

BIN=${BIN:-bin}
SEQ="$BIN/conta-objetos-sequencial"
PAR="$BIN/conta-objetos-paralelo"
GERA="$BIN/gera-matriz"
DIR=$(dirname "$0")
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

falhas=0
total=0

# configuracoes paralelas: "-t N -b L C" (algumas forcam 1 bloco por celula)
CONFIGS="
-t 2
-t 3
-t 4
-t 8
-t 2 -b 2 2
-t 4 -b 2 2
-t 4 -b 3 3
-t 8 -b 3 3
-t 2 -b 1 3
-t 3 -b 3 1
-t 4 -b 4 4
-t 4 -b 1000 1000
"

objetos() { # extrai o numero de objetos da saida do programa
    awk '/^objetos:/ {print $2}'
}

confere() { # confere ARQUIVO ESPERADO ROTULO
    arq=$1; esp=$2; rot=$3
    s=$($SEQ "$arq" | objetos)
    total=$((total + 1))
    if [ "$s" != "$esp" ]; then
        echo "FALHOU [seq] $rot: esperado=$esp obtido=$s"
        falhas=$((falhas + 1))
    fi
    echo "$CONFIGS" | while read -r cfg; do
        [ -z "$cfg" ] && continue
        p=$($PAR "$arq" $cfg | objetos)
        if [ "$p" != "$esp" ]; then
            echo "FALHOU [par $cfg] $rot: esperado=$esp obtido=$p"
            echo x >> "$TMP/falhas"
        fi
    done
}

echo "== Matrizes obrigatorias =="
while read -r f e; do
    confere "$DIR/obrigatorios/$f" "$e" "$f"
    printf '  %-22s esperado=%s  seq=%s  par(2x2,4t)=%s\n' "$f" "$e" \
        "$($SEQ "$DIR/obrigatorios/$f" | objetos)" \
        "$($PAR "$DIR/obrigatorios/$f" -t 4 -b 2 2 | objetos)"
done < "$DIR/obrigatorios/esperados.txt"

echo "== Casos adicionais =="
while read -r f e; do
    confere "$DIR/adicionais/$f" "$e" "$f"
    printf '  %-22s esperado=%s  seq=%s  par(2x2,4t)=%s\n' "$f" "$e" \
        "$($SEQ "$DIR/adicionais/$f" | objetos)" \
        "$($PAR "$DIR/adicionais/$f" -t 4 -b 2 2 | objetos)"
done < "$DIR/adicionais/esperados.txt"

echo "== Estresse aleatorio (paralelo == sequencial) =="
n=0
for sem in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    l=$(( (sem * 7) % 53 + 1 ))
    c=$(( (sem * 11) % 61 + 1 ))
    d=$(( 15 + (sem * 13) % 45 ))
    "$GERA" $l $c $d $sem > "$TMP/r.txt"
    s=$($SEQ "$TMP/r.txt" | objetos)
    echo "$CONFIGS" | while read -r cfg; do
        [ -z "$cfg" ] && continue
        p=$($PAR "$TMP/r.txt" $cfg | objetos)
        if [ "$p" != "$s" ]; then
            echo "FALHOU aleatoria ${l}x${c} dens=$d sem=$sem [$cfg]: seq=$s par=$p"
            echo x >> "$TMP/falhas"
        fi
    done
    n=$((n + 1))
done
echo "  $n matrizes aleatorias x $(echo "$CONFIGS" | grep -c .) configuracoes"

echo "== Determinismo (50 execucoes da mesma configuracao) =="
"$GERA" 200 300 40 99 > "$TMP/d.txt"
ref=$($SEQ "$TMP/d.txt" | objetos)
dif=0
i=0
while [ $i -lt 50 ]; do
    p=$($PAR "$TMP/d.txt" -t 8 -b 4 4 | objetos)
    [ "$p" != "$ref" ] && dif=$((dif + 1))
    i=$((i + 1))
done
echo "  referencia=$ref divergencias=$dif"
[ "$dif" -ne 0 ] && echo x >> "$TMP/falhas"

echo "== Entradas invalidas devem falhar com erro =="
printf '2 2\n1 0\n1 x\n' > "$TMP/inv1.txt"
printf '2 2\n1 0\n1\n'   > "$TMP/inv2.txt"
for f in inv1 inv2; do
    if $SEQ "$TMP/$f.txt" >/dev/null 2>&1 || $PAR "$TMP/$f.txt" >/dev/null 2>&1; then
        echo "FALHOU: $f deveria ser rejeitada"; echo x >> "$TMP/falhas"
    else
        echo "  $f rejeitada corretamente"
    fi
done

if [ -s "$TMP/falhas" ] || [ "$falhas" -ne 0 ]; then
    echo "RESULTADO: FALHOU"
    exit 1
fi
echo "RESULTADO: todos os testes passaram"
