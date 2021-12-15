
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

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
#define RR0 0b00000101 //ANSWER
#define RR1 0b10000101 //ANSWER
#define REJ0 0b00000001 //ANSWER
#define REJ1 0b10000001 //ANSWER
#define II0 0b00000000 //ANSWER
#define II1 0b01000000 //ANSWER

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

unsigned char SEND_SEQ = II0;
unsigned char REC_READY = RR1;
unsigned char REC_REJECTED = REJ1;

typedef enum SET_STATES {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK
} set_states;

int verify = 0;
int alarmFlag = 0;
int numAttempts = 0;

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

void alarmHandler(int sig){
    //printf( "[ALARM] Timeout\n");
    alarmFlag = 1;
    numAttempts++;
}


int receiveSupFrame(int fd, unsigned char *frame, unsigned char addr, unsigned char cmd, unsigned char mode) {
    unsigned char input;
    int res = 1;
    verify = 0;
    alarmFlag = 0;
    if ((mode == TRANSMITTER) && (cmd == UA || cmd == DISC)) {
        alarm(3);
    }

    int fail = 0;
    set_states set_machine = START;
    while (!verify) {
       // printf("Lendo sup frame ...\n");
        if (read(fd, &input, sizeof(unsigned char)) < 0 ) {
            printf("\nTIMEOUT!\tRetrying connection!\n");
            return 0;
        }
        if (alarmFlag == 1) {
            printf("\nAlarm!\tTentando novamente\n");
            alarmFlag = 0;
            alarm(0);
            return 0;
        }
        switch (set_machine) {
            case START:
                frame[FLAG_IND] = input;
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;

                break;

            case FLAG_RCV:
                frame[ADDR_IND] = input;
                if (input == addr)
                    set_machine = A_RCV;
                else if (input != FR_FLAG)
                    set_machine = START;
                break;

            case A_RCV:
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

unsigned char *stuffing(unsigned char *frame, unsigned int *size) {
    unsigned char *result;
    unsigned int escapes = 0, newSize;
    for (size_t i = 0; i < *size; i++)
        if (frame[i] == FR_FLAG || frame[i] == ESC_FLAG)
            escapes++;
    newSize = *size + escapes;
    result = malloc(newSize);
    int offset = 0;
    for (size_t i = 0; i < *size; i++) {
        if (frame[i] == FR_FLAG) {
            result[i + offset] = ESC_FLAG;
            result[i + offset + 1] = FR_SUB;
            offset += 1;
        } else if (frame[i] == ESC_FLAG) {
            result[offset + i] = ESC_FLAG;
            result[1 + offset + i] = ESC_SUB;
            offset += 1;
        } else {
            result[(i) + offset] = frame[i];
        }
    }
    *size = newSize;
    return result;
}

unsigned char *destuffing(unsigned char *frame, unsigned int *size) {
    unsigned char *result;
    unsigned int escapes = 0, newSize;
    for (int i = 0; i < *size; i++) {
        if (i < (*size - 1)) {
            if (frame[i] == ESC_FLAG && frame[i + 1] == FR_SUB)
                escapes++;

            if (frame[i] == ESC_FLAG && frame[i + 1] == ESC_SUB)
                escapes++;
        }
    }
    newSize = *size - escapes;
    *size = newSize;
    result = malloc(newSize);
    int offset = 0;
    for (size_t i = 0; i < newSize;) {
        if (frame[i + offset] == ESC_FLAG && frame[i + 1 + offset] == FR_SUB) {
            result[i++] = FR_FLAG;
            offset += 1;
        } else if (frame[i + offset] == ESC_FLAG && frame[i + 1 + offset] == ESC_SUB) {
            result[i++] = ESC_FLAG;
            offset += 1;
        } else
            result[i++] = frame[i + offset];
    }
    return result;
}

int receiveInfoFrame(int fd, unsigned char *frame, unsigned int *totalSize) {
    set_states setMachine = START;
    int res;
    verify = 0;
    int fail = 0;
    alarmFlag = 0;
    unsigned char input;
    unsigned int ind = 4, acc = 0, current = 0;
    while (!verify) {
       // printf("Lendo info frame ...\n");
        if (read(fd, &input, sizeof(unsigned char)) == -1) {
            printf("\nTIMEOUT!\tTentando reconectar!\n");
            return 0;
        }
        if (alarmFlag == 1) {
            printf("\nAlarm!\tTentando novamente\n");
            alarmFlag = 0;
            alarm(0);
            return 0;
        }
        frame[setMachine] = input;
        switch (setMachine) {
            case START:
                //printf("START STATE: %02x\n", input);
                if (input == FR_FLAG) {
                    setMachine = FLAG_RCV;
                }
                break;

            case FLAG_RCV:
                //printf("FLAG STATE: %02x\n", input);
                if (input == EM_CMD)
                    setMachine = A_RCV;
                else if (input != FR_FLAG)
                    setMachine = START;
                break;

            case A_RCV:
                //printf("A STATE: %02x\n", input);
                if (input == SEND_SEQ)
                    setMachine = C_RCV;
                else if (input == FR_FLAG)
                    setMachine = FLAG_RCV;
                else
                    setMachine = START;
                break;

            case C_RCV:
                //printf("C STATE: %02x\n", input);
                if (input == (EM_CMD ^ SEND_SEQ)) {
                    setMachine = BCC_OK;
                    break;
                }
                if (input == FR_FLAG)
                    setMachine = FLAG_RCV;
                else
                    setMachine = START;
                break;


            default:
                //  printf("INFO: %02x\n", input);
                if (input == FR_FLAG) {
                    alarm(0);
                    verify = 1;
                    break;
                }
                setMachine++;
                acc = setMachine;
                break;
        }

    }
    *totalSize = acc + 1;
    return 1;
}

unsigned char calculateBCC(unsigned char *data, unsigned int size) {
    unsigned char BCC = 0;
    for (int i = 0; i < size; i++) {
        BCC ^= data[i]; //Bitwise exclusive OR and assignment
    }
    return BCC;
}

void updateSeq() {
    if (SEND_SEQ == II0) {
        SEND_SEQ = II1;
        REC_READY = RR0;
        REC_REJECTED = REJ0;
    } else {
        SEND_SEQ = II0;
        REC_READY = RR1;
        REC_REJECTED = REJ1;
    }
}





















