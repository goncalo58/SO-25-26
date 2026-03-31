#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h> 

int main(int argc, char *argv[]) {

    mkfifo("tmp/controller_pipe", 0666);

    
    int fifo_fd = open("tmp/controller_pipe", O_RDONLY);
    
    if (fifo_fd == -1) {
        perror("Erro ao abrir FIFO");
        return 1;
    }

    write(1, "DEBUG: fifo is open and waiting...\n", 35);

    char buf[100];
    int read_bytes;
    
    while ((read_bytes = read(fifo_fd, buf, 100)) > 0) {
        write(1, "Controller recebeu: ", 20);
        write(1, buf, read_bytes);
        write(1, "\n", 1);
    }
    close(fifo_fd);
    unlink("tmp/controller_pipe"); 

    return 0;
}