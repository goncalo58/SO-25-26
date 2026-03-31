#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "protocolo.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    // 1. Abrir o FIFO (Mesmo nome que no Controller!)
    mkfifo("tmp/controller_pipe", 0666);

    
    int fifo_fd = open("tmp/controller_pipe", O_WRONLY);
    

    if (fifo_fd == -1) {
        perror("Erro ao abrir FIFO ");
        return 1;
    }

    write(1, "DEBUG: fifo is open for writing\n", 32);

    // 2. O que enviar? 
    // Em vez de ler do teclado (0), vamos enviar o argumento que passas no terminal
    if (argc > 1) {
        char *comando = argv[1]; 
        int tam = strlen(comando);

        // Escrever no FIFO
        write(fifo_fd, comando, tam);
    }

    // 3. Fechar (Slide 15 do teu guia)
    close(fifo_fd);
    return 0;
}