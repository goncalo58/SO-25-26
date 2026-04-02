#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
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

    Msg queue[MAX_QUEUE];
    int queue_count = 0;

    mkfifo("tmp/controller_pipe", 0666);

    int fd_c = open("tmp/controller_pipe", O_RDWR);

    Msg m;
    char buffer[512];

    int running = 1;

    while (running && read(fd_c, &m, sizeof(Msg)) > 0) {

        // -e: novo pedido
        if (m.type == 1) {

            if (running_now < max_parallel) {

                char fifo_res[64];
                snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);

                int fd_res = open(fifo_res, O_WRONLY);
                if (fd_res != -1) {
                    int ok = 1;
                    write(fd_res, &ok, sizeof(int));
                    close(fd_res);

                    running_now++;

                    int n = snprintf(buffer, sizeof(buffer),
                        "[controller] PID %d a correr\n", m.runner_pid);
                    write(1, buffer, n);
                }

            } else {

                if (queue_count < MAX_QUEUE) {
                    queue[queue_count++] = m;

                    int n = snprintf(buffer, sizeof(buffer),
                        "[controller] cheio -> PID %d em fila (%d)\n",
                        m.runner_pid, queue_count);
                    write(1, buffer, n);
                }
            }
        }

        // -e: terminou
        else if (m.type == 4) {

            running_now--;

            int n = snprintf(buffer, sizeof(buffer),
                "[controller] PID %d terminou | vagas: %d\n",
                m.runner_pid, max_parallel - running_now);
            write(1, buffer, n);

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

                    running_now++;

                    int n2 = snprintf(buffer, sizeof(buffer),
                        "[controller] fila avançou -> PID %d autorizado\n",
                        next.runner_pid);
                    write(1, buffer, n2);
                }
            }
        }

        // -c: consulta
        else if (m.type == 2) {

            char fifo_res[64];
            snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);

            int fd_res = open(fifo_res, O_WRONLY);
            if (fd_res != -1) {

                char out[1024];
                int len = 0;

                len += snprintf(out + len, sizeof(out) - len,
                    "=== ESTADO CONTROLLER ===\n");

                len += snprintf(out + len, sizeof(out) - len,
                    "A correr: %d / %d\n", running_now, max_parallel);

                len += snprintf(out + len, sizeof(out) - len,
                    "Em fila: %d\n", queue_count);

                for (int i = 0; i < queue_count; i++) {
                    len += snprintf(out + len, sizeof(out) - len,
                        "PID em fila: %d\n", queue[i].runner_pid);
                }

                write(fd_res, out, strlen(out));
                close(fd_res);
            }
        }


        // -s: shutdown
        else if (m.type == 5) {

            write(1, "[controller] shutdown recebido\n", 32);
            running = 0;
        }
    }

    close(fd_c);
    unlink("tmp/controller_pipe");

    return 0;
}