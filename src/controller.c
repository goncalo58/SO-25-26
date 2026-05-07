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


static int    queue_count   = 0;
static Msg    queue[MAX_QUEUE];     // Fila de espera (pedidos parados)

static int    running_count = 0;
static Msg    running[MAX_RUNNING]; // Comandos que estão a correr agora

static int    running_now   = 0;    // Contador de slots ocupados
static int    max_parallel  = 1;    // Limite P definido no arranque
static int    last_user_id  = -1;   // Memória do último utilizador servido (para Round-Robin)

/* * o sinal verde
 * envia um sinal ao runner via FIFO privado para ele avançar com o execvp().
 */
static void authorize(Msg *m) {
    char fifo_res[64];
    snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m->runner_pid);

    // abre o canal privado do runner
    int fd_res = open(fifo_res, O_WRONLY);
    if (fd_res == -1) return;

    int ok = 1;
    // o runner está bloqueado num read(); este write "acorda-o"
    write(fd_res, &ok, sizeof(int));
    close(fd_res);

    // regista que este comando passou a estar em execução
    running[running_count++] = *m;
    running_now++;
    last_user_id = m->user_id; // atualiza quem foi o último servido

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "[controller] PID %d (user %d) autorizado [paralelo: %d/%d]\n",
        m->runner_pid, m->user_id, running_now, max_parallel);
    write(1, buf, n);
}

/* * PICK_NEXT: O "Cérebro" do Escalonamento
 * implementa a política Round-Robin por utilizador (jutiça)
 ----------------------------------------------------------
 * Em vez de um FIFO simples, o escalonador percorre a fila 
 * à procura do primeiro pedido de um UTILIZADOR DIFERENTE 
 * do último que foi servido.
 *
 * Objetivo: Evitar que um utilizador com muitos comandos 
 * monopolize o sistema (starvation), garantindo que todos 
 * progridam independentemente da carga dos outros.
 */
static int pick_next(void) {
    if (queue_count == 0) return -1;

    /* Tenta encontrar alguém que NÃO seja o último utilizador servido */
    for (int i = 0; i < queue_count; i++) {
        if (queue[i].user_id != last_user_id)
            return i; // Encontrou um utilizador diferente (Justiça!)
    }
    
    /* Se todos na fila forem do mesmo utilizador, serve o mais antigo */
    return 0;
}

/* Remove um elemento da fila de espera e "chega" os outros para a frente */
static void queue_remove(int idx) {
    for (int i = idx; i < queue_count - 1; i++)
        queue[i] = queue[i + 1];
    queue_count--;
}

/* Sempre que abre uma vaga, esta função tenta preenchê-la com quem está na fila */
static void fill_slots(void) {
    while (running_now < max_parallel && queue_count > 0) {
        int idx = pick_next();
        if (idx < 0) break;
        Msg next = queue[idx];
        queue_remove(idx);
        authorize(&next);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(2, "Uso: ./controller <max_parallel>\n", 33);
        return 1;
    }

    max_parallel = atoi(argv[1]);
    if (max_parallel < 1) max_parallel = 1;

    //  arranque do servidor
    {
        char buf[128];
        int n = snprintf(buf, sizeof(buf),
            "[controller] iniciado | max_parallel=%d\n", max_parallel);
        write(1, buf, n);
    }

    // Cria o ponto de entrada central
    mkfifo("tmp/controller_pipe", 0666);
    
    /* *  abrimos o pipe em O_RDWR (Leitura e Escrita).
     * isto garante que o pipe nunca fica sem "escritores", logo o read() 
     * nunca retorna 0 (EOF) e o controller não desliga sozinho.

     * Se abríssemos apenas em O_RDONLY, o read() retornaria 0 (EOF) 
     * sempre que o último runner ativo fechasse a sua extremidade de escrita.
    * Isto faria com que o Controller saísse do ciclo while e terminasse 
    * prematuramente, mesmo havendo comandos em fila ou a aguardar novos pedidos.
    * Com O_RDWR, o próprio Controller conta como um "escritor" ativo, 
    * mantendo o pipe aberto e o servidor em escuta permanente.
     */
    int fd_c = open("tmp/controller_pipe", O_RDWR);
    if (fd_c == -1) return 1;

    Msg m;
    char buffer[512];
    int shutdown_requested = 0;
    int running_flag = 1;

    //  O servidor nunca dorme
    while (running_flag) {
        // O processo fica parado aqui até chegar uma mensagem de qualquer runner
        ssize_t r = read(fd_c, &m, sizeof(Msg));
        if (r <= 0) break;

        /* TIPO 1: NOVO PEDIDO DE EXECUÇÃO (-e) */
        if (m.type == 1) {
            gettimeofday(&m.start_time, NULL); // Marca o tempo de chegada

            if (running_now < max_parallel && !shutdown_requested) {
                authorize(&m);
            } else if (!shutdown_requested) {
                // Se estiver cheio, guarda na fila
                if (queue_count < MAX_QUEUE) {
                    queue[queue_count++] = m;
                    int n = snprintf(buffer, sizeof(buffer),
                        "[controller] PID %d (user %d) em fila [pos %d]\n",
                        m.runner_pid, m.user_id, queue_count);
                    write(1, buffer, n);
                }
            }
        }

        /* comando terminoou, notificaçao vinda do runner*/
        else if (m.type == 4) {
            int idx = -1;
            for (int i = 0; i < running_count; i++) {
                if (running[i].runner_pid == m.runner_pid) {
                    idx = i; break;
                }
            }

            if (idx != -1) {
                struct timeval end;
                gettimeofday(&end, NULL);
                double duration = (end.tv_sec - running[idx].start_time.tv_sec) +
                                 (end.tv_usec - running[idx].start_time.tv_usec) / 1e6;

                // regista no log
                int fd_log = open("log.txt", O_CREAT | O_WRONLY | O_APPEND, 0666);
                if (fd_log != -1) {
                    char linha[512];
                    int nlog = snprintf(linha, sizeof(linha),
                        "user %d | pid %d | cmd \"%s\" | tempo %.3f s\n",
                        running[idx].user_id, running[idx].runner_pid, running[idx].command, duration);
                    write(fd_log, linha, nlog);
                    close(fd_log);
                }

                // Remove da lista de ativos e tenta meter alguém da fila
                for (int i = idx; i < running_count - 1; i++) running[i] = running[i + 1];
                running_count--;
                running_now--;
                fill_slots();
            }
        }

        /* consulta de estado (-c) */
        else if (m.type == 2) {
            char fifo_res[64];
            snprintf(fifo_res, sizeof(fifo_res), "tmp/fifo_%d", m.runner_pid);
            int fd_res = open(fifo_res, O_WRONLY);
            if (fd_res != -1) {
                char out[4096];
                int len = snprintf(out, sizeof(out), "=== ESTADO DO CONTROLLER ===\nParalelo max: %d\nA correr: %d\nEm fila: %d\n", 
                                   max_parallel, running_now, queue_count);
                write(fd_res, out, len);
                close(fd_res);
            }
        }

        /* pedido de shutdown(-s) */
        else if (m.type == 5) {
            write(1, "[controller] shutdown pedido (a aguardar fim de tarefas)\n", 57);
            shutdown_requested = 1;
        }

        // Só desliga se não houver mais nada a fazer
        if (shutdown_requested && running_now == 0 && queue_count == 0) {
            running_flag = 0;
        }
    }

    unlink("tmp/controller_pipe");
    return 0;
}