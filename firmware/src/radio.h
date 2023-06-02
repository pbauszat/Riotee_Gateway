#ifndef __RADIO_H_
#define __RADIO_H_

int radio_init();
int radio_start();

/* Max payload size is 255. We have 8 Byte protocol overhead. */
#define PKT_PAYLOAD_SIZE 247

typedef struct __attribute__((packed)) {
  uint8_t len;
  uint32_t dev_id;
  uint16_t pkt_id;
  uint16_t acknowledgement_id;
  uint8_t data[PKT_PAYLOAD_SIZE];
} pkt_t;

int radio_msgq_get(pkt_t* pkt, k_timeout_t timeout);

extern uint32_t my_dev_id;

#endif /* __RADIO_H_ */