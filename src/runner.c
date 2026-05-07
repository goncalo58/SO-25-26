#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "protocolo.h"


#define MAX_ARGS   64
#define MAX_CMDS   16 

typedef struct {
    char *args[MAX_ARGS];
    int   nargs;
    char *redir_in;    /* Armazena o ficheiro após o símbolo '<' */
    char *redir_out;   /* Armazena o ficheiro após o símbolo '>' */
    char *redir_err;   /* Armazena o ficheiro após o símbolo '2>' */
} Cmd;

/* * PARSE_PIPELINE: Transforma a string do utilizador em estruturas Cmd.
 * 1. Divide a linha por '|' para identificar os estágios da pipeline. (ver quais comandos fazem parte dos argumaentos que o utilziador escreveu)
 * 2. Para cada estágio, identifica os símbolos de redirecionamento.
 */
static int parse_pipeline(char *line, Cmd cmds[MAX_CMDS]) {
    int ncmds = 0;
    char *saveptr1; //guarda o endereço de memoria de onde a funçao parou de ler a string original
    char *seg = strtok_r(line, "|", &saveptr1); // Divide pelos Pipes, devolvle o ponteiro para o 
    //proximo pedaço da string

    while (seg && ncmds < MAX_CMDS) {
    /*e um ciclo que enquaanto o numero de comandos e o
     a divisao dos pipes for menor que o numero max de comandos 
     executa a procura certo
    */
        Cmd *c = &cmds[ncmds++]; //guarda os dados do comando atual
        c->nargs = 0; c->redir_in = NULL; c->redir_out = NULL; c->redir_err = NULL;

        char *saveptr2;
        char *tok = strtok_r(seg, " \t\n", &saveptr2);
        while (tok) {
            // Verifica se o ponteiro atual é um operador de redirecionamento
            if (strcmp(tok, ">") == 0) {
                c->redir_out = strtok_r(NULL, " \t\n", &saveptr2);
            } else if (strcmp(tok, "2>") == 0) {
                c->redir_err = strtok_r(NULL, " \t\n", &saveptr2);
            } else if (strcmp(tok, "<") == 0) {
                c->redir_in = strtok_r(NULL, " \t\n", &saveptr2);
            } else {
                // Se for um comando ou argumento (ex: "ls" ou "-l"), guarda no array
                if (c->nargs < MAX_ARGS - 1)
                    c->args[c->nargs++] = tok;
            }
            tok = strtok_r(NULL, " \t\n", &saveptr2);
        }
        c->args[c->nargs] = NULL; // Termina a lista de argumentos com NULL para o execvp
        seg = strtok_r(NULL, "|", &saveptr1);
    }
    return ncmds;
}

/* * EXEC_PIPELINE: Cria os processos e faz a "canalização" (redirecionamentos e pipes).
 */
static pid_t exec_pipeline(char *command) {
    Cmd cmds[MAX_CMDS];
    char buf[300];
    strncpy(buf, command, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';

    int ncmds = parse_pipeline(buf, cmds);
    if (ncmds == 0) return -1;

    int prev_fd = -1; // Guardará o descritor de leitura do pipe anterior
    pid_t last_pid = -1;

    for (int i = 0; i < ncmds; i++) {
        int pipefd[2] = {-1, -1};
        int is_last = (i == ncmds - 1);

        /* Se não for o último comando, cria um PIPE para passar dados ao seguinte */
        if (!is_last) {
            if (pipe(pipefd) == -1) { return -1; }
        }

        pid_t pid = fork();
        if (pid == 0) { /* filho*/

            /* redirecionameto de entrada(Stdin) */
            if (prev_fd != -1) { 
                // Recebe dados do pipe anterior
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            } else if (cmds[i].redir_in) {
                // Recebe dados de um ficheiro (<)
                int fd = open(cmds[i].redir_in, O_RDONLY);
                if (fd != -1) { dup2(fd, STDIN_FILENO); close(fd); }
            }

            /* redirencionameto de saída(Stdout) */
            if (!is_last) {
                // Envia dados para o pipe seguinte (|)
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]); close(pipefd[1]);
            } else if (cmds[i].redir_out) {
                // Envia dados para um ficheiro (>)
                int fd = open(cmds[i].redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd != -1) { dup2(fd, STDOUT_FILENO); close(fd); }
            }

            /* REDIRECIONAMENTO DE ERROS (Stderr) */
            if (cmds[i].redir_err) {
                int fd = open(cmds[i].redir_err, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd != -1) { dup2(fd, STDERR_FILENO); close(fd); }
            }

            // Transforma o processo atual no comando real (ex: ls, grep)
            if (cmds[i].args[0]) execvp(cmds[i].args[0], cmds[i].args);
            _exit(1); 
        }

        /* pai (runner) */
        if (prev_fd != -1) close(prev_fd); // Fecha leitura do pipe que já foi usada
        if (!is_last) {
            close(pipefd[1]); // Pai fecha escrita; só o filho escreve
            prev_fd = pipefd[0]; // Guarda a leitura do pipe para o próximo filho
        }
        last_pid = pid;
    }

    /* O pai bloqueia aqui até que todos os filhos da pipeline terminem */
    for (int i = 0; i < ncmds; i++) wait(NULL);
    return last_pid;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;

    /*modo execução (-e)*/
    if (strcmp(argv[1], "-e") == 0) {
        Msg m;
        m.runner_pid = getpid(); // BI do Runner
        m.type = 1;              // Tipo 1: Pedido de execução
        m.user_id = atoi(argv[2]);

        // reconstrói o comando completo a partir dos argumentos do terminal
        memset(m.command, 0, sizeof(m.command));
        for (int i = 3; i < argc; i++) {
            strcat(m.command, argv[i]);
            if (i < argc - 1) strcat(m.command, " ");
        }

        /* Criar FIFO privado para receber a autorização do Controller */
        char meu_fifo[64];
        snprintf(meu_fifo, sizeof(meu_fifo), "tmp/fifo_%d", m.runner_pid);
        mkfifo(meu_fifo, 0666);

        /* Enviar pedido para o Controller */
        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c != -1) {
            write(fd_c, &m, sizeof(Msg));
            close(fd_c);
        }

        /*  O Runner fica parado aqui até o Controller responder */
        int fd_r = open(meu_fifo, O_RDONLY);
        int ok; read(fd_r, &ok, sizeof(int));
        close(fd_r);

        /*  O Controller autorizou o slot de paralelismo */
        exec_pipeline(m.command);

        /*  Avisa que terminou para libertar a vaga (Tipo 4) */
        m.type = 4;
        fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c != -1) {
            write(fd_c, &m, sizeof(Msg));
            close(fd_c);
        }

        unlink(meu_fifo); // Limpeza do FIFO privado
    }

    /* modo consukta (.c) */
    else if (strcmp(argv[1], "-c") == 0) {
        Msg m;
        m.runner_pid = getpid(); m.type = 2; // Tipo 2: Status
        m.user_id = (argc > 2) ? atoi(argv[2]) : 0;

        char meu_fifo[64];
        snprintf(meu_fifo, sizeof(meu_fifo), "tmp/fifo_%d", m.runner_pid);
        mkfifo(meu_fifo, 0666);

        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        write(fd_c, &m, sizeof(Msg));
        close(fd_c);

        /* Llẽ o relatorio enviado pelo controller*/
        int fd_r = open(meu_fifo, O_RDONLY);
        char buffer[2048];
        int n = read(fd_r, buffer, sizeof(buffer) - 1);
        if (n > 0) { buffer[n] = '\0'; write(1, buffer, n); }
        close(fd_r);
        unlink(meu_fifo);
    }

    /* modo shurdowm (-s)*/
    else if (strcmp(argv[1], "-s") == 0) {
        Msg m; m.type = 5; // Tipo 5: Desligar servidor
        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        write(fd_c, &m, sizeof(Msg));
        close(fd_c);
    }

    return 0;
}