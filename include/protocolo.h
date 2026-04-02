#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <sys/types.h>

typedef struct {
    pid_t runner_pid;    
    int type;            // 1=Executar (-e), 2=Consultar (-c), 3=Terminar (-s)
    int user_id;
    char command[300];   
} Msg;

#endif