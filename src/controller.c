#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include "protocolo.h"

#define MAX_QUEUE 100

int main(int argc, char *argv[]) {

    if (argc < 2) {
        char *usage = "Uso: ./controller <max_parallel>\n";
        write(2, usage, 31);
        return 1;
    }

    int max_parallel = atoi(argv[1]);
    int running_now = 0;

    // Fila de espera
    Msg queue[MAX_QUEUE];
    int queue_count = 0;

    // Lista de processos em execução
    Msg running[MAX_QUEUE];
    int running_count = 0;

    int shutdown_requested = 0;

    mkfifo("tmp/controller_pipe", 0666);
    int fd_c = open("tmp/controller_pipe", O_RDWR);

    Msg m;
    char buffer[512];

    int running_flag = 1;

    while (running_flag && read(fd_c, &m, sizeof(Msg)) > 0) {

        // NOVO PEDIDO (-e)
        if (m.type == 1) {

            // Guardar tempo de chegada
            gettimeofday(&m.start_time, NULL);

            if (running_now < max_parallel) {

                char fifo_res[64];
                snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);

                int fd_res = open(fifo_res, O_WRONLY);
                if (fd_res != -1) {
                    int ok = 1;
                    write(fd_res, &ok, sizeof(int));
                    close(fd_res);

                    // Adicionar à lista de execução
                    running[running_count++] = m;

                    running_now++;

                    int n = snprintf(buffer, sizeof(buffer),
                        "[controller] PID %d autorizado\n", m.runner_pid);
                    write(1, buffer, n);
                }

            } else {

                // Vai para fila
                if (queue_count < MAX_QUEUE) {
                    queue[queue_count++] = m;

                    int n = snprintf(buffer, sizeof(buffer),
                        "[controller] PID %d em fila (%d)\n",
                        m.runner_pid, queue_count);
                    write(1, buffer, n);
                }
            }
        }

        // TERMINOU (-e fim)
        else if (m.type == 4) {

            int idx = -1;

            // Procurar processo na lista de execução
            for (int i = 0; i < running_count; i++) {
                if (running[i].runner_pid == m.runner_pid) {
                    idx = i;
                    i = running_count; // sair do loop
                }
            }

            if (idx != -1) {

                // Tempo atual
                struct timeval end;
                gettimeofday(&end, NULL);

                // Calcular duração
                double duration = (end.tv_sec - running[idx].start_time.tv_sec) +
                                  (end.tv_usec - running[idx].start_time.tv_usec) / 1000000.0;

                // Guardar no ficheiro
                int fd_log = open("log.txt", O_CREAT | O_WRONLY | O_APPEND, 0666);

                if (fd_log != -1) {
                    char linha[256];

                    int nlog = snprintf(linha, sizeof(linha),
                        "user %d | pid %d | tempo %.2f s\n",
                        running[idx].user_id,
                        running[idx].runner_pid,
                        duration);

                    write(fd_log, linha, nlog);
                    close(fd_log);
                }

                // Remover da lista de execução
                for (int i = idx; i < running_count - 1; i++) {
                    running[i] = running[i + 1];
                }
                running_count--;

                running_now--;
            }

            int n = snprintf(buffer, sizeof(buffer),
                "[controller] PID %d terminou\n", m.runner_pid);
            write(1, buffer, n);

            // Autorizar próximo da fila
            if (queue_count > 0) {

                Msg next = queue[0];

                for (int i = 0; i < queue_count - 1; i++) {
                    queue[i] = queue[i + 1];
                }
                queue_count--;

                char fifo_res[64];
                snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", next.runner_pid);

                int fd_res = open(fifo_res, O_WRONLY);
                if (fd_res != -1) {
                    int ok = 1;
                    write(fd_res, &ok, sizeof(int));
                    close(fd_res);

                    // Adicionar à lista de execução
                    running[running_count++] = next;

                    running_now++;

                    int n2 = snprintf(buffer, sizeof(buffer),
                        "[controller] fila -> PID %d autorizado\n",
                        next.runner_pid);
                    write(1, buffer, n2);
                }
            }
        }

        // CONSULTA (-c)
        else if (m.type == 2) {

            char fifo_res[64];
            snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);

            int fd_res = open(fifo_res, O_WRONLY);
            if (fd_res != -1) {

                char out[1024];
                int len = 0;

                len += snprintf(out + len, sizeof(out) - len,
                    "=== ESTADO ===\n");

                len += snprintf(out + len, sizeof(out) - len,
                    "A correr: %d / %d\n", running_now, max_parallel);

                len += snprintf(out + len, sizeof(out) - len,
                    "Em fila: %d\n", queue_count);

                for (int i = 0; i < queue_count; i++) {
                    len += snprintf(out + len, sizeof(out) - len,
                        "PID fila: %d\n", queue[i].runner_pid);
                }

                write(fd_res, out, strlen(out));
                close(fd_res);
            }
        }

        // SHUTDOWN (-s)
        else if (m.type == 5) {

            write(1, "[controller] shutdown pedido\n", 30);
            shutdown_requested = 1;
        }

        // Só termina quando tudo acabar
        if (shutdown_requested && running_now == 0 && queue_count == 0) {
            write(1, "[controller] shutdown concluido\n", 32);
            running_flag = 0;
        }
    }

    close(fd_c);
    unlink("tmp/controller_pipe");

    return 0;
}