#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "state_machine.c"

#define BAUDRATE B38400
#define FALSE 0
#define TRUE 1



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

    int fd;
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

    /*Seta o modo  */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 5;  /* blocking read until 5 chars received */

    cfsetispeed(&newtio, BAUDRATE);
    cfsetospeed(&newtio, BAUDRATE);

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        exit(-1);
    }

    int numAttempts = 0;
    unsigned char *receivedFrame = malloc(5);
    if (mode == TRANSMITTER) {
        while (TRUE) {
            /* enviar SET e esperar UA */

            numAttempts++;

            printf("Abrindo em modo Transmitter\n");
            if (numAttempts > 3) {
                printf("Nao foi possivel estabelecer conexao\n");
                return -1;
            }

            printf("Tentativa: %d\n", numAttempts);
            sendSupFrame(fd, EM_CMD, SET);
            printf("Terminou o send\n");

            // Esperar o UA
            if (!receiveFrame(fd, receivedFrame, EM_CMD, UA, TRANSMITTER))
                continue;
            printf("recebido\n"); //TODO retirar
            if (!(receivedFrame[CTRL_IND] == UA))
                continue;
            printf("recebido UA\n"); //TODO retirar
            printf("Conexao estabelecida em modo Transmitter\n");
            break;
        }
    } else if (mode == RECEIVER) { // RECEIVER
        printf("Abrindo em modo Receiver\n");
        /* espera o SET e devolve o UA */
        while (TRUE) {
            if (!receiveFrame(fd, receivedFrame, EM_CMD, SET, RECEIVER))
                continue;
            printf("recebido\n"); //TODO retirar
            if (!(receivedFrame[CTRL_IND] == SET))
                continue;
            printf("recebido set \n"); //TODO retirar
            sendSupFrame(fd, EM_CMD, UA);
            printf("Conexao estabelecida em modo Receiver\n");
            break;
        }
    }
    return fd;
}

int llwrite(int fd, char *buffer, int length) {
    int res;

    unsigned char bcc2 = calculate_bcc2(buffer, length);
    int bcc2_size = 1;
    int info_length = length + bcc2_size;
    unsigned char *info = malloc((info_length));
    memcpy(info, buffer, length);
    memcpy(&info[length], &bcc2, bcc2_size);
    unsigned char *stuffedData = stuffing(info, &info_length);
    unsigned char *frameData = malloc(5);
    int sent = 0;
    int received_status;
    int numAttempts = 0;
    while (!sent) {
        while (numAttempts < 3) {
            numAttempts++;
            unsigned char *frame = malloc((info_length + 5));
            frame[FLAG_IND] = FR_FLAG;
            frame[ADDR_IND] = EM_CMD;
            frame[CTRL_IND] = SEND_SEQ;
            frame[BCC_IND] = frame[ADDR_IND] ^ frame[CTRL_IND];
            memcpy(&frame[4], stuffedData, info_length);
            frame[4 + info_length] = FR_FLAG;
            if ((res = send_info_frame(fd, frame,
                                       info_length + 5)) < 0) {
                perror("Error while writing frame\n");
            }
            alarm(3);
            received_status = receiveFrame(fd, frameData, EM_CMD, REC_READY, TRANSMITTER);
            alarm(0);
            if (received_status == 3) {
                printf("\nRejected frame! Resending...\n");
                continue;
            } else if (received_status == 0) {
                continue;
            }
            if (!(frameData[CTRL_IND] == REC_READY))
                continue;
            sent = 1;
            break;
        }
        if (numAttempts >= 3) {
            printf("Maximum attempts exceeded!\n");
            return -1;
        }
    }
    free(stuffedData);
    free(frameData);
    free(info);
    //sigaction(SIGALRM,&old,NULL);
    update_seq();
    return res;
}

int llread(int fd, char *buffer) {
    /* read() informacao (I) */
    unsigned char *frameData = malloc(
            BYTES_PER_PACKAGE * 2);
    unsigned char *stuffedData;
    unsigned char *destuffedData;

    int rejected = 0;
    int real_size = 0, data_size;//,rand;
    // reset();
    int numAttempts = 0;
    while (numAttempts < 3) {
        //rand = random()%2;
        rejected = 0;
        alarm(3);
        if (!receive_info_frame(fd, frameData, &real_size)) {
            alarm(0);
            sendSupFrame(fd, EM_CMD, REC_REJECTED);
            rejected = 1;
            continue;
        }
        alarm(0);
        unsigned char *final_frame = malloc(real_size);
        memcpy(final_frame, frameData, real_size);
        stuffedData = retrieve_info_frame_data(final_frame, real_size, &data_size);
        destuffedData = destuffing(stuffedData, &data_size);
        unsigned char received_bcc2 = destuffedData[data_size - 1];
//      unsigned char calculated_bcc2 = rand == 1 ? 0 : calculate_bcc2(destuffedData,data_size-1);
        unsigned char calculated_bcc2 = calculate_bcc2(destuffedData, data_size - 1);
        if (received_bcc2 != calculated_bcc2) {
            printf("\nBCC2 not recognized\n");
            rejected = 1;
            sendSupFrame(fd, EM_CMD, REC_REJECTED);
            continue;
        }
        sendSupFrame(fd, EM_CMD, REC_READY);
        if (!rejected) {
            memcpy(buffer, destuffedData, data_size - 1);
            free(frameData);
            free(final_frame);
            free(stuffedData);
            free(destuffedData);
            update_seq();
            return data_size - 1;
        }
        return -2;
    }
    return -1;
}

int llclose(int fd) {
    unsigned char *receivedFrame = malloc(5);
    if (mode == TRANSMITTER) {
        printf("Fechando em modo Trasmitter\n");
        /* enviar DISC, esperar DISC e enviar UA e fecha conexao */
        sendSupFrame(fd, EM_CMD, DISC);
        // Esperar o UA
        if (!receiveFrame(fd, receivedFrame, EM_CMD, DISC, TRANSMITTER))
            exit(-1);
        sendSupFrame(fd, EM_CMD, UA);
        close(fd);
        printf("Trasmitter fechado com sucesso\n");
    } else { // RECEIVER
        /* espera DISC, envia DISC, espera UA e fecha conexao */
        printf("Fechando em modo Receiver\n");
        if (!receiveFrame(fd, receivedFrame, EM_CMD, DISC, RECEIVER))
            exit(-1);
        sendSupFrame(fd, EM_CMD, DISC);
        // Esperar o UA
        sendSupFrame(fd, EM_CMD, UA);
        close(fd);
        printf("Receiver fechado com sucesso\n");
    }
    return 1;
}


