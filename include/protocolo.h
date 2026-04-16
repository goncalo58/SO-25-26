#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <sys/types.h>
#include <sys/time.h>

typedef struct {
    pid_t runner_pid;    
    int type;            // 1=Executar (-e), 2=Consultar (-c), 3=Terminar (-s)
    int user_id;
    char command[300];
    
    struct timeval start_time;

} Msg;

#endif