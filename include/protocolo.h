typedef struct {
    pid_t runner_pid;    // Para saberes quem respondeu
    int type;            // 1=Executar, 2=Consultar, 3=Terminar, 4=Concluiu
    int user_id;
    char command[300];   // O comando propriamente dito
} Msg;