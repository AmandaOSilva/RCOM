#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "linkLayer.c"
#include "appLayer.h"

typedef enum SET_STATES_RECEIVE {
    INIT,
    DATA,
    END
} set_states_receive;

int processControlPackage(unsigned char *buffer, const int expectControl, int control) {
    for (int i = 0; i < 20; ++i) {
        printf(" %02x", buffer[i]);
    }
    printf("\n");
    if (control == expectControl) {
        // obtem o tamanho do arquivo
        // ja  sabemos de antemao  que primeiro paramento e o tamanho do arquivo e que ele deve usar 2 octetos
        if (buffer[1] != APP_PARAM_SIZE || buffer[2] != 2) {
            printf("Pacote rejeitado. erro no campo tamanho do arquivo\n"); // TODO avaliar se precisa disso e arrumar a msg
            return -1;
        }

        int filesize = buffer[3] * 256 + buffer[4];

        printf("Tamanho: %d\n", filesize);
        // obtem o nome do arquivo
        if (buffer[5] != APP_PARAM_NAME) {
            printf("Pacote rejeitado. erro no campo tamanho do arquivo\n");
            return -1;
        }
        int filenameSize = buffer[6];
        char *filename  = malloc(filenameSize);
        memcpy(filename, &buffer[7], filenameSize);
        printf("Nome original : %s\n", filename);
        return 0;
    } else {
        printf("Pacote rejeitado. Esperado: %d, Recebido: %d\n", expectControl, control);
        return -1;
    }
}

int processDataPackage(const unsigned char *buffer, const int expectedSeq,int bufferSize) {
    for (int i = 0; i < 10; ++i) {
        printf(" %02x", buffer[i]);
    }
    printf("\n");
    unsigned char seq = buffer[1];
    if (expectedSeq != seq) {
        return -1;
    }
    int size = buffer[2] * 256 + buffer[3];
    if (size != bufferSize - INFO_LENGTH) {
        printf("erro no recebimente dos dados\n");
        return -1;
        //continue;
    }
    printf("seq: %d, bufferSize: %d\n", seq, size);
    return size;
}

int main(int argc, char **argv) {
    if (argc <2) {
        perror("Usage: ./receptor <port> <filename>");
        exit(-1);
    }
    char *port = argv[1];

    int fd = llopen(port, RECEIVER);
    char *filename = argv[2];
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        ferror(file);
        exit(-1);
    }

    unsigned char *buffer = malloc(BYTES_PER_PACKAGE);

    int control;
    int expectedSeq = 1;
    set_states_receive state = INIT;
    while (state != END) {
        int bufferSize = llread(fd, buffer);
        control = buffer[0];
        //le pacote de inicio
        switch (state) {
            case INIT: {
                if (processControlPackage(buffer, APP_START, control) == 0) {
                    state = DATA;
                }
                continue;
            }
            case DATA: {
                if (control == APP_DATA) {
                    int size = processDataPackage(buffer, expectedSeq,  bufferSize);
                    if (size < 0) {
                        perror("Erro ao receber pacote de dados");
                        exit(-1);
                    }
                    if (fwrite(&buffer[4], 1, size, file) <0 ) {
                        perror("Erro ao gravar pacote de dados");
                        exit(-1);
                    }
                    expectedSeq++;
                } else if (control == APP_END) {
                    if (processControlPackage(buffer, APP_END, control) == 0) {
                        state = END;
                        printf("Arquivo salvo com sucesso: %s", filename);
                    }
                    continue;
                }
            }
        }
    }
    printf(" \n");
    llclose(fd);
    fclose(file);
    return 1;
}
