#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include "protocolo.h"

#define MAX_QUEUE   200
#define MAX_RUNNING 200

/* ------------------------------------------------------------------ *
 *  Política de escalonamento: Round-Robin por utilizador             *
 *                                                                    *
 *  A fila de espera é mantida por ordem de chegada.                 *
 *  Quando uma vaga fica livre, o controller percorre a fila e       *
 *  escolhe o primeiro pedido cujo user_id seja diferente do         *
 *  último user_id que foi autorizado (se existir). Só se não        *
 *  houver nenhum diferente é que volta ao mesmo utilizador.         *
 *  Isto garante alternância entre utilizadores (fairness).          *
 * ------------------------------------------------------------------ */

static int    queue_count   = 0;
static Msg    queue[MAX_QUEUE];

static int    running_count = 0;
static Msg    running[MAX_RUNNING];

static int    running_now   = 0;
static int    max_parallel  = 1;
static int    last_user_id  = -1;   /* último utilizador autorizado */

/* Envia autorização ao runner e regista-o como em execução */
static void authorize(Msg *m) {
    char fifo_res[64];
    snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m->runner_pid);

    int fd_res = open(fifo_res, O_WRONLY);
    if (fd_res == -1) return;

    int ok = 1;
    write(fd_res, &ok, sizeof(int));
    close(fd_res);

    running[running_count++] = *m;
    running_now++;
    last_user_id = m->user_id;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "[controller] PID %d (user %d) autorizado [paralelo: %d/%d]\n",
        m->runner_pid, m->user_id, running_now, max_parallel);
    write(1, buf, n);
}

/* Escolhe o próximo da fila usando Round-Robin por utilizador */
static int pick_next(void) {
    if (queue_count == 0) return -1;

    /* Tenta encontrar um utilizador diferente do último */
    for (int i = 0; i < queue_count; i++) {
        if (queue[i].user_id != last_user_id)
            return i;
    }
    /* Todos do mesmo utilizador: aceita o primeiro */
    return 0;
}

/* Remove índice idx da fila */
static void queue_remove(int idx) {
    for (int i = idx; i < queue_count - 1; i++)
        queue[i] = queue[i + 1];
    queue_count--;
}

/* Tenta preencher vagas disponíveis com pedidos da fila */
static void fill_slots(void) {
    while (running_now < max_parallel && queue_count > 0) {
        int idx = pick_next();
        if (idx < 0) break;
        Msg next = queue[idx];
        queue_remove(idx);
        authorize(&next);
    }
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {

    if (argc < 2) {
        write(2, "Uso: ./controller <max_parallel>\n", 33);
        return 1;
    }

    max_parallel = atoi(argv[1]);
    if (max_parallel < 1) max_parallel = 1;

    {
        char buf[128];
        int n = snprintf(buf, sizeof(buf),
            "[controller] iniciado | max_parallel=%d\n", max_parallel);
        write(1, buf, n);
    }

    mkfifo("tmp/controller_pipe", 0666);
    /* Abre em O_RDWR para que o pipe nunca fique sem escritor e o
       read() não retorne 0 prematuramente quando não há runners.    */
    int fd_c = open("tmp/controller_pipe", O_RDWR);
    if (fd_c == -1) {
        write(2, "Erro ao abrir controller_pipe\n", 30);
        return 1;
    }

    Msg m;
    char buffer[512];
    int shutdown_requested = 0;
    int running_flag = 1;

    while (running_flag) {

        ssize_t r = read(fd_c, &m, sizeof(Msg));
        if (r <= 0) break;

        /* ---------- NOVO PEDIDO DE EXECUÇÃO (-e) ---------- */
        if (m.type == 1) {

            gettimeofday(&m.start_time, NULL);

            if (running_now < max_parallel) {
                authorize(&m);
            } else {
                /* Vai para a fila de espera */
                if (queue_count < MAX_QUEUE) {
                    queue[queue_count++] = m;
                    int n = snprintf(buffer, sizeof(buffer),
                        "[controller] PID %d (user %d) em fila [pos %d]\n",
                        m.runner_pid, m.user_id, queue_count);
                    write(1, buffer, n);
                } else {
                    write(2, "[controller] fila cheia, pedido rejeitado\n", 42);
                }
            }
        }

        /* ---------- COMANDO TERMINOU (-e fim) ---------- */
        else if (m.type == 4) {

            /* Procura na lista de execução */
            int idx = -1;
            for (int i = 0; i < running_count; i++) {
                if (running[i].runner_pid == m.runner_pid) {
                    idx = i;
                    break;
                }
            }

            if (idx != -1) {
                struct timeval end;
                gettimeofday(&end, NULL);

                double duration =
                    (end.tv_sec  - running[idx].start_time.tv_sec) +
                    (end.tv_usec - running[idx].start_time.tv_usec) / 1e6;

                /* Registo persistente */
                int fd_log = open("log.txt",
                                  O_CREAT | O_WRONLY | O_APPEND, 0666);
                if (fd_log != -1) {
                    char linha[512];
                    int nlog = snprintf(linha, sizeof(linha),
                        "user %d | pid %d | cmd \"%s\" | tempo %.3f s\n",
                        running[idx].user_id,
                        running[idx].runner_pid,
                        running[idx].command,
                        duration);
                    write(fd_log, linha, nlog);
                    close(fd_log);
                }

                /* Remove da lista de execução */
                for (int i = idx; i < running_count - 1; i++)
                    running[i] = running[i + 1];
                running_count--;
                running_now--;

                int n = snprintf(buffer, sizeof(buffer),
                    "[controller] PID %d terminou (%.3f s)\n",
                    m.runner_pid, duration);
                write(1, buffer, n);
            }

            /* Tenta preencher vagas com pedidos da fila */
            fill_slots();
        }

        /* ---------- CONSULTA (-c) ---------- */
        else if (m.type == 2) {

            char fifo_res[64];
            snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);

            int fd_res = open(fifo_res, O_WRONLY);
            if (fd_res != -1) {
                char out[4096];
                int  len = 0;

                len += snprintf(out + len, sizeof(out) - len,
                    "=== ESTADO DO CONTROLLER ===\n");
                len += snprintf(out + len, sizeof(out) - len,
                    "Paralelo max : %d\n", max_parallel);
                len += snprintf(out + len, sizeof(out) - len,
                    "A correr     : %d\n", running_now);
                len += snprintf(out + len, sizeof(out) - len,
                    "Em fila      : %d\n\n", queue_count);

                if (running_count > 0) {
                    len += snprintf(out + len, sizeof(out) - len,
                        "-- Em execucao --\n");
                    for (int i = 0; i < running_count; i++) {
                        len += snprintf(out + len, sizeof(out) - len,
                            "  PID %-7d | user %d | cmd: %s\n",
                            running[i].runner_pid,
                            running[i].user_id,
                            running[i].command);
                    }
                    len += snprintf(out + len, sizeof(out) - len, "\n");
                }

                if (queue_count > 0) {
                    len += snprintf(out + len, sizeof(out) - len,
                        "-- Em espera (ordem de escalonamento) --\n");
                    for (int i = 0; i < queue_count; i++) {
                        len += snprintf(out + len, sizeof(out) - len,
                            "  [%d] PID %-7d | user %d | cmd: %s\n",
                            i + 1,
                            queue[i].runner_pid,
                            queue[i].user_id,
                            queue[i].command);
                    }
                }

                write(fd_res, out, len);
                close(fd_res);
            }
        }

        /* ---------- SHUTDOWN (-s) ---------- */
        else if (m.type == 5) {
            write(1, "[controller] shutdown pedido\n", 29);
            shutdown_requested = 1;
        }

        /* Verifica condição de saída */
        if (shutdown_requested && running_now == 0 && queue_count == 0) {
            write(1, "[controller] shutdown concluido\n", 32);
            running_flag = 0;
        }
    }

    close(fd_c);
    unlink("tmp/controller_pipe");
    return 0;
}