#ifndef __RADIO_H_
#define __RADIO_H_

int radio_init();
int radio_start();

/* Max payload size is 255. We have 8 Byte protocol overhead. */
#define PKT_PAYLOAD_SIZE (255 - sizeof(pkt_header_t))

typedef struct __attribute__((packed)) {
  /* This is always the ID of the 'station' device sending or receiving the packet */
  uint32_t dev_id;
  /* ID of the packet */
  uint16_t pkt_id;
  /* ID of a previous packet that is acknowledged with this packet */
  uint16_t ack_id;
} pkt_header_t;

typedef struct __attribute__((packed)) {
  /* Lenght of the packet, excluding this one byte length field */
  uint8_t len;
  pkt_header_t hdr;
  /* Max payload size is 255. We have 8 Byte protocol overhead. */
  uint8_t data[PKT_PAYLOAD_SIZE];
} pkt_t;

int radio_msgq_get(pkt_t* pkt, k_timeout_t timeout);

#endif /* __RADIO_H_ */