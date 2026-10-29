#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RING_BUFFER_SIZE            512

typedef struct {
    char buffer[RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} ring_buffer_t;

void     ring_buffer_init(ring_buffer_t *ring_buffer);
void     ring_buffer_clear(ring_buffer_t *ring_buffer);
void     ring_buffer_write(ring_buffer_t *ring_buffer, uint8_t* data, uint16_t length);
uint16_t ring_buffer_copy(ring_buffer_t *ring_buffer, uint8_t* dest);

#ifdef __cplusplus
}
#endif

#endif /* __RING_BUFFER_H__ */
