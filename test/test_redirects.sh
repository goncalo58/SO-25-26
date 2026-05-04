#!/bin/bash
# test_redirects.sh — configurado para correr da raiz

BIN=./bin
TMP=./tmp
mkdir -p $TMP

echo "=== Teste de redirecionamentos e pipes ==="

# Inicia controller com paralelismo 2
$BIN/controller 2 &
CTRL_PID=$!
sleep 0.3

# Teste 1: redirecionamento de saída >
echo "[teste 1] echo para ficheiro (>)"
$BIN/runner -e 1 "echo ola mundo > tmp/out1.txt"
sleep 0.5
if grep -q "ola mundo" tmp/out1.txt; then
    echo "  PASSOU: ficheiro contém 'ola mundo'"
else
    echo "  FALHOU"
fi

# Teste 2: redirecionamento de entrada <
echo "[teste 2] wc -l com redirecionamento (<)"
echo -e "linha1\nlinha2\nlinha3" > tmp/input.txt
$BIN/runner -e 1 "wc -l < tmp/input.txt > tmp/out2.txt"
sleep 0.5
echo "  Resultado: $(cat tmp/out2.txt)"

# Teste 3: pipe |
echo "[teste 3] pipeline: ls | wc -l"
$BIN/runner -e 1 "ls bin | wc -l > tmp/out3.txt"
sleep 0.5
echo "  Resultado: $(cat tmp/out3.txt) ficheiros em bin/"

# Teste 4: pipeline mais longa
echo "[teste 4] pipeline longa: cat | tr | sort | uniq"
echo -e "banana\nmaçã\nbanana\nlaranja\nmaçã\nmaçã" > tmp/frutas.txt
$BIN/runner -e 1 "cat tmp/frutas.txt | sort | uniq > tmp/out4.txt"
sleep 0.5
echo "  Frutas únicas:"
cat tmp/out4.txt | sed 's/^/    /'

# Teste 5: stderr redireccionado 2>
echo "[teste 5] stderr com 2>"
$BIN/runner -e 1 "ls /caminho_inexistente 2> tmp/out5.txt"
sleep 0.5
if [ -s tmp/out5.txt ]; then
    echo "  PASSOU: stderr capturado em out5.txt: $(cat tmp/out5.txt)"
else
    echo "  FALHOU: ficheiro vazio"
fi

# Encerra controller
$BIN/runner -s
wait $CTRL_PID 2>/dev/null
echo ""
echo "=== Testes de redirecionamento concluídos ==="