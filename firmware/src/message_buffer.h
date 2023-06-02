#ifndef __MESSAGE_BUFFER_H_
#define __MESSAGE_BUFFER_H_

#include "radio.h"
#include <stdint.h>

#define MSG_PAYLOAD_SIZE 247

typedef struct {
  uint8_t len;
  uint16_t pkt_id;
} msg_header_t;

typedef struct {
  msg_header_t header;
  uint8_t data[MSG_PAYLOAD_SIZE];
} msg_t;

int msg_buf_init(void);
int msg_buf_insert(pkt_t *pkt);

/* Get a pointer to a packet for the device from the message buffer */
int msg_buf_get_claim(pkt_t **dst, uint32_t dev_id);
/* Let message buffer know that the packet was processed */
int msg_buf_get_finish(uint32_t dev_id);

#endif /* __MESSAGE_BUFFER_H_ */