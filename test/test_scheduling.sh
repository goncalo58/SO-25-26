#!/bin/bash
# test_scheduling.sh — configurado para correr da raiz

BIN=./bin
LOG=./log.txt
TMP=./tmp

# Garante que as pastas existem e limpa o log anterior
mkdir -p $TMP
rm -f $LOG

echo "=== TESTE DE ESCALONAMENTO JUSTO (Round-Robin) ==="

# 1. Iniciar o Controller com apenas 1 vaga (para vermos a alternância)
$BIN/controller 1 &
CTRL_PID=$!
sleep 0.5

echo "[Passo 1] User 10 enche a fila com 3 comandos longos..."
$BIN/runner -e 10 "sleep 5" &
$BIN/runner -e 10 "sleep 5" &
$BIN/runner -e 10 "sleep 5" &

sleep 1 # Tempo para entrarem na fila

echo "[Passo 2] User 20 chega com um comando rápido..."
$BIN/runner -e 20 "echo 'User 20: Passei a frente devido ao Round-Robin!'" &

sleep 2
echo -e "\n--- Verificação do Estado (O User 20 deve estar bem posicionado) ---"
$BIN/runner -c

echo -e "\n[Passo 3] Aguardando conclusão dos processos..."
sleep 12

echo -e "\n=== Análise do Log (A ordem deve mostrar a justiça do escalonador) ==="
# Verifica se o log existe antes de tentar ler
if [ -f "$LOG" ]; then
    cat "$LOG"
else
    echo "Erro: Ficheiro log.txt não encontrado em $LOG"
fi

# Shutdown elegante
$BIN/runner -s
wait $CTRL_PID 2>/dev/null

echo -e "\n=== Teste de Escalonamento Concluído ==="