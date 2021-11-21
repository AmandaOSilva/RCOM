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
    char *port = "/dev/ttyS10";

    int fd = llopen(port, TRANSMITTER);

    FILE *file = fopen("pinguim.gif", "rb");
    int fileD = fileno(file);
    unsigned char *buffer = malloc(MAX_SIZE);
    int i;
    for (i = 0; i < MAX_SIZE; ++i) {
        if (read(fileD, &buffer[i], 1) == -1) {
            break;
        }
    }
    int bufferSize = i;
    int qtyBlock = 1;
    if (bufferSize > BYTES_PER_PACKAGE) {
        qtyBlock = bufferSize / BYTES_PER_PACKAGE;
        if (bufferSize % BYTES_PER_PACKAGE != 0) qtyBlock++;
    }
    printf("BufferSize: %d\n", bufferSize);
    printf("Quantidade de blocos: %d\n", qtyBlock);
    unsigned char *dataFrame = malloc(BYTES_PER_PACKAGE);
    for (int j = 0; j <= qtyBlock; ++j) {
        memcpy(dataFrame, &buffer[j * BYTES_PER_PACKAGE], BYTES_PER_PACKAGE);

        printf("Enviando bloco: %d\n", j);
        llwrite(fd, dataFrame, sizeof(dataFrame));
    }
//    char *buffer = "Ama~nda";

    llclose(fd);
    return 1;
}