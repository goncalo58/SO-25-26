#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "protocolo.h"

/* ------------------------------------------------------------------ *
 *  Executa uma pipeline completa suportando >, 2>, < e |             *
 *  Devolve o PID do último processo da cadeia (ou -1 em erro).      *
 * ------------------------------------------------------------------ */

#define MAX_ARGS   64
#define MAX_CMDS   16   /* máximo de estágios na pipeline */

typedef struct {
    char *args[MAX_ARGS];
    int   nargs;
    char *redir_in;    /* < ficheiro   */
    char *redir_out;   /* > ficheiro   */
    char *redir_err;   /* 2> ficheiro  */
} Cmd;

/* Divide a linha de comandos em estágios separados por '|' e
   identifica redirecionamentos dentro de cada estágio.            */
static int parse_pipeline(char *line, Cmd cmds[MAX_CMDS]) {
    int ncmds = 0;
    char *saveptr1;
    char *seg = strtok_r(line, "|", &saveptr1);

    while (seg && ncmds < MAX_CMDS) {
        Cmd *c = &cmds[ncmds++];
        c->nargs      = 0;
        c->redir_in   = NULL;
        c->redir_out  = NULL;
        c->redir_err  = NULL;

        char *saveptr2;
        char *tok = strtok_r(seg, " \t\n", &saveptr2);
        while (tok) {
            if (strcmp(tok, ">") == 0) {
                c->redir_out = strtok_r(NULL, " \t\n", &saveptr2);
            } else if (strcmp(tok, "2>") == 0) {
                c->redir_err = strtok_r(NULL, " \t\n", &saveptr2);
            } else if (strcmp(tok, "<") == 0) {
                c->redir_in = strtok_r(NULL, " \t\n", &saveptr2);
            } else {
                if (c->nargs < MAX_ARGS - 1)
                    c->args[c->nargs++] = tok;
            }
            tok = strtok_r(NULL, " \t\n", &saveptr2);
        }
        c->args[c->nargs] = NULL;
        seg = strtok_r(NULL, "|", &saveptr1);
    }
    return ncmds;
}

/* Lança a pipeline e devolve o PID do último filho */
static pid_t exec_pipeline(char *command) {
    Cmd cmds[MAX_CMDS];
    char buf[300];
    strncpy(buf, command, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';

    int ncmds = parse_pipeline(buf, cmds);
    if (ncmds == 0) return -1;

    int prev_fd = -1;       /* fd de leitura do pipe anterior */
    pid_t last_pid = -1;

    for (int i = 0; i < ncmds; i++) {
        int pipefd[2] = {-1, -1};
        int is_last = (i == ncmds - 1);

        /* Cria pipe para passar stdout ao próximo estágio */
        if (!is_last) {
            if (pipe(pipefd) == -1) {
                write(2, "Erro ao criar pipe\n", 19);
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            write(2, "Erro no fork\n", 13);
            return -1;
        }

        if (pid == 0) { /* filho */

            /* stdin: pipe anterior ou redirecionamento < */
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            } else if (cmds[i].redir_in) {
                int fd = open(cmds[i].redir_in, O_RDONLY);
                if (fd == -1) { _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            /* stdout: pipe seguinte ou redirecionamento > */
            if (!is_last) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            } else if (cmds[i].redir_out) {
                int fd = open(cmds[i].redir_out,
                              O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) { _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            /* stderr: redirecionamento 2> */
            if (cmds[i].redir_err) {
                int fd = open(cmds[i].redir_err,
                              O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) { _exit(1); }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            if (cmds[i].args[0])
                execvp(cmds[i].args[0], cmds[i].args);

            _exit(1);
        }

        /* pai: fecha extremidades usadas pelos filhos */
        if (prev_fd != -1) close(prev_fd);
        if (!is_last) {
            close(pipefd[1]);
            prev_fd = pipefd[0];
        }

        last_pid = pid;
    }

    /* Espera todos os filhos */
    for (int i = 0; i < ncmds; i++) wait(NULL);

    return last_pid;
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {

    if (argc < 2) {
        write(2, "Uso: ./runner -e <user_id> <comando> | -c [user_id] | -s\n", 57);
        return 1;
    }

    /* ---- MODO -e (EXECUTAR) ---- */
    if (strcmp(argv[1], "-e") == 0) {

        if (argc < 4) {
            write(2, "Uso: ./runner -e <user_id> <comando>\n", 37);
            return 1;
        }

        Msg m;
        m.runner_pid = getpid();
        m.type       = 1;
        m.user_id    = atoi(argv[2]);

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
            unlink(meu_fifo);
            return 1;
        }
        write(fd_c, &m, sizeof(Msg));
        close(fd_c);

        int n1 = snprintf(out_buf, sizeof(out_buf),
            "[runner] command %d submitted\n", m.runner_pid);
        write(1, out_buf, n1);

        /* Espera autorização */
        int fd_r = open(meu_fifo, O_RDONLY);
        int ok;
        read(fd_r, &ok, sizeof(int));
        close(fd_r);

        int n2 = snprintf(out_buf, sizeof(out_buf),
            "[runner] executing command %d...\n", m.runner_pid);
        write(1, out_buf, n2);

        /* Executa pipeline */
        exec_pipeline(m.command);

        /* Notifica controller que terminou */
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

    /* ---- MODO -c (CONSULTAR) ---- */
    else if (strcmp(argv[1], "-c") == 0) {

        Msg m;
        m.runner_pid = getpid();
        m.type       = 2;
        m.user_id    = (argc > 2) ? atoi(argv[2]) : 0;

        char meu_fifo[64];
        char buffer[2048];

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
        int n = read(fd_r, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            write(1, buffer, n);
        }
        close(fd_r);
        unlink(meu_fifo);
    }

    /* ---- MODO -s (SHUTDOWN) ---- */
    else if (strcmp(argv[1], "-s") == 0) {

        Msg m;
        m.runner_pid = getpid();
        m.type       = 5;
        m.user_id    = 0;

        int fd_c = open("tmp/controller_pipe", O_WRONLY);
        if (fd_c == -1) {
            write(2, "Erro: Controller nao esta ativo\n", 32);
            return 1;
        }
        write(fd_c, &m, sizeof(Msg));
        close(fd_c);

        write(1, "[runner] shutdown signal sent\n", 30);
    }

    else {
        write(2, "Modo nao suportado\n", 20);
        return 1;
    }

    return 0;
}