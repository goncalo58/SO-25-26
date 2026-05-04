#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <sys/types.h>
#include <sys/time.h>

typedef struct {
    pid_t  runner_pid;
    int    type;       /* 1=Executar, 2=Consultar, 4=Terminou, 5=Shutdown */
    int    user_id;
    char   command[300];
    struct timeval start_time;
} Msg;

#endif