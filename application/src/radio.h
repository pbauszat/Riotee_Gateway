#ifndef __RADIO_H_
#define __RADIO_H_

int radio_init();
int radio_start();

typedef struct __attribute__((packed)) {
  uint8_t len;
  uint32_t dev_id;
  uint16_t pkt_id;
  uint16_t acknowledgement_id;
  /* Max payload size is 255. We have 8 Byte protocol overhead. */
  uint8_t data[247];
} pkt_t;

extern struct k_msgq pkt_mq;

#endif /* __RADIO_H_ */