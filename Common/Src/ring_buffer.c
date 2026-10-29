#include <string.h>     // memcpy()
#include "ring_buffer.h"

#define RING_BUFFER_DISCARD_CHUNK_SIZE  35U         // 缓冲区空间不足时，每次丢弃的字节数

void ring_buffer_init(ring_buffer_t *ring_buffer)
{
    if (ring_buffer == NULL)
    {
        return;
    }

    ring_buffer->head = 0U;
    ring_buffer->tail = 0U;
}

static uint8_t ring_buffer_is_empty(ring_buffer_t *ring_buffer)
{
    return (ring_buffer->head == ring_buffer->tail);
}

static uint16_t ring_buffer_size(ring_buffer_t *ring_buffer)
{
    if (ring_buffer->head >= ring_buffer->tail)
    {
        return (ring_buffer->head - ring_buffer->tail);
    }
    else
    {
        return (RING_BUFFER_SIZE - ring_buffer->tail + ring_buffer->head);
    }
}

void ring_buffer_clear(ring_buffer_t *ring_buffer)
{
    ring_buffer->tail = ring_buffer->head; // 队列空
}

// ring_buffer->head指向下一个要写入数据的位置，当前所处位置不存储数据
// RING_BUFFER_SIZE 大小的队列，规定最多只能存储 RING_BUFFER_SIZE-1 个数据
void ring_buffer_write(ring_buffer_t *ring_buffer, uint8_t* data, uint16_t length)
{
    if ((length == 0) || (data == NULL))
    {
        return;
    }

    // 只取最后 RING_BUFFER_SIZE-1 个数据
    if (length > (RING_BUFFER_SIZE - 1))
    {
        data += (length - RING_BUFFER_SIZE + 1);
        length = RING_BUFFER_SIZE - 1;
    }

    // 丢弃最早的数据
    int16_t data_len = ring_buffer_size(ring_buffer);
    while (length > RING_BUFFER_SIZE - 1 - data_len) // length > 剩余可填充数据个数
    {
        ring_buffer->tail = (ring_buffer->tail + RING_BUFFER_DISCARD_CHUNK_SIZE) % RING_BUFFER_SIZE;
        data_len -= RING_BUFFER_DISCARD_CHUNK_SIZE; // 目的是删除一行数据
        if (data_len < 0)
        {
            ring_buffer->tail = ring_buffer->head; // 队列空
        }
    }

    for (uint16_t i = 0; i < length; i++)
    {
        ring_buffer->buffer[ring_buffer->head] = data[i];   // 填入数据
        if (((ring_buffer->head + 1) % RING_BUFFER_SIZE) == ring_buffer->tail) // 填入数据后队列满
        {
            ++ring_buffer->tail; // 丢弃最早的数据
        }
        ++ring_buffer->head; // 指针右移
        if (ring_buffer->head >= RING_BUFFER_SIZE) // 环形队列处理
        {
            ring_buffer->head = 0;
        }
        if (ring_buffer->tail >= RING_BUFFER_SIZE)
        {
            ring_buffer->tail = 0;
        }
    }
}

// 返回有效数据的长度
// head指向的数据丢弃处理，原因是方便条件2和3逻辑的处理
uint16_t ring_buffer_copy(ring_buffer_t *ring_buffer, uint8_t* dest)
{
    uint16_t data_len = ring_buffer_size(ring_buffer);

    if (ring_buffer_is_empty(ring_buffer) || (dest == NULL))
    {
        return 0;
    }
    else if (ring_buffer->head > ring_buffer->tail) // head指向的数据丢弃处理，包括tail-x-x-x-head
    {
        memcpy((void*)dest, (const void*)(ring_buffer->buffer + ring_buffer->tail), data_len);
    }
    else // head < tail，x-head-x-tail-x-x
    {
        uint32_t tail_len = RING_BUFFER_SIZE - ring_buffer->tail;
        memcpy((void*)dest, (const void*)(ring_buffer->buffer + ring_buffer->tail), tail_len);
        memcpy((void*)(dest + tail_len), (const void*)(ring_buffer->buffer), data_len - tail_len);
    }

    return data_len;
}



