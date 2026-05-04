#!/bin/bash
# test_scalability.sh — Teste de escalabilidade e paralelismo
# Mede o wall-clock para 10 tarefas de 1s com P ∈ {1, 2, 5, 10}

BIN=./bin
mkdir -p tmp results

rm -f log.txt
echo "P  | Tempo Total (ms)"
echo "---+-------------------"

for P in 1 2 5 10; do

    # 1. Iniciar o controller com P vagas
    $BIN/controller $P &
    CTRL_PID=$!
    sleep 1   # aguarda o controller criar o FIFO

    START=$(date +%s%N)

    # 2. Lançar 10 runners em background, guardando PIDs explicitamente
    PIDS=()
    for i in {1..10}; do
        $BIN/runner -e 1 "sleep 1" &
        PIDS+=($!)
    done

    # 3. Espera seletiva e fiável — aguarda cada PID individualmente
    #    (jobs -p não é fiável para runners já terminados)
    for pid in "${PIDS[@]}"; do
        wait "$pid" 2>/dev/null
    done

    END=$(date +%s%N)
    DIFF=$(( (END - START) / 1000000 ))

    printf " %-2s | %d ms\n" "$P" "$DIFF"
    echo "parallel=$P wall_ms=$DIFF" >> results/scalability.txt

    # 4. Shutdown e aguarda controller terminar
    $BIN/runner -s
    wait $CTRL_PID 2>/dev/null
    sleep 0.5   # pequena pausa antes do próximo cenário

done
