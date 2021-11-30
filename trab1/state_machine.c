
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 256 * 256 // 64KB
#define BYTES_PER_PACKAGE 4 * 1024 // 4KB

#define TRANSMITTER 0
#define RECEIVER 1

#define INFO_LENGTH 4

//ADDRESS CONSTANTS
#define EM_CMD 0x03 //0b00000011

//CONTROL CONSTANTS
#define SET 0x03  //0b00000011 COMMAND
#define DISC 0x0b //0b00001011 COMMAND
#define UA 0x07   //0b00000111 ANSWER
#define RR(X) (X==0? 0b00000101 : 0b10000101) //ANSWER
#define RJ(X) (X==0? 0b00000001 : 0b10000001) //ANSWER
#define II(X) (X==0? 0b00000000 : 0b01000000) //ANSWER

//FRAME FLAG
#define FR_FLAG 0x7e
#define ESC_FLAG 0x7d
#define FR_SUB 0x5e
#define ESC_SUB 0x5d

//INDEXES
#define FLAG_IND 0
#define ADDR_IND 1
#define CTRL_IND 2
#define BCC_IND 3
#define END_FLAG_IND 4

#define DEBUG_READING_STATUS 0

unsigned char SEND_SEQ = II(0);
unsigned char REC_READY = RR(1);
unsigned char REC_REJECTED = RJ(1);

typedef enum SET_STATES {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK
} set_states;

extern int fail;

int verify = 0;

void sendSupFrame(int fd, unsigned char addr, unsigned char cmd) {
    unsigned char *frame = malloc(5);
    frame[FLAG_IND] = FR_FLAG;
    frame[CTRL_IND] = cmd;
    frame[ADDR_IND] = addr;
    frame[BCC_IND] = frame[ADDR_IND] ^ frame[CTRL_IND];
    frame[END_FLAG_IND] = FR_FLAG;
    write(fd, frame, 5);
    free(frame);
}

int receiveFrame(int fd, unsigned char *frame, unsigned char addr, unsigned char cmd, unsigned char mode) {
    unsigned char input;
    int res = 1;
    verify = 0;
    if ((mode == TRANSMITTER) && (cmd == UA || cmd == DISC)) {
        alarm(3);
    }

    int fail = 0;
    set_states set_machine = START;
    while (!verify) {
        if (read(fd, &input, sizeof(unsigned char)) == -1) {
            printf("\nTIMEOUT!\tRetrying connection!\n");
            return 0;
        }
        switch (set_machine) {
            case START:
                //printf("START STATE: %02x\n", input);
                frame[FLAG_IND] = input;
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;

                break;

            case FLAG_RCV:
                //printf("FLAG STATE: %02x\n", input);
                frame[ADDR_IND] = input;
                if (input == addr)
                    set_machine = A_RCV;
                else if (input != FR_FLAG)
                    set_machine = START;
                break;

            case A_RCV:
                //printf("A STATE: %02x\n", input);
                frame[CTRL_IND] = input;
                if (input == REC_READY) {
                    if (input == cmd) {
                        set_machine = C_RCV;
                        res = 2;
                    } else if (input == FR_FLAG)
                        set_machine = FLAG_RCV;
                    else
                        set_machine = START;
                    break;
                } else if (input == REC_REJECTED) {
                    if (REC_READY == cmd) {
                        set_machine = C_RCV;
                        res = 3;
                    } else if (input == FR_FLAG)
                        set_machine = FLAG_RCV;
                    else
                        set_machine = START;
                    break;
                } else {
                    if (input == cmd)
                        set_machine = C_RCV;
                    else if (input == FR_FLAG)
                        set_machine = FLAG_RCV;
                    else
                        set_machine = START;
                    break;
                }
            case C_RCV:
                //printf("C STATE: %02x\n", input);
                frame[BCC_IND] = input;

                if (input == (addr ^ (res == 2 ? cmd : (res == 3 ? REC_REJECTED : cmd)))) {
                    set_machine = BCC_OK;
                    break;
                }
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;
                else
                    set_machine = START;
                break;

            case BCC_OK:
                //printf("BCC STATE: %02x\n", input);
                frame[END_FLAG_IND] = input;
                if (input == FR_FLAG) {
                    alarm(0);
                    return res;
                } else
                    set_machine = START;
                break;
            default:
                break;
        }
    }
    if (!fail && !verify) {
        alarm(0);
        return res;
    }
    return res;

}

unsigned char *stuffing(unsigned char *fr, unsigned int *size) {
    unsigned char *result;
    unsigned int num_escapes = 0, new_size;
    for (size_t i = 0; i < *size; i++)
        if (fr[i] == FR_FLAG || fr[i] == ESC_FLAG)
            num_escapes++;
    new_size = *size + num_escapes;
    result = malloc(new_size);
    int offset = 0;
    for (size_t i = 0; i < *size; i++) {
        if (fr[i] == FR_FLAG) {
            result[i + offset] = ESC_FLAG;
            result[i + offset + 1] = FR_SUB;
            offset += 1;
        } else if (fr[i] == ESC_FLAG) {
            result[offset + i] = ESC_FLAG;
            result[1 + offset + i] = ESC_SUB;
            offset += 1;
        } else {
            result[(i) + offset] = fr[i];
        }
    }
    *size = new_size;
    return result;
}

unsigned char *destuffing(unsigned char *fr, unsigned int *size) {
    unsigned char *result;
    unsigned int num_escapes = 0, new_size;
    for (int i = 0; i < *size; i++) {
        if (i < (*size - 1)) {
            if (fr[i] == ESC_FLAG && fr[i + 1] == FR_SUB)
                num_escapes++;

            if (fr[i] == ESC_FLAG && fr[i + 1] == ESC_SUB)
                num_escapes++;
        }
    }
    new_size = *size - num_escapes;
    *size = new_size;
    result = malloc(new_size);
    int offset = 0;
    for (size_t i = 0; i < new_size;) {
        if (fr[i + offset] == ESC_FLAG && fr[i + 1 + offset] == FR_SUB) {
            result[i++] = FR_FLAG;
            offset += 1;
        } else if (fr[i + offset] == ESC_FLAG && fr[i + 1 + offset] == ESC_SUB) {
            result[i++] = ESC_FLAG;
            offset += 1;
        } else
            result[i++] = fr[i + offset];
    }
    return result;
}

int receive_info_frame(int fd, unsigned char *frame, unsigned int *total_size) {
    set_states set_machine = START;
    int res;
    verify = 0;
    int fail = 0;
    unsigned char input;
    unsigned int ind = 4, acc = 0, current = 0;
    while (!verify) {
        if (DEBUG_READING_STATUS == 1)
            printf("before info read...\n");
        if (read(fd, &input, sizeof(unsigned char)) == -1) {
            printf("\nTIMEOUT!\tRetrying connection!\n");
            return 0;
        }
        frame[set_machine] = input;
        switch (set_machine) {
            case START:
                //printf("START STATE: %02x\n", input);
                if (input == FR_FLAG) {
                    set_machine = FLAG_RCV;
                }
                break;

            case FLAG_RCV:
                //printf("FLAG STATE: %02x\n", input);
                if (input == EM_CMD)
                    set_machine = A_RCV;
                else if (input != FR_FLAG)
                    set_machine = START;
                break;

            case A_RCV:
                //printf("A STATE: %02x\n", input);
                if (input == SEND_SEQ)
                    set_machine = C_RCV;
                else if (input == FR_FLAG)
                    set_machine = FLAG_RCV;
                else
                    set_machine = START;
                break;

            case C_RCV:
                //printf("C STATE: %02x\n", input);
                if (input == (EM_CMD ^ SEND_SEQ)) {
                    set_machine = BCC_OK;
                    break;
                }
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;
                else
                    set_machine = START;
                break;


            default:
                //  printf("INFO: %02x\n", input);
                if (input == FR_FLAG) {
                    alarm(0);
                    verify = 1;
                    break;
                }
                set_machine++;
                acc = set_machine;
                break;
        }

    }
    *total_size = acc + 1;
    return 1;
}

unsigned char calculate_bcc2(unsigned char *data, unsigned int size) {
    unsigned char BCC2 = 0;
    for (int i = 0; i < size; i++) {
        BCC2 ^= data[i];
    }
    return BCC2;
}

unsigned char *retrieve_info_frame_data(unsigned char *frame, unsigned int frame_size, unsigned int *data_size) {
    *data_size = frame_size - 5;
    unsigned char *data = (unsigned char *) malloc(*data_size);
    data = memcpy(data, &frame[4], *data_size);
    return data;
}

int send_info_frame(int fd, unsigned char *frame, unsigned int size) {
    int res;
    res = write(fd, frame, size);
    //print_su_frame(frame,size);
    free(frame);
    return res;
}

void update_seq() {
    if (SEND_SEQ == II(0)) {
        SEND_SEQ = II(1);
        REC_READY = RR(0);
        REC_REJECTED = RJ(0);
    } else {
        SEND_SEQ = II(0);
        REC_READY = RR(1);
        REC_REJECTED = RJ(1);
    }
}





















