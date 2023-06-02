#include <nrf.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/kernel.h>

#include "message_buffer.h"

#define PKT_MQ_CAPACITY 16
#define RADIO_STACK_SIZE 1024

K_MSGQ_DEFINE(pkt_mq, sizeof(pkt_t), PKT_MQ_CAPACITY, 4);

/* Structure for the radio handler thread that fills the pkt_mq */
K_THREAD_STACK_DEFINE(radio_thread_stack, RADIO_STACK_SIZE);
struct k_thread radio_thread_data;

/* Stores the device ID of the gateway */
uint32_t my_dev_id;

/* DMA buffer for incoming radio packets */
static pkt_t rx_pkt;

/* Buffer for outgoing acknowledgement packets in case no other packets are to be sent */
static pkt_t ack_only_pkt;

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

/* Used to signal from the radio ISR to the handler thread */
enum {
  RADIO_EVT_CRCOK = (1UL << 0),
  RADIO_EVT_CLAIM = (1UL << 1),
  RADIO_EVT_END = (1UL << 2),
};

K_EVENT_DEFINE(radio_evt);

static void radio_isr(void);

int radio_init() {
  /* 0dBm TX power */
  NRF_RADIO->TXPOWER = (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos);
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
  NRF_RADIO->PCNF0 = (0 << RADIO_PCNF0_S1LEN_Pos) | (0 << RADIO_PCNF0_S0LEN_Pos) | (8 << RADIO_PCNF0_LFLEN_Pos) |
                     (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);

  /* No whitening, little endian, 2B base address, 4B payload */
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) | (2 << RADIO_PCNF1_BALEN_Pos) |
                     (0 << RADIO_PCNF1_STATLEN_Pos) | (255 << RADIO_PCNF1_MAXLEN_Pos);

  /* One byte CRC */
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos);
  NRF_RADIO->CRCINIT = 0xABUL;
  NRF_RADIO->CRCPOLY = 0x108UL;

  NRF_RADIO->TXADDRESS = LA_DOWNLINK_IDX;

  /* Set default shorts */
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;

  NRF_RADIO->INTENCLR = 0xFFFFFFFF;

  IRQ_DIRECT_CONNECT(RADIO_IRQn, 0, radio_isr, 0);
  irq_enable(RADIO_IRQn);

  /* Device ID of the gateway */
  my_dev_id = NRF_FICR->DEVICEID[0];

  /* Prepare empty acknowledgement packet */
  ack_only_pkt.len = 8;
  ack_only_pkt.pkt_id = 0xFFFF;
  ack_only_pkt.dev_id = my_dev_id;

  return 0;
}

static void radio_isr(void) {
  if ((NRF_RADIO->EVENTS_TXREADY == 1) && (NRF_RADIO->INTENSET & RADIO_INTENSET_TXREADY_Msk)) {
    NRF_RADIO->EVENTS_TXREADY = 0;

    /* Set shorts for turning around to RX */
    NRF_RADIO->SHORTS &= ~RADIO_SHORTS_DISABLED_TXEN_Msk;
    NRF_RADIO->SHORTS |= RADIO_SHORTS_DISABLED_RXEN_Msk;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
  }
  if ((NRF_RADIO->EVENTS_RXREADY == 1) && (NRF_RADIO->INTENSET & RADIO_INTENSET_RXREADY_Msk)) {
    NRF_RADIO->EVENTS_RXREADY = 0;

    /* Set shorts for turning around to TX to send acknowledgement */
    NRF_RADIO->SHORTS &= ~RADIO_SHORTS_DISABLED_RXEN_Msk;
    NRF_RADIO->SHORTS |= RADIO_SHORTS_DISABLED_TXEN_Msk;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_CRCOK_Msk | RADIO_INTENSET_CRCERROR_Msk;
  }
  /* Transmission of acknowledgement has finished */
  if ((NRF_RADIO->EVENTS_END == 1) && (NRF_RADIO->INTENSET & RADIO_INTENSET_END_Msk)) {
    NRF_RADIO->EVENTS_END = 0;

    /* Prepare for listening again */
    NRF_RADIO->PACKETPTR = (uint32_t)&rx_pkt;
    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_RXREADY_Msk;

    /* Notify application that it can pick up the received packet */
    k_event_post(&radio_evt, RADIO_EVT_END);
  }
  /* Valid packet received */
  if ((NRF_RADIO->EVENTS_CRCOK == 1) && (NRF_RADIO->INTENSET & RADIO_INTENSET_CRCOK_Msk)) {
    NRF_RADIO->EVENTS_CRCOK = 0;

    pkt_t* tx_pkt;
    /* Get a packet that is to be sent to the device from which we just received something */
    if (msg_buf_get_claim(&tx_pkt, rx_pkt.dev_id) == 0)
      /* Let the application know that we have claimed a buffer */
      k_event_post(&radio_evt, RADIO_EVT_CLAIM);
    else
      /* If there is no packet pending, send an empty acknowledgement */
      tx_pkt = &ack_only_pkt;

    /* Insert Packet ID of received packet into acknowledgement */
    tx_pkt->acknowledgement_id = rx_pkt.pkt_id;

    NRF_RADIO->PACKETPTR = (uint32_t)tx_pkt;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_TXREADY_Msk;

    /* Notify application that it can pick up the received packet */
    k_event_post(&radio_evt, RADIO_EVT_CRCOK);
  }
  /* Bad CRC -> Abort transmission of Acknowledgement*/
  if ((NRF_RADIO->EVENTS_CRCERROR == 1) && (NRF_RADIO->INTENSET & RADIO_INTENSET_CRCERROR_Msk)) {
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

/* This thread processes the radio events to offload work from the ISR. */
static void radio_handler() {
  pkt_t tmp_buf;
  while (1) {
    /* Wait until a valid packet is received */
    k_event_wait(&radio_evt, RADIO_EVT_CRCOK, false, K_FOREVER);
    /* Copy received packet into buffer to immediately free the receive buffer */
    memcpy(&tmp_buf, &rx_pkt, rx_pkt.len + 1);

    /* Wait until acknowledgement has been sent */
    uint32_t events = k_event_wait(&radio_evt, RADIO_EVT_END, false, K_FOREVER);

    /* If the acknowledgement packet has been taken from the message buffer*/
    if (events & RADIO_EVT_CLAIM)
      /* Tell the buffer that we're done with the packet */
      msg_buf_get_finish(tmp_buf.dev_id);

    /* Send last received packet to application for processing */
    k_msgq_put(&pkt_mq, &tmp_buf, K_NO_WAIT);

    k_event_clear(&radio_evt, 0xFFFFFFFF);
  }
}

int radio_msgq_get(pkt_t* pkt, k_timeout_t timeout) {
  return k_msgq_get(&pkt_mq, pkt, timeout);
}

int radio_start() {
  k_thread_create(&radio_thread_data, radio_thread_stack, K_THREAD_STACK_SIZEOF(radio_thread_stack), radio_handler,
                  NULL, NULL, NULL, 6, 0, K_NO_WAIT);

  /* If it's not running, start the HFCLCK */
  if ((NRF_CLOCK->HFCLKSTAT & (CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos)) == 0) {
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
