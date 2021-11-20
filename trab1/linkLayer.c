#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "state_machine.c"
//#include "utils.h"

#define BAUDRATE B38400
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1


#define START_FLAG 0x02
#define END_FLAG 0x03

//FRAME FLAG
//#define FR_FLAG 0x7e
#define SCAPE 0x7d


///
//#define MAX_SIZE 512


/// Tamanho  512


typedef struct {
    char port[20]; /*Dispositivo /dev/ttySx, x = 0, 1*/
    int baudRate; /*Velocidade de transmissão*/
    unsigned int sequenceNumber; /*Número de sequência da trama: 0, 1*/
    unsigned int timeout; /*Valor do temporizador: 1 s*/
    unsigned int numTransmissions; /*Número de tentativas em caso de falha*/
    char frame[MAX_SIZE]; /*Trama*/
} linkLayer;

linkLayer linkLayer1;
bool mode;

int llopen(char port[20], bool aMode) {
    mode = aMode;
    struct termios oldtio, newtio;
    printf("Modo: %d\n", mode);

    int fd, res, size;
    volatile int STOP = FALSE;

    fd = open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(port);
        exit(-1);
    }

    if (tcgetattr(fd, &oldtio) == -1) { /* save current port settings */
        perror("tcgetattr \n");
        exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; /* inter-character timer unused */
    newtio.c_cc[VMIN] = 5;  /* blocking read until 5 chars received */

    cfsetispeed(&newtio, BAUDRATE);
    cfsetospeed(&newtio, BAUDRATE);

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }

    int numAttempts = 0;
    uc *receivedFrame = malloc(sizeof(uc) * 5);
    if (mode == TRANSMITTER) {
        while (TRUE) {
            /* enviar SET e esperar UA */

//            linkLayer1.frame[0] = FR_FLAG;
//            linkLayer1.frame[1] = 0x03; // comando enviado pelo emissor
//            linkLayer1.frame[2] = 0x03; // SET
//            linkLayer1.frame[3] = 0xFF; //TODO: BCC1, que ainda precisa ser calculado e depois vertificado do outro lado)
//            linkLayer1.frame[4] = FR_FLAG;
//            res = write(fd, linkLayer1.frame,  sizeof(uc) * 5);
//            printf("enviados: %d\n", res);
//            print_su_frame(linkLayer1.frame, sizeof(uc) * 5);

            numAttempts++;

            printf("Abrindo em modo Transmitter\n");
            if (numAttempts > 3) {
                printf("Nao foi possivel estabelecer conexao\n");
                return -1;
            }

            printf("Tentativa: %d\n", numAttempts);
            uc *frameSET = create_su_frame(EM_CMD, SET);
            print_su_frame(frameSET, sizeof(uc) * 5);
            send_su_frame(fd, frameSET);
            printf("Terminou o send\n");

            // Esperar o UA
            if (!receive_su_frame(fd, receivedFrame, EM_CMD, UA, TRANSMITTER))
                continue;
            printf("recebido\n"); //TODO retirar
            if (!check_su_frame(receivedFrame, UA))
                continue;
            printf("recebido UA\n"); //TODO retirar
            printf("Conexao estabelecida em modo Transmitter\n");
            break;
        }
    } else if (mode == RECEIVER) { // RECEIVER
        printf("Abrindo em modo Receiver\n");
        /* espera o SET e devolve o UA */
        while (TRUE) {
            if (!receive_su_frame(fd, receivedFrame, EM_CMD, SET, RECEIVER))
                continue;
            printf("recebido\n"); //TODO retirar
            if (!check_su_frame(receivedFrame, SET))
                continue;
            printf("recebido set \n"); //TODO retirar
            send_su_frame(fd, create_su_frame(EM_CMD, UA));
            printf("Conexao estabelecida em modo Receiver\n");
            break;
        }
    }
    return fd;
}

int llwrite(int fd, char *buffer, int length) {
    /* write() informacao (I) */

}

int llread(int fd, char *buffer) {
    /* read() informacao (I) */

}

int llclose(int fd) {
    if (mode == TRANSMITTER) {
        /* enviar DISC, esperar DISC e enviar UA e fecha conexao */

        printf("Fechando em modo Trasmitter\n");
    } else { // RECEIVER
        /* espera DISC, envia DISC, espera UA e fecha conexao */
        printf("Fechando em modo Receiver \n");
    }
    return 1;
}


int main(int argc, char **argv) {
    strncpy(linkLayer1.port, "/dev/ttyS10", 20);
    int modo = TRANSMITTER;
    if ((strcmp("1", argv[1]) == 0)) {
        modo = RECEIVER;
        strncpy(linkLayer1.port, "/dev/ttyS11", 20);
    }

    int fd = llopen(linkLayer1.port, modo);
    printf("descritor: %d", fd);
    llclose(fd);
    return 1;
}

