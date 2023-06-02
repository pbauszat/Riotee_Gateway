#include <zephyr/sys/ring_buffer.h>

#include "message_buffer.h"
#include "radio.h"

#define MAX_NUM_DEVICES 16
#define PKTS_PER_BUF 16

typedef struct {
  /* ID of recipient for this message queue. This is set to own ID when slot is not used. */
  uint32_t dev_id;
  struct ring_buf msg_buf;
  uint8_t msg_buf_data[PKTS_PER_BUF * sizeof(pkt_t)];
} dev_msg_buf_t;

dev_msg_buf_t buffers[MAX_NUM_DEVICES];

#define msg_buf_used(idx) (buffers[idx].dev_id != my_dev_id)
#define msg_buf_set_unused(dev_msg_buf_ptr) dev_msg_buf_ptr->dev_id = my_dev_id

/* Get pointer to the dev_msg_buf for the specified device ID */
static inline dev_msg_buf_t *get_dev_msg_buf(uint32_t dev_id) {
  for (unsigned int buf_idx = 0; buf_idx < MAX_NUM_DEVICES; buf_idx++) {
    if (dev_id == buffers[buf_idx].dev_id)
      return &buffers[buf_idx];
  }
  return NULL;
}

static inline dev_msg_buf_t *get_empty_msg_buf() {
  for (unsigned int buf_idx = 0; buf_idx < MAX_NUM_DEVICES; buf_idx++) {
    if (!msg_buf_used(buf_idx))
      return &buffers[buf_idx];
  }
  return NULL;
}

int msg_buf_init(void) {
  for (unsigned int i = 0; i < MAX_NUM_DEVICES; i++) {
    ring_buf_init(&buffers[i].msg_buf, PKTS_PER_BUF * sizeof(pkt_t), buffers[i].msg_buf_data);
    /* Set to own ID to indicate that this slot is not used */
    buffers[i].dev_id = my_dev_id;
  }
  return 0;
}

int msg_buf_insert(pkt_t *pkt) {
  dev_msg_buf_t *dev_msg_buf;

  /* Search for a buffer that is already in use for this device id */
  if ((dev_msg_buf = get_dev_msg_buf(pkt->dev_id)) == NULL) {
    /* No buffer in use? Get a new one. */
    if ((dev_msg_buf = get_empty_msg_buf()) == NULL)
      /* All buffers used*/
      return -1;
    /* Claim the empty buffer for this device id */
    dev_msg_buf->dev_id = pkt->dev_id;
  }

  /* Insert own device ID as sender into the packet */
  pkt->dev_id = my_dev_id;

  ring_buf_put(&dev_msg_buf->msg_buf, (uint8_t *)pkt, sizeof(pkt_t));
  return 0;
}

/* This gets called from a critical section within the radio ISR and should run as quickly as possible */
int msg_buf_get_claim(pkt_t **dst, uint32_t dev_id) {
  dev_msg_buf_t *dev_msg_buf;
  if ((dev_msg_buf = get_dev_msg_buf(dev_id)) == NULL)
    /* No messages available for this device id*/
    return 1;

  if (sizeof(pkt_t) != ring_buf_get_claim(&dev_msg_buf->msg_buf, (uint8_t **)dst, sizeof(pkt_t)))
    return -1;

  return 0;
}

int msg_buf_get_finish(uint32_t dev_id) {
  dev_msg_buf_t *dev_msg_buf;
  if ((dev_msg_buf = get_dev_msg_buf(dev_id)) == NULL)
    /* This should not happen, since we should have claimed before */
    return -1;

  ring_buf_get_finish(&dev_msg_buf->msg_buf, sizeof(pkt_t));

  if (ring_buf_is_empty(&dev_msg_buf->msg_buf))
    msg_buf_set_unused(dev_msg_buf);

  return 0;
}