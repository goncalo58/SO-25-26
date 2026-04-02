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
        write(2, "Uso: ./runner -e <user_id> <comando>\n", 37);
        return 1;
    }

    Msg m;
    m.runner_pid = getpid();
    m.type = 1; // Executar
    m.user_id = atoi(argv[2]);
    strncpy(m.command, argv[3], sizeof(m.command) - 1);

    // 1. Criar FIFO privado para receber resposta
    char meu_fifo[64];
    snprintf(meu_fifo, sizeof(meu_fifo), "tmp/fifo_%d", m.runner_pid);
    mkfifo(meu_fifo, 0666);

    // 2. Enviar pedido ao Controller
    int fd_c = open("tmp/controller_pipe", O_WRONLY);
    if (fd_c == -1) { perror("Controller off"); return 1; }
    write(fd_c, &m, sizeof(Msg));
    close(fd_c);
    write(1, "[runner] command submitted\n", 27);

    // 3. Esperar pelo "OK" do Controller
    int fd_r = open(meu_fifo, O_RDONLY);
    int autorizacao;
    read(fd_r, &autorizacao, sizeof(int)); // Bloqueia aqui!
    close(fd_r);

    write(1, "[runner] executing command...\n", 30);

    // 4. EXECUÇÃO REAL (Fork + Exec)
    if (fork() == 0) {
        // Divide o comando por espaços (simplificado para o teste)
        char *args[] = {m.command, NULL}; 
        execvp(args[0], args);
        perror("Erro no exec");
        exit(1);
    }
    wait(NULL); // Espera o comando terminar

    write(1, "[runner] command finished\n", 26);
    unlink(meu_fifo); // Limpa o seu próprio FIFO
    return 0;
}