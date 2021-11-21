#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "linkLayer.c"

int main(int argc, char **argv) {
    char *port = "/dev/ttyS11";
    int fd = llopen(port, RECEIVER);

    char *buffer = malloc(BYTES_PER_PACKAGE);
    llread(fd, buffer);
    printf("Recebido");
    //printf("Recebido: %s", buffer);

    llclose(fd);
    return 1;
}

//int main(int argc, char **argv) {
//    strncpy(linkLayer1.port, "/dev/ttyS10", 20);
//    int modo = TRANSMITTER;
//    if ((strcmp("1", argv[1]) == 0)) {
//        modo = RECEIVER;
//        strncpy(linkLayer1.port, "/dev/ttyS11", 20);
//    }
//
//    int fd = llopen(linkLayer1.port, modo);
//    printf("descritor: %d", fd);
//
//    //strncpy(buffer, "Amanda", 6);
//    if (modo) {
//        char *buffer = "Ama~nda";
//        llwrite(fd, buffer, 7);
//    } else {
//        char *buffer;
//        llread(fd, buffer);
//        printf("Recebido: %s", buffer);
//    }
//    llclose(fd);
//    return 1;
//}