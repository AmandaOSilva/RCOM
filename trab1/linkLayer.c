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

            numAttempts++;

            printf("Abrindo em modo Transmitter\n");
            if (numAttempts > 3) {
                printf("Nao foi possivel estabelecer conexao\n");
                return -1;
            }

            printf("Tentativa: %d\n", numAttempts);
            send_su_frame(fd, create_su_frame(EM_CMD, SET));
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
    int res;
    //struct sigaction sa,old;
    //sigemptyset(&sa.sa_mask);
    //sa.sa_handler = alarm_handler;
    //sa.sa_flags = 0;
    //sigaction(SIGALRM, &sa, &old);
    //reset();
    uc bcc2 = calculate_bcc2(buffer, length);
    int bcc2_size = 1;
    int info_length = length + bcc2_size;
    uc *info = malloc(sizeof(uc) * (info_length));
    memcpy(info, buffer, length);
    memcpy(&info[length], &bcc2, sizeof(uc) * bcc2_size);
    uc *stuffed_data = execute_stuffing(info, &info_length);
    uc *frameData = malloc(sizeof(uc) * 5);
    int sent = 0;
    int received_status;
    int numAttempts = 0;
    while (!sent) {
        while (numAttempts < 3) {
            numAttempts++;
            if ((res = send_info_frame(fd, create_information_plot(sender_seq, stuffed_data, info_length),
                                       info_length + 5)) < 0) {
                perror("Error while writing frame\n");
            }
            alarm(3);
            received_status = receive_su_frame(fd, frameData, EM_CMD, r_ready, TRANSMITTER);
            alarm(0);
            if (received_status == 3) {
                printf("\nRejected frame! Resending...\n");
                continue;
            } else if (received_status == 0) {
                continue;
            }
            if (!check_su_frame(frameData, r_ready))
                continue;
            sent = 1;
            break;
        }
        if (numAttempts >= 3) {
            printf("Maximum attempts exceeded!\n");
            return -1;
        }
    }
    free(stuffed_data);
    free(frameData);
    free(info);
    //sigaction(SIGALRM,&old,NULL);
    update_seq();
    return res;

}

int llread(int fd, char *buffer) {
    /* read() informacao (I) */
    uc *frameData = (uc *) malloc(
            sizeof(uc) * (BYTES_PER_PACKAGE + INFO_LENGTH) * 2), *final_frame, *stuffed_data, *destuffed_data;

    int rejected = 0;
    int real_size = 0, data_size;//,rand;
    uc *ready_frame = create_su_frame(EM_CMD, r_ready);
    // reset();
    int numAttempts = 0;
    while (numAttempts < 3) {
        //rand = random()%2;
        rejected = 0;
        alarm(3);
        if (!receive_info_frame(fd, frameData, &real_size)) {
            alarm(0);
            send_su_frame(fd, create_su_frame(EM_CMD, r_rej));
            rejected = 1;
            continue;
        }
        alarm(0);
        final_frame = fit_frame(frameData, real_size);
        stuffed_data = retrieve_info_frame_data(final_frame, real_size, &data_size);
        destuffed_data = execute_destuffing(stuffed_data, &data_size);
        uc received_bcc2 = destuffed_data[data_size - 1];
//        uc calculated_bcc2 = rand == 1 ? 0 : calculate_bcc2(destuffed_data,data_size-1);
        uc calculated_bcc2 = calculate_bcc2(destuffed_data, data_size - 1);
        if (received_bcc2 != calculated_bcc2) {
            printf("\nBCC2 not recognized\n");
            rejected = 1;
            send_su_frame(fd, create_su_frame(EM_CMD, r_rej));
            continue;
        }
        send_su_frame(fd, ready_frame);
        if (!rejected) {
            memcpy(buffer, destuffed_data, data_size - 1);
            free(frameData);
            free(final_frame);
            free(stuffed_data);
            free(destuffed_data);
            update_seq();
            return data_size - 1;
        }
        return -2;
    }
    return -1;
}

int llclose(int fd) {
    uc *receivedFrame = malloc(sizeof(uc) * 5);
    if (mode == TRANSMITTER) {
        printf("Fechando em modo Trasmitter\n");
        /* enviar DISC, esperar DISC e enviar UA e fecha conexao */
        send_su_frame(fd, create_su_frame(EM_CMD, DISC));
        // Esperar o UA
        if (!receive_su_frame(fd, receivedFrame, EM_CMD, DISC, TRANSMITTER))
            exit(-1);
        send_su_frame(fd, create_su_frame(EM_CMD, UA));
        close(fd);
        printf("Trasmitter fechado com sucesso\n");
    } else { // RECEIVER
        /* espera DISC, envia DISC, espera UA e fecha conexao */
        printf("Fechando em modo Receiver\n");
        if (!receive_su_frame(fd, receivedFrame, EM_CMD, DISC, RECEIVER))
            exit(-1);
        send_su_frame(fd, create_su_frame(EM_CMD, DISC));
        // Esperar o UA
        send_su_frame(fd, create_su_frame(EM_CMD, UA));
        close(fd);
        printf("Receiver fechado com sucesso\n");
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

    //strncpy(buffer, "Amanda", 6);
    if (modo) {
        char *buffer = "Ama~nda";
        llwrite(fd, buffer, 7);
    } else {
        char *buffer;
        llread(fd, buffer);
        printf("Recebido: %s", buffer);
    }
    llclose(fd);
    return 1;
}

