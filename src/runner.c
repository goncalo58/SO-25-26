#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "protocolo.h"

int main(int argc, char *argv[]) {

    if (argc < 2) {
        write(2, "Uso: ./runner -e <user_id> <comando>\n", 37);
        return 1;
    }


    // MODO -e (EXECUTAR)
    if (strcmp(argv[1], "-e") == 0) {

        if (argc < 4) {
            write(2, "Uso: ./runner -e <user_id> <comando>\n", 37);
            return 1;
        }

        Msg m;
        m.runner_pid = getpid();
        m.type = 1;
        m.user_id = atoi(argv[2]);

        memset(m.command, 0, sizeof(m.command));

        for (int i = 3; i < argc; i++) {
            strcat(m.command, argv[i]);
            if (i < argc - 1) strcat(m.command, " ");
        }

        char meu_fifo[64];
        char out_buf[512];

        snprintf(meu_fifo, sizeof(meu_fifo), "tmp/fifo_%d", m.runner_pid);
        mkfifo(meu_fifo, 0666);

        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c == -1) {
            write(2, "Erro: Controller nao esta ativo\n", 32);
            return 1;
        }

        write(fd_c, &m, sizeof(Msg));
        close(fd_c);

        int n1 = snprintf(out_buf, sizeof(out_buf),
            "[runner] command %d submitted\n", m.runner_pid);
        write(1, out_buf, n1);

        int fd_r = open(meu_fifo, O_RDONLY);
        int ok;
        read(fd_r, &ok, sizeof(int));
        close(fd_r);

        int n2 = snprintf(out_buf, sizeof(out_buf),
            "[runner] executing command %d...\n", m.runner_pid);
        write(1, out_buf, n2);

        pid_t pid = fork();

        if (pid == 0) {
            char *args[16];
            int i = 0;

            char *token = strtok(m.command, " \t\n");
            while (token != NULL && i < 15) {
                args[i++] = token;
                token = strtok(NULL, " \t\n");
            }
            args[i] = NULL;

            if (args[0] != NULL) execvp(args[0], args);
            _exit(1);
        }

        waitpid(pid, NULL, 0);

        m.type = 4;

        fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c != -1) {
            write(fd_c, &m, sizeof(Msg));
            close(fd_c);
        }

        int n3 = snprintf(out_buf, sizeof(out_buf),
            "[runner] command %d finished\n", m.runner_pid);
        write(1, out_buf, n3);

        unlink(meu_fifo);
    }

    // MODO -c (CONSULTAR)
    else if (strcmp(argv[1], "-c") == 0) {

        Msg m;
        m.runner_pid = getpid();
        m.type = 2; // consulta
        m.user_id = (argc > 2) ? atoi(argv[2]) : 0;

        char meu_fifo[64];
        char buffer[1024];

        snprintf(meu_fifo, sizeof(meu_fifo), "tmp/fifo_%d", m.runner_pid);
        mkfifo(meu_fifo, 0666);

        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c == -1) {
            write(2, "Erro: Controller nao esta ativo\n", 32);
            unlink(meu_fifo);
            return 1;
        }

        write(fd_c, &m, sizeof(Msg));
        close(fd_c);

        int fd_r = open(meu_fifo, O_RDONLY);
        if (fd_r == -1) {
            write(2, "Erro ao abrir FIFO de resposta\n", 32);
            unlink(meu_fifo);
            return 1;
        }

        int n = read(fd_r, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            write(1, buffer, strlen(buffer));
        }

        close(fd_r);
        unlink(meu_fifo);
    }

    // MODO -s (SHUTDOWN)
    else if (strcmp(argv[1], "-s") == 0) {

        Msg m;
        m.runner_pid = getpid();
        m.type = 5; // shutdown
        m.user_id = 0;

        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c == -1) {
            write(2, "Erro: Controller nao esta ativo\n", 32);
            return 1;
        }

        write(fd_c, &m, sizeof(Msg));
        close(fd_c);

        char out_buf[128];
        int n = snprintf(out_buf, sizeof(out_buf),
            "[runner] shutdown signal sent to controller\n");
        write(1, out_buf, n);
    }

    // OUTROS MODOS
    else {
        write(2, "Modo nao suportado\n", 20);
    }

    return 0;
}


