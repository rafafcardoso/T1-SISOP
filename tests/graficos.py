#!/usr/bin/env python3
"""Gera os graficos de tempo, aceleracao e eficiencia a partir de results/medicoes.csv.

Ferramenta AUXILIAR de analise (a solucao em si e toda em C).
Requer matplotlib:  pip install matplotlib
Uso: python3 tests/graficos.py
"""
import csv
import os
import statistics as st
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results")
tempos = {}  # (versao, p) -> [tempo_ms, ...]
with open(os.path.join(RES, "medicoes.csv"), newline="") as f:
    for r in csv.DictReader(f):
        tempos.setdefault((r["versao"], int(r["trabalhadores"])), []).append(float(r["tempo_ms"]))

ts = st.median(tempos[("sequencial", 1)])
ps = sorted(p for (v, p) in tempos if v == "paralela")
med = [st.median(tempos[("paralela", p)]) for p in ps]
mn = [min(tempos[("paralela", p)]) for p in ps]
mx = [max(tempos[("paralela", p)]) for p in ps]

# Figura 1 - tempo (barras = mediana; barras de erro = minimo e maximo)
rot = ["seq"] + [str(p) for p in ps]
valores = [ts] + med
smn = min(tempos[("sequencial", 1)]); smx = max(tempos[("sequencial", 1)])
erro_inf = [ts - smn] + [m - a for m, a in zip(med, mn)]
erro_sup = [smx - ts] + [b - m for m, b in zip(med, mx)]
plt.figure(figsize=(6, 4))
plt.bar(rot, valores, yerr=[erro_inf, erro_sup], capsize=4, color="#4C78A8")
plt.xlabel("Versao / numero de threads (p)"); plt.ylabel("Tempo (ms)")
plt.title("Tempo de execucao (mediana, barras = min/max)")
plt.tight_layout(); plt.savefig(os.path.join(RES, "grafico-tempo.png"), dpi=150); plt.close()

# Figura 2 - aceleracao
S = [ts / m for m in med]
plt.figure(figsize=(6, 4))
plt.plot(ps, S, "o-", label="Observada")
plt.plot(ps, ps, "--", color="gray", label="Ideal S(p)=p")
plt.axhline(1.0, color="red", lw=0.8, ls=":", label="S=1 (igual ao sequencial)")
plt.xlabel("Threads (p)"); plt.ylabel("Aceleracao S(p)"); plt.title("Aceleracao")
plt.legend(); plt.grid(alpha=0.3)
plt.tight_layout(); plt.savefig(os.path.join(RES, "grafico-aceleracao.png"), dpi=150); plt.close()

# Figura 3 - eficiencia
E = [s / p for s, p in zip(S, ps)]
plt.figure(figsize=(6, 4))
plt.plot(ps, E, "s-", color="#E45756")
plt.ylim(0, 1.1)
plt.xlabel("Threads (p)"); plt.ylabel("Eficiencia E(p)=S(p)/p"); plt.title("Eficiencia paralela")
plt.grid(alpha=0.3)
plt.tight_layout(); plt.savefig(os.path.join(RES, "grafico-eficiencia.png"), dpi=150); plt.close()
print("graficos gerados em", os.path.normpath(RES))
