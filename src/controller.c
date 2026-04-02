#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "protocolo.h"

int main(int argc, char *argv[]) {
    if (argc < 2) { return 1; }

    mkfifo("tmp/controller_pipe", 0666);
    int fd_c = open("tmp/controller_pipe", O_RDONLY);

    Msg m;
    while (read(fd_c, &m, sizeof(Msg)) > 0) {
        if (m.type == 1) {
            printf("[controller] User %d (PID %d) quer: %s\n", m.user_id, m.runner_pid, m.command);

            // 1. Abrir o FIFO privado do Runner para responder
            char fifo_res[64];
            snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);
            
            int fd_res = open(fifo_res, O_WRONLY);
            if (fd_res != -1) {
                int ok = 1;
                write(fd_res, &ok, sizeof(int)); // Manda o "GO"
                close(fd_res);
                printf("[controller] Autorizacao enviada ao PID %d\n", m.runner_pid);
            }
        }
    }
    return 0;
}