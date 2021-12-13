#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "linkLayer.c"
#include "appLayer.h"

void sendControlPackage(const int fd, const char *filename, const int bufferSize, const int control) {
    size_t filenameSize = strlen(filename);
    int packageFrameSize = 1 + 4 + (2 + filenameSize); // controlo, tamanho e nome

    printf("Tamanho do pacote: %d\n", packageFrameSize);
    char *packageFrame = malloc(packageFrameSize);
    packageFrame[0] = control;
    //tamanho
    packageFrame[1] = APP_PARAM_SIZE;
    packageFrame[2] = 2;
    packageFrame[3] = bufferSize / 256;
    packageFrame[4] = bufferSize % 256;
    printf("tamanho: %d, %d\n", bufferSize / 256, bufferSize % 256);
    packageFrame[5] = APP_PARAM_NAME;
    packageFrame[6] = filenameSize;
    memcpy(&packageFrame[7], filename, filenameSize);
    for (int i = 0; i < 10; ++i) {
        printf(" %02x", packageFrame[i]);
    }
    printf("\n");
    printf("Nome: %s\n", filename);

    if (llwrite(fd, packageFrame, packageFrameSize) < 0) {
        perror("Erro ao enviar");
        exit(-1);
    };
}

void sendDataPackage(const int fd, const char *buffer, const int packageSeq, const int packageSize) {
    int packageFrameSize = 4 + packageSize; // controlo, tamanho e nome

    printf("Tamanho do pacote: %d\n", packageFrameSize);
    char *packageFrame = malloc(packageFrameSize);
    packageFrame[0] = APP_DATA;
    //tamanho
    packageFrame[1] = packageSeq;
    packageFrame[2] = packageSize / 256;
    packageFrame[3] = packageSize % 256;
    printf("tamanho: %d, %d\n", packageSize / 256, packageSize % 256);
    memcpy(&packageFrame[4], buffer, packageSize);
    for (int i = 0; i < 10; ++i) {
        printf(" %02x", packageFrame[i]);
    }
    printf("\n");
    llwrite(fd, packageFrame, packageFrameSize);
}


FILE *openFile(const char *filename, int *bufferSize) {
    FILE *file = fopen(filename, "rb");

    fseek(file, 0L, SEEK_END);
    (*bufferSize) = ftell(file);
    fseek(file, 0L, SEEK_SET);
    return file;
}

void sendData(int fd, int bufferSize, FILE *file) {//Envia  dados
//    printf(" \n");
    int qtyPackage = 1;
    int packageSize = BYTES_PER_PACKAGE - INFO_LENGTH;
    if (bufferSize > packageSize) {
        qtyPackage = bufferSize / packageSize;
        if (bufferSize % packageSize != 0) qtyPackage++;
    }
    printf("BufferSize: %d\n", bufferSize);
    printf("Quantidade de blocos: %d\n", qtyPackage);

    for (int packageSeq = 1; packageSeq <= qtyPackage; ++packageSeq) {

        if (packageSeq == qtyPackage)
            packageSize = bufferSize % packageSize;
        printf("Enviando: seq=%d, size=%d\n", packageSeq, packageSize);
        char *buffer = malloc(packageSize);
        fread(buffer, packageSize, 1, file);
        printf("Leu arquivo\n");
        sendDataPackage(fd, buffer, packageSeq, packageSize);
        free(buffer);
    }
}

int main(int argc, char **argv) {
    srand(time(NULL));
    if (argc < 2) {
        perror("Usage: ./emissor <port> <filename>");
        exit(-1);
    }
    char *port = argv[1];

    int fd = llopen(port, TRANSMITTER);
    if (fd < 0) {
        perror(port);
        exit(-1);
    }
    char *filename = argv[2];
    int bufferSize;

    // abrirt arquivo
    FILE *file = openFile(filename, &bufferSize);
    if (file == NULL) {
        return -1;
    }

    //envia pacote START
    sendControlPackage(fd, filename, bufferSize, APP_START);
    // envia pacotes com os dados
    sendData(fd, bufferSize, file);
    // envia pacote de END
    sendControlPackage(fd, filename, bufferSize, APP_END);

    // Fecha conexao
    llclose(fd);
    fclose(file);
    return 1;
}



