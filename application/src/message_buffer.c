#include <zephyr/sys/ring_buffer.h>

#include "message_buffer.h"

#define MAX_NUM_DEVICES 16
#define PKTS_PER_BUF 16

typedef struct {
  /* Is this slot currently in use? */
  bool used;
  /* ID of recipient */
  uint32_t dev_id;
  struct ring_buf msg_buf;
  uint8_t msg_buf_data[PKTS_PER_BUF * sizeof(msg_t)];
} dev_msg_buf_t;

dev_msg_buf_t buffers[MAX_NUM_DEVICES];

int msg_buf_init(void) {
  for (unsigned int i = 0; i < MAX_NUM_DEVICES; i++) {
    buffers[i].used = false;
    ring_buf_init(&buffers[i].msg_buf, PKTS_PER_BUF * sizeof(msg_t),
                  buffers[i].msg_buf_data);
  }
  return 0;
}

int msg_buf_insert(uint32_t dev_id, msg_t *msg) {

  int buf_idx;
  /* Search for an existing buffer for this device id */
  for (buf_idx = 0; buf_idx < MAX_NUM_DEVICES; buf_idx++) {
    if (buffers[buf_idx].used && (buffers[buf_idx].dev_id == dev_id))
      break;
  }

  if (buf_idx == MAX_NUM_DEVICES) {
    /* Search for an unused buffer slot */
    for (buf_idx = 0; buf_idx < MAX_NUM_DEVICES; buf_idx++) {
      if (!buffers[buf_idx].used)
        break;
    }
    if (buf_idx == MAX_NUM_DEVICES)
      return -1;
  }

  ring_buf_put(&buffers[buf_idx].msg_buf, (uint8_t *)msg, sizeof(msg_t));
  return 0;
}

int msg_buf_get(msg_header_t *hdr_dst, uint8_t *data_dst, uint32_t dev_id) {
  unsigned int i;
  /* Search buffers for device ID */
  for (i = 0; i < MAX_NUM_DEVICES; i++) {
    if (!buffers[i].used)
      continue;
    if (buffers[i].dev_id == dev_id) {
      if (ring_buf_get(&buffers[i].msg_buf, (uint8_t *)hdr_dst,
                       sizeof(msg_header_t)) != sizeof(msg_header_t))
        return -1;
      if (ring_buf_get(&buffers[i].msg_buf, data_dst, MSG_PAYLOAD_SIZE) !=
          MSG_PAYLOAD_SIZE)
        return -1;
      return 0;
    }
  }
  /* Device ID not found */
  return 1;
}