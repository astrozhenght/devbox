#ifndef __COMM_QUEUE_H
#define __COMM_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>

#define COMM_QUEUE_SIZE          512
#define COMM_TEXTAREA_PER_LINE   35  // 每行字符数。因为使用的非等宽字体，因此该数值是个经验值，以x为例得到35

typedef struct Comm_Queue {
    char buffer[COMM_QUEUE_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} Comm_Queue;


void     Comm_Queue_Init(Comm_Queue* q);
uint8_t  Comm_Queue_IsEmpty(Comm_Queue* q);
uint16_t Comm_Queue_GetDataLen(Comm_Queue* q);
void     Comm_Queue_Clear(Comm_Queue* q);
void     Comm_Queue_Enqueue(Comm_Queue* q, uint8_t* data, uint16_t length);
uint16_t Comm_Queue_Memcpy(Comm_Queue* q, uint8_t* dest);

#ifdef __cplusplus
}
#endif

#endif /* __COMM_QUEUE_H */
