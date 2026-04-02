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
    if (argc < 4) {
        char *usage = "Uso: ./runner -e <user_id> <comando>\n";
        write(2, usage, 37);
        return 1;
    }

    Msg m;
    m.runner_pid = getpid();
    m.type = 1; 
    m.user_id = atoi(argv[2]);
    // Copiar o comando garantindo que termina em \0
    memset(m.command, 0, sizeof(m.command));
    strncpy(m.command, argv[3], sizeof(m.command) - 1);

    char out_buf[512];
    char meu_fifo[64];
    snprintf(meu_fifo, sizeof(meu_fifo), "tmp/fifo_%d", m.runner_pid);
    mkfifo(meu_fifo, 0666);

    // 1. Enviar Pedido ao Controller
    int fd_c = open("tmp/controller_pipe", O_WRONLY);
    if (fd_c == -1) { write(2, "Erro: Controller nao esta ativo\n", 32); return 1; }
    write(fd_c, &m, sizeof(Msg));
    close(fd_c);

    int n1 = snprintf(out_buf, sizeof(out_buf), "[runner] command %d submitted\n", m.runner_pid);
    write(1, out_buf, n1);

    // 2. Esperar autorização (Bloqueia no read)
    int fd_r = open(meu_fifo, O_RDONLY);
    int ok;
    read(fd_r, &ok, sizeof(int));
    close(fd_r);

    int n2 = snprintf(out_buf, sizeof(out_buf), "[runner] executing command %d...\n", m.runner_pid);
    write(1, out_buf, n2);

    // 3. Execução REAL (Onde o sleep 5 acontece)
    pid_t pid = fork();
    if (pid == 0) {
        char *args[16];
        int i = 0;
        
        // Parsing robusto: corta por espaços, tabs ou novas linhas
        char *token = strtok(m.command, " \t\n");
        while (token != NULL && i < 15) {
            args[i++] = token;
            token = strtok(NULL, " \t\n");
        }
        args[i] = NULL;

        if (args[0] != NULL) {
            execvp(args[0], args);
            // Se o execvp falhar, ele escreve o erro
            perror("execvp"); 
        }
        _exit(1); 
    }
    
    // O PAI ESPERA O FILHO (Aqui é que o tempo passa)
    waitpid(pid, NULL, 0);

    // 4. Notificar Término ao Controller
    m.type = 4;
    fd_c = open("tmp/controller_pipe", O_WRONLY);
    if (fd_c != -1) {
        write(fd_c, &m, sizeof(Msg));
        close(fd_c);
    }

    int n3 = snprintf(out_buf, sizeof(out_buf), "[runner] command %d finished\n", m.runner_pid);
    write(1, out_buf, n3);

    unlink(meu_fifo);
    return 0;
}