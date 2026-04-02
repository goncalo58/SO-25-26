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
    
    // Fila de espera simples
    Msg queue[MAX_QUEUE];
    int queue_count = 0;

    mkfifo("tmp/controller_pipe", 0666);
    int fd_c = open("tmp/controller_pipe", O_RDONLY);

    Msg m;
    char buffer[512];

    while (read(fd_c, &m, sizeof(Msg)) > 0) {
        if (m.type == 1) { // NOVO PEDIDO
            if (running_now < max_parallel) {
                // HÁ VAGA: Autoriza logo
                char fifo_res[64];
                snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);
                int fd_res = open(fifo_res, O_WRONLY);
                if (fd_res != -1) {
                    int ok = 1;
                    write(fd_res, &ok, sizeof(int));
                    close(fd_res);
                    running_now++;
                    
                    int n = snprintf(buffer, sizeof(buffer), "[controller] %d a correr. Autorizei PID %d\n", running_now, m.runner_pid);
                    write(1, buffer, n);
                }
            } else {
                // CHEIO: Vai para a fila
                if (queue_count < MAX_QUEUE) {
                    queue[queue_count++] = m;
                    int n = snprintf(buffer, sizeof(buffer), "[controller] Cheio! PID %d em espera (Posicao %d)\n", m.runner_pid, queue_count);
                    write(1, buffer, n);
                }
            }
        } 
        else if (m.type == 4) { // COMANDO TERMINOU
            running_now--;
            int n = snprintf(buffer, sizeof(buffer), "[controller] PID %d terminou. Vagas: %d\n", m.runner_pid, max_parallel - running_now);
            write(1, buffer, n);

            // Se houver alguém na fila, autoriza o próximo!
            if (queue_count > 0) {
                Msg next = queue[0];
                // Chegar a fila para a frente (shift)
                for(int i=0; i < queue_count-1; i++) queue[i] = queue[i+1];
                queue_count--;

                char fifo_res[64];
                snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", next.runner_pid);
                int fd_res = open(fifo_res, O_WRONLY);
                if (fd_res != -1) {
                    int ok = 1;
                    write(fd_res, &ok, sizeof(int));
                    close(fd_res);
                    running_now++;
                    int n2 = snprintf(buffer, sizeof(buffer), "[controller] Fila andou! Autorizei PID %d\n", next.runner_pid);
                    write(1, buffer, n2);
                }
            }
        }
    }
    return 0;
}