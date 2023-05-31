#include <nrf.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/kernel.h>

#include "message_buffer.h"

pkt_t rx_pkt;
pkt_t tx_pkt;

enum {
  /* Uplink logical address index */
  LA_UPLINK_IDX = 1,
  /* Downlink logical address index */
  LA_DOWNLINK_IDX = 2,
};

enum {
  /* Uplink logical address index */
  LA_UPLINK = 0x5D,
  /* Downlink logical address index */
  LA_DOWNLINK = 0xF7,
};

enum { RADIO_EVT_CRCOK = 0x01 };

K_EVENT_DEFINE(radio_evt);

#define PKT_MQ_CAPACITY 16
static char __aligned(4) pkt_mq_buffer[PKT_MQ_CAPACITY * sizeof(pkt_t)];
struct k_msgq pkt_mq;

void radio_isr(void) {

  if ((NRF_RADIO->EVENTS_TXREADY == 1) &&
      (NRF_RADIO->INTENSET & RADIO_INTENSET_TXREADY_Msk)) {
    NRF_RADIO->EVENTS_TXREADY = 0;

    /* Set shorts for turning around to RX */
    NRF_RADIO->SHORTS &= ~RADIO_SHORTS_DISABLED_TXEN_Msk;
    NRF_RADIO->SHORTS |= RADIO_SHORTS_DISABLED_RXEN_Msk;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
  }
  if ((NRF_RADIO->EVENTS_RXREADY == 1) &&
      (NRF_RADIO->INTENSET & RADIO_INTENSET_RXREADY_Msk)) {
    NRF_RADIO->EVENTS_RXREADY = 0;

    /* Set shorts for turning around to TX to send acknowledgement */
    NRF_RADIO->SHORTS &= ~RADIO_SHORTS_DISABLED_RXEN_Msk;
    NRF_RADIO->SHORTS |= RADIO_SHORTS_DISABLED_TXEN_Msk;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET =
        RADIO_INTENSET_CRCOK_Msk | RADIO_INTENSET_CRCERROR_Msk;
  }
  /* Transmission of acknowledgement has finished */
  if ((NRF_RADIO->EVENTS_END == 1) &&
      (NRF_RADIO->INTENSET & RADIO_INTENSET_END_Msk)) {
    NRF_RADIO->EVENTS_END = 0;

    /* Prepare for listening again */
    NRF_RADIO->PACKETPTR = (uint32_t)&rx_pkt;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_RXREADY_Msk;
  }
  /* Valid packet received */
  if ((NRF_RADIO->EVENTS_CRCOK == 1) &&
      (NRF_RADIO->INTENSET & RADIO_INTENSET_CRCOK_Msk)) {
    NRF_RADIO->EVENTS_CRCOK = 0;

#if 0
    msg_header_t msg_header;
    /* Copy */
    if (msg_buf_get(&msg_header, tx_pkt.data, rx_pkt.dev_id) == 0) {
      tx_pkt.len = 8 + msg_header.len;
      tx_pkt.pkt_id = msg_header.pkt_id;
    } else {
      tx_pkt.len = 8;
      tx_pkt.pkt_id = 0xFFFF;
    }
#endif
    NRF_RADIO->PACKETPTR = (uint32_t)&tx_pkt;
    /* TODO: Insert Packet ID of received packet into acknowledgement */
    tx_pkt.acknowledgement_id = rx_pkt.pkt_id;
    tx_pkt.dev_id = 0xFFFFFFFF;
    tx_pkt.len = 8;
    tx_pkt.pkt_id = 0xAAAA;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_TXREADY_Msk;

    /* Notify application that it can pick up the received packet */
    k_event_set(&radio_evt, RADIO_EVT_CRCOK);
  }
  /* Bad CRC -> Abort transmission of Acknowledgement*/
  if ((NRF_RADIO->EVENTS_CRCERROR == 1) &&
      (NRF_RADIO->INTENSET & RADIO_INTENSET_CRCERROR_Msk)) {
    NRF_RADIO->EVENTS_CRCERROR = 0;

    /* Set shorts for turning around to RX */
    NRF_RADIO->SHORTS &= ~RADIO_SHORTS_DISABLED_TXEN_Msk;
    NRF_RADIO->SHORTS |= RADIO_SHORTS_DISABLED_RXEN_Msk;

    /* Disable radio to start listening again */
    NRF_RADIO->TASKS_DISABLE = 1;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_RXREADY_Msk;
  }
}

int radio_start() {
  if ((NRF_CLOCK->HFCLKSTAT &
       (CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos)) == 0) {
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;

    NRF_CLOCK->TASKS_HFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {
    };
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  }

  NRF_RADIO->PACKETPTR = (uint32_t)&rx_pkt;
  NRF_RADIO->INTENSET = RADIO_INTENSET_RXREADY_Msk;
  NRF_RADIO->TASKS_RXEN = 1;
  return 0;
}

int radio_init() {
  /* 0dBm TX power */
  NRF_RADIO->TXPOWER =
      (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos);
  /* 2450 MHz frequency */
  NRF_RADIO->FREQUENCY = 76UL;
  /* BLE 2MBit */
  NRF_RADIO->MODE = (RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos);
  /* Fast radio rampup */
  NRF_RADIO->MODECNF0 = (RADIO_MODECNF0_RU_Fast << RADIO_MODECNF0_RU_Pos);

  /* We'll only use base address 1, i.e. logical addresses 1-7 */
  NRF_RADIO->BASE1 = 0xFB235D41;

  NRF_RADIO->PREFIX0 = (LA_DOWNLINK << 16) | (LA_UPLINK << 8);
  /* We want to receive on both, downlink and uplink message addresses */
  NRF_RADIO->RXADDRESSES = (1UL << LA_UPLINK_IDX);

  /* Make interframe spacing slightly longer than turnaround time (~40us) */
  NRF_RADIO->TIFS = 60;

  /* No S0, LEN and S1 fields */
  NRF_RADIO->PCNF0 = (0 << RADIO_PCNF0_S1LEN_Pos) |
                     (0 << RADIO_PCNF0_S0LEN_Pos) |
                     (8 << RADIO_PCNF0_LFLEN_Pos) |
                     (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);

  /* No whitening, little endian, 2B base address, 4B payload */
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                     (2 << RADIO_PCNF1_BALEN_Pos) |
                     (0 << RADIO_PCNF1_STATLEN_Pos) |
                     (255 << RADIO_PCNF1_MAXLEN_Pos);

  /* One byte CRC */
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos);
  NRF_RADIO->CRCINIT = 0xABUL;
  NRF_RADIO->CRCPOLY = 0x108UL;

  NRF_RADIO->TXADDRESS = LA_DOWNLINK_IDX;

  /* Set default shorts */
  NRF_RADIO->SHORTS =
      RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;

  NRF_RADIO->INTENCLR = 0xFFFFFFFF;

  IRQ_DIRECT_CONNECT(RADIO_IRQn, 0, radio_isr, 0);
  irq_enable(RADIO_IRQn);

  return 0;
}

/* This thread copies received packets into a message queue for processing */
void radio_handler() {

  radio_init();
  radio_start();

  const struct device *dev;
  dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
  gpio_pin_configure(dev, 31, GPIO_OUTPUT_ACTIVE);

  k_msgq_init(&pkt_mq, pkt_mq_buffer, sizeof(pkt_t), PKT_MQ_CAPACITY);

  while (1) {
    k_event_wait(&radio_evt, RADIO_EVT_CRCOK, true, K_FOREVER);
    k_msgq_put(&pkt_mq, &rx_pkt, K_NO_WAIT);
  }
}

K_THREAD_DEFINE(radio, 1024, radio_handler, NULL, NULL, NULL, 6, 0, 0);