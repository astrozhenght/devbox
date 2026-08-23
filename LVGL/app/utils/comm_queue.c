#include <stdint.h>
#include "comm_queue.h"

void Comm_Queue_Init(Comm_Queue* q)
{
    q->head = 0;
    q->tail = 0;
}

uint8_t Comm_Queue_IsEmpty(Comm_Queue* q)
{
    return (q->head == q->tail);
}

uint16_t Comm_Queue_GetDataLen(Comm_Queue* q)
{
    if (q->head >= q->tail)
    {
        return (q->head - q->tail);
    }
    else
    {
        return (COMM_QUEUE_SIZE - q->tail + q->head);
    }
}

void Comm_Queue_Clear(Comm_Queue* q)
{
    q->tail = q->head; // 队列空
}

// q->head指向下一个要写入数据的位置，当前所处位置不存储数据
// COMM_QUEUE_SIZE大小的队列，规定最多只能存储COMM_QUEUE_SIZE-1个数据
void Comm_Queue_Enqueue(Comm_Queue* q, uint8_t* data, uint16_t length)
{
    if ((length == 0) || (data == NULL))
    {
        return;
    }

    // 只取最后COMM_QUEUE_SIZE-1个数据
    if (length > (COMM_QUEUE_SIZE - 1))
    {
        data += (length - COMM_QUEUE_SIZE + 1);
        length = COMM_QUEUE_SIZE - 1;
    }

    // 丢弃最早的数据
    int16_t data_len = Comm_Queue_GetDataLen(q);
    while (length > COMM_QUEUE_SIZE - 1 - data_len) // length > 剩余可填充数据个数
    {
        q->tail = (q->tail + COMM_TEXTAREA_PER_LINE) % COMM_QUEUE_SIZE;
        data_len -= COMM_TEXTAREA_PER_LINE; // 目的是删除一行数据
        if (data_len < 0)
        {
            q->tail = q->head; // 队列空
        }
    }

    for (uint16_t i = 0; i < length; i++)
    {
        q->buffer[q->head] = data[i];   // 填入数据
        if (((q->head + 1) % COMM_QUEUE_SIZE) == q->tail) // 填入数据后队列满
        {
            ++q->tail; // 丢弃最早的数据
        }
        ++q->head; // 指针右移
        if (q->head >= COMM_QUEUE_SIZE) // 环形队列处理
        {
            q->head = 0;
        }
        if (q->tail >= COMM_QUEUE_SIZE)
        {
            q->tail = 0;
        }
    }
}

// 返回有效数据的长度
// head指向的数据丢弃处理，原因是方便条件2和3逻辑的处理
uint16_t Comm_Queue_Memcpy(Comm_Queue* q, uint8_t* dest)
{
    uint16_t data_len = Comm_Queue_GetDataLen(q);
    if (Comm_Queue_IsEmpty(q) || (dest == NULL))
    {
        return 0;
    }
    else if (q->head > q->tail) // head指向的数据丢弃处理，包括tail-x-x-x-head
    {
        memcpy((void*)dest, (const void*)(q->buffer + q->tail), data_len);
    }
    else // head < tail，x-head-x-tail-x-x
    {
        uint32_t tail_len = COMM_QUEUE_SIZE - q->tail;
        memcpy((void*)dest, (const void*)(q->buffer + q->tail), tail_len);
        memcpy((void*)(dest + tail_len), (const void*)(q->buffer), data_len - tail_len);
    }
    return data_len;
}



