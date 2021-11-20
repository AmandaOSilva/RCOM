
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 64000
#define PORT_SIZE 20
#define TRANSMITTER 0
#define RECEIVER 1
#define HEADER_SIZE 6
#define BYTES_PER_PACKAGE 4500      // [Maximum] number of bytes per package sent
#define INFO_LENGTH 4

//ADDRESS CONSTANTS
#define EM_CMD 0x03 //0b00000011
#define RE_CMD 0x01 //

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

typedef unsigned char uc;

uc sender_seq = II(0);
uc r_ready = RR(1);
uc r_rej = RJ(1);

typedef enum SET_STATES {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK
} set_states;

extern int fail;

int verify = 0;

void print_su_frame(uc *fr, unsigned int size) {
    for (int i = 0; i < size; i++) {
        printf("%02x", fr[i]);
    }
    printf("--");
    for (int i = 0; i < size; i++) {
        printf("%c", fr[i]);
    }
    printf("\n");
}

uc *create_su_frame(uc addr, uc cmd) {
    uc *frame = malloc(sizeof(uc) * 5);
    frame[FLAG_IND] = FR_FLAG;
    frame[CTRL_IND] = cmd;
    frame[ADDR_IND] = addr;
    frame[BCC_IND] = frame[ADDR_IND] ^ frame[CTRL_IND];
    frame[END_FLAG_IND] = FR_FLAG;
    return frame;
}

void send_su_frame(int fd, uc *frame) {
    write(fd, frame, sizeof(uc) * 5);
    free(frame);
}

int check_su_frame(uc *frame, uc ctrl) {
    return frame[CTRL_IND] == ctrl;
}

int receive_su_frame(int fd, uc *frame, uc addr, uc cmd, uc mode) {
    uc input;
    int res = 1;
    verify = 0;
    if ((mode == TRANSMITTER) && (cmd == UA || cmd == DISC)) {
        alarm(3);
    }

    int fail = 0;
    set_states set_machine = START;
    while (!verify) {
        if (read(fd, &input, sizeof(uc)) == -1) {
            printf("\nTIMEOUT!\tRetrying connection!\n");
            return 0;
        }
        switch (set_machine) {
            case START:
                printf("START STATE: %02x\n", input);
                frame[FLAG_IND] = input;
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;

                break;

            case FLAG_RCV:
                printf("FLAG STATE: %02x\n", input);
                frame[ADDR_IND] = input;
                if (input == addr)
                    set_machine = A_RCV;
                else if (input != FR_FLAG)
                    set_machine = START;
                break;

            case A_RCV:
                printf("A STATE: %02x\n", input);
                frame[CTRL_IND] = input;
                if (input == r_ready) {
                    if (input == cmd) {
                        set_machine = C_RCV;
                        res = 2;
                    } else if (input == FR_FLAG)
                        set_machine = FLAG_RCV;
                    else
                        set_machine = START;
                    break;
                } else if (input == r_rej) {
                    if (r_ready == cmd) {
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
                printf("C STATE: %02x\n", input);
                frame[BCC_IND] = input;

                if (input == (addr ^ (res == 2 ? cmd : (res == 3 ? r_rej : cmd)))) {
                    set_machine = BCC_OK;
                    break;
                }
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;
                else
                    set_machine = START;
                break;

            case BCC_OK:
                printf("BCC STATE: %02x\n", input);
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

uc *execute_stuffing(uc *fr, unsigned int *size) {
    uc *result;
    unsigned int num_escapes = 0, new_size;
    for (size_t i = 0; i < *size; i++)
        if (fr[i] == FR_FLAG || fr[i] == ESC_FLAG)
            num_escapes++;
    new_size = *size + num_escapes;
    result = malloc(sizeof(uc) * new_size);
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

uc *execute_destuffing(uc *fr, unsigned int *size) {
    uc *result;
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
    result = malloc(sizeof(uc) * new_size);
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

uc *create_information_plot(uc ctrl, uc *data, int length) {
    uc *frame = malloc(sizeof(uc) * (length + 5));
    frame[FLAG_IND] = FR_FLAG;
    frame[ADDR_IND] = EM_CMD;
    frame[CTRL_IND] = ctrl;
    frame[BCC_IND] = frame[ADDR_IND] ^ frame[CTRL_IND];
    memcpy(&frame[4], data, length);
    frame[4 + length] = FR_FLAG;
    return frame;
}

int receive_info_frame(int fd, uc *frame, unsigned int *total_size) {
    set_states set_machine = START;
    int res;
    verify = 0;
    int fail = 0;
    uc input;
    unsigned int ind = 4, acc = 0, current = 0;
    while (!verify) {
        if (DEBUG_READING_STATUS == 1)
            printf("before info read...\n");
        if (read(fd, &input, sizeof(uc)) == -1) {
            printf("\nTIMEOUT!\tRetrying connection!\n");
            return 0;
        }
        frame[set_machine] = input;
        switch (set_machine) {
            case START:
                printf("START STATE: %02x\n", input);
                if (input == FR_FLAG) {
                    set_machine = FLAG_RCV;
                }
                break;

            case FLAG_RCV:
                printf("FLAG STATE: %02x\n", input);
                if (input == EM_CMD)
                    set_machine = A_RCV;
                else if (input != FR_FLAG)
                    set_machine = START;
                break;

            case A_RCV:
                printf("A STATE: %02x\n", input);
                if (input == sender_seq)
                    set_machine = C_RCV;
                else if (input == FR_FLAG)
                    set_machine = FLAG_RCV;
                else
                    set_machine = START;
                break;

            case C_RCV:
                printf("C STATE: %02x\n", input);
                if (input == (EM_CMD ^ sender_seq)) {
                    set_machine = BCC_OK;
                    break;
                }
                if (input == FR_FLAG)
                    set_machine = FLAG_RCV;
                else
                    set_machine = START;
                break;


            default:
                printf("INFO: %02x\n", input);
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

uc calculate_bcc2(uc *data, unsigned int size) {
    uc BCC2 = 0;
    for (int i = 0; i < size; i++) {
        BCC2 ^= data[i];
    }
    return BCC2;
}

int check_bcc(uc candidate, uc *frame) {
    return candidate == frame[BCC_IND];
}

int check_bcc2(uc candidate, uc *data, unsigned int size) {
    uc BCC2 = calculate_bcc2(data, size);
    return BCC2 == candidate;
}

uc *retrieve_info_frame_data(uc *frame, unsigned int frame_size, unsigned int *data_size) {
    *data_size = frame_size - 5;
    uc *data = (unsigned char *) malloc(*data_size);
    data = memcpy(data, &frame[4], *data_size);
    return data;
}

int send_info_frame(int fd, uc *frame, unsigned int size) {
    int res;
    res = write(fd, frame, sizeof(uc) * size);
    //print_su_frame(frame,size);
    free(frame);
    return res;
}

uc *fit_frame(uc *prev_frame, unsigned int total_size) {
    uc *result = malloc(sizeof(uc) * total_size);
    memcpy(result, prev_frame, sizeof(uc) * total_size);
    return result;
}

void update_seq() {
    if (sender_seq == II(0)) {
        sender_seq = II(1);
        r_ready = RR(0);
        r_rej = RJ(0);
    } else {
        sender_seq = II(0);
        r_ready = RR(1);
        r_rej = RJ(1);
    }
}





















