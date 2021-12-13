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

    newtio.c_cc[VTIME] = 1; /* inter-character timer unused */
    newtio.c_cc[VMIN] = 0;  /* no blocking  */

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

            //printf("Tentativa: %d\n", numAttempts);
            sendSupFrame(fd, EM_CMD, SET);

            // Esperar o UA
            if (!receiveSupFrame(fd, receivedFrame, EM_CMD, UA, TRANSMITTER)) continue;
            printf("Conexao estabelecida em modo Transmitter\n");
            break;
        }
    } else if (mode == RECEIVER) { // RECEIVER
        printf("Abrindo em modo Receiver\n");
        /* espera o SET e devolve o UA */
        while (TRUE) {
            if (!receiveSupFrame(fd, receivedFrame, EM_CMD, SET, RECEIVER)) continue;
            sendSupFrame(fd, EM_CMD, UA);
            printf("Conexao estabelecida em modo Receiver\n");
            break;
        }
    }
    signal(SIGALRM, alarmHandler);

    return fd;
}

int llclose(int fd) {
    unsigned char *receivedFrame = malloc(5);
    if (mode == TRANSMITTER) {
        printf("Fechando em modo Trasmitter\n");
        /* enviar DISC, esperar DISC e enviar UA e fecha conexao */
        while (TRUE) {
            sendSupFrame(fd, EM_CMD, DISC);
            // Esperar o UA
            if (!receiveSupFrame(fd, receivedFrame, EM_CMD, DISC, TRANSMITTER)) continue;
            break;
        }
        sendSupFrame(fd, EM_CMD, UA);
        close(fd);
        printf("Trasmitter fechado com sucesso\n");
    } else { // RECEIVER
        /* espera DISC, envia DISC, espera UA e fecha conexao */
        printf("Fechando em modo Receiver\n");
        while (TRUE) {
            if (!receiveSupFrame(fd, receivedFrame, EM_CMD, DISC, RECEIVER)) continue;
            break;
        }
        while (TRUE) {
            sendSupFrame(fd, EM_CMD, DISC);
            // Esperar o UA
            if (!receiveSupFrame(fd, receivedFrame, EM_CMD, UA, RECEIVER)) continue;
            break;
        }
        close(fd);
        printf("Receiver fechado com sucesso\n");
    }
    return 1;
}

int llwrite(int fd, char *buffer, int length) {
    int res;
    unsigned char bcc2 = calculateBCC(buffer, length);
    int bcc2_size = 1;
    int info_length = length + bcc2_size;
    unsigned char *info = malloc((info_length));
    memcpy(info, buffer, length);
    memcpy(&info[length], &bcc2, bcc2_size);
    unsigned char *stuffed_data = stuffing(info, &info_length);
    unsigned char *frameData = malloc(5);
    int sent = 0;
    int received_status;
    while (!sent) {
        while (numAttempts < 3) {
            unsigned char *frame = malloc((info_length + 5));
            frame[FLAG_IND] = FR_FLAG;
            frame[ADDR_IND] = EM_CMD;
            frame[CTRL_IND] = SEND_SEQ;
            frame[BCC_IND] = frame[ADDR_IND] ^ frame[CTRL_IND];
            memcpy(&frame[4], stuffed_data, info_length);
            frame[4 + info_length] = FR_FLAG;
            res = write(fd, frame, info_length + 5);
            free(frame);
            if (res < 0) {
                perror("Erro ao enviar frame\n");
            }
            alarm(3);
            received_status = receiveSupFrame(fd, frameData, EM_CMD, REC_READY, TRANSMITTER);
            alarm(0);
            if (received_status == 3) {
                printf("\nFrame rejeitado. Reenviando...\n");
                continue;
            } else if (received_status == 0) {
                continue;
            }
            if (!(frameData[CTRL_IND] == REC_READY))
                continue;
            sent = 1;
            numAttempts = 0;
            break;
        }
        if (numAttempts >= 3) {
            printf("Excedido numero maximo de tentativas\n");
            return -1;
        }
    }
    free(stuffed_data);
    free(frameData);
    free(info);
    updateSeq();
    return res;
}

int llread(int fd, char *buffer) {
    /* read() informacao (I) */
    unsigned char *frameData = malloc(
            BYTES_PER_PACKAGE * 2);
    unsigned char *final_frame;
    unsigned char *stuffed_data;
    unsigned char *destuffed_data;

    int rejected = 0;
    int real_size = 0, data_size;
    // reset();
    //int numAttempts = 0;
    while (numAttempts < 3) {
        rejected = 0;
        alarm(3);
        if (!receiveInfoFrame(fd, frameData, &real_size)) {
            alarm(0);
            sendSupFrame(fd, EM_CMD, REC_REJECTED);
            rejected = 1;
            continue;
        }
        alarm(0);
        unsigned char *final_frame = malloc(real_size);
        memcpy(final_frame, frameData, real_size);
        *&data_size = real_size - 5;
        unsigned char *data = (unsigned char *) malloc(*&data_size);
        stuffed_data = memcpy(data, &final_frame[4], *&data_size);
        destuffed_data = destuffing(stuffed_data, &data_size);
        unsigned char received_bcc2 = destuffed_data[data_size - 1];
        // Simula receptor nao envia resposta (20% de prob.)
        if (random() % 5 == 4) {
            printf("\n simulando erro de não enviar resposta\n");
            alarm(0);
            continue;
        }
        // Envia REJ com (20% de prob.)
        unsigned char calculated_bcc2 = random() % 5 == 1 ? 0 : calculateBCC(destuffed_data,data_size-1);
        //unsigned char calculated_bcc2 = calculateBCC(destuffed_data, data_size - 1);
        if (received_bcc2 != calculated_bcc2) {
            printf("\nBCC2 not recognized\n");
            rejected = 1;
            sendSupFrame(fd, EM_CMD, REC_REJECTED);
            continue;
        }
        sendSupFrame(fd, EM_CMD, REC_READY);
        if (!rejected) {
            numAttempts = 0;
            memcpy(buffer, destuffed_data, data_size - 1);
            free(frameData);
            free(final_frame);
            free(stuffed_data);
            free(destuffed_data);
            updateSeq();
            return data_size - 1;
        }
        return -2;
    }
    return -1;
}



