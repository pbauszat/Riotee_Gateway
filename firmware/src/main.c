#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/sys/base64.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>

#include "radio.h"

#define RING_BUF_SIZE 2048
#define PRINTER_STACK_SIZE 2048
#define CDCACM_STACK_SIZE 2048

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

RING_BUF_DECLARE(cdcacm_ringbuf_tx, RING_BUF_SIZE);
RING_BUF_DECLARE(cdcacm_ringbuf_rx, RING_BUF_SIZE);

K_EVENT_DEFINE(uart_rx_evt);

static void interrupt_handler(const struct device *dev, void *user_data) {
  ARG_UNUSED(user_data);

  while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
    if (uart_irq_rx_ready(dev)) {
      int recv_len;
      uint8_t buffer[64];

      /* Claim some space on the ringbuffer */
      recv_len = uart_fifo_read(dev, buffer, sizeof(buffer));
      if (recv_len < 0) {
        LOG_ERR("Failed to read UART FIFO");
        continue;
      }
      uint32_t written = ring_buf_put(&cdcacm_ringbuf_rx, buffer, recv_len);
      if (written > 0)
        k_event_set(&uart_rx_evt, 0xFFFFFFFF);
      else
        LOG_ERR("UART ringbuffer full");
    }

    if (uart_irq_tx_ready(dev)) {
      uint8_t buffer[64];
      int rb_len, send_len;

      rb_len = ring_buf_get(&cdcacm_ringbuf_tx, buffer, sizeof(buffer));
      if (!rb_len) {
        LOG_DBG("Ring buffer empty, disable TX IRQ");
        uart_irq_tx_disable(dev);
        continue;
      }

      send_len = uart_fifo_fill(dev, buffer, rb_len);
      if (send_len < rb_len) {
        LOG_ERR("Drop %d bytes", rb_len - send_len);
      }

      LOG_DBG("ringbuf -> tty fifo %d bytes", send_len);
    }
  }
}

static int string2packet(pkt_t *dst, char *pkt_str, size_t pkt_str_len) {
  size_t n_written;
  size_t n;
  char *s = pkt_str;

  if ((n = strlen(s)) != 8)
    return -1;

  if (base64_decode((uint8_t *)&dst->hdr.dev_id, 4, &n_written, s, n) < 0)
    return -1;

  s += n + 1;
  if ((n = strlen(s)) != 4)
    return -1;

  if (base64_decode((uint8_t *)&dst->hdr.pkt_id, 2, &n_written, s, n) < 0)
    return -1;

  s += n + 1;
  if ((s - pkt_str) > pkt_str_len)
    return -1;
  n = strlen(s);

  if (base64_decode((uint8_t *)&dst->data, PKT_PAYLOAD_SIZE, &n_written, s, n) < 0)
    return -1;

  dst->len = sizeof(pkt_header_t) + n_written;
  return 0;
}

static inline uint32_t ring_buf_get_wait(uint8_t **buf_slice_p) {
  uint32_t rb_size;
  while ((rb_size = ring_buf_get_claim(&cdcacm_ringbuf_rx, buf_slice_p, 64)) == 0)
    k_event_wait(&uart_rx_evt, 0xFFFFFFFF, true, K_FOREVER);

  return rb_size;
}

/* Loads data from cdc acm, buffer by buffer, to extract a packet descriptor */
static int find_in_ring(char *dst, size_t buf_size) {
  bool found = false;
  uint8_t *buf_slice_p;
  unsigned int i;
  do {
    /* Get a slice from the ringbuf */
    int rb_size = ring_buf_get_wait(&buf_slice_p);
    for (i = 0; i < rb_size; i++) {
      /* We're looking for an opening bracket */
      if (buf_slice_p[i] == '[') {
        found = true;
        break;
      }
    }
    /* Let the ringbuffer know that we're done with our slice*/
    ring_buf_get_finish(&cdcacm_ringbuf_rx, i);
  } while (!found);

  /* Drop the opening bracket at the start of the packet */
  while (ring_buf_get(&cdcacm_ringbuf_rx, NULL, 1) == 0)
    k_event_wait(&uart_rx_evt, 0xFFFFFFFF, true, K_FOREVER);

  /* Now start the search for the closing bracket */
  found = false;
  int written = 0;
  do {
    int rb_size = ring_buf_get_wait(&buf_slice_p);
    for (i = 0; i < rb_size; i++) {
      /* Another opening bracket indicates that something is wrong */
      if (buf_slice_p[i] == '[') {
        ring_buf_get_finish(&cdcacm_ringbuf_rx, 0);
        return -1;
      }
      if (buf_slice_p[i] == ']') {
        found = true;
        break;
      }
    }
    /* Check if buffer would overflow */
    if ((written + i) > buf_size) {
      ring_buf_get_finish(&cdcacm_ringbuf_rx, 0);
      LOG_ERR("Packet string buffer overflow");
      return -1;
    }
    memcpy(dst + written, buf_slice_p, i);
    written += i;
    ring_buf_get_finish(&cdcacm_ringbuf_rx, i);

  } while (!found);
  return written;
}

int cdcacm_init(void) {
  const struct device *dev;
  uint32_t dtr;

  int ret;

  ret = usb_enable(NULL);

  if (ret != 0) {
    LOG_ERR("Failed to enable USB");
    return -1;
  }

  dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(dev)) {
    LOG_ERR("CDC ACM device not ready");
    return -1;
  }

  LOG_INF("Wait for DTR");

  while (true) {
    uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
    if (dtr) {
      break;
    } else {
      k_msleep(10);
    }
  }

  LOG_INF("DTR set");

  uart_irq_callback_set(dev, interrupt_handler);

  /* Enable rx interrupts */
  uart_irq_rx_enable(dev);

  if (!ring_buf_is_empty(&cdcacm_ringbuf_tx))
    uart_irq_tx_enable(dev);
  return 0;
}

/* Receives incoming data stream from cdc acm, extracts packets and processes the result */
void cdcacm_handler(void) {
  static char pkt_string_buf[512];

  pkt_t pkt;
  int rc;
  while (1) {
    int pkt_str_len = find_in_ring(pkt_string_buf, sizeof(pkt_string_buf));
    if (pkt_str_len < 0) {
      LOG_ERR("Error finding packet in uart stream");
      continue;
    }

    if ((rc = string2packet(&pkt, pkt_string_buf, pkt_str_len)) < 0) {
      LOG_ERR("Error processing packet: %d", rc);
      continue;
    }
    LOG_INF("Packet processed: %08X, %04X", pkt.hdr.dev_id, pkt.hdr.pkt_id);
  }
}

size_t packet2string(char *dst, size_t dst_size, pkt_t *pkt) {
  size_t olen;
  size_t n_written = 0;

  dst[n_written++] = '[';
  base64_encode(dst + n_written, dst_size, &olen, (uint8_t *)&pkt->hdr.dev_id, 4);
  /* Make sure to also include the trailing \0 in the string as a delimiter*/
  n_written += olen + 1;
  base64_encode(dst + n_written, dst_size - n_written, &olen, (uint8_t *)&pkt->hdr.pkt_id, 2);
  n_written += olen + 1;
  base64_encode(dst + n_written, dst_size - n_written, &olen, (uint8_t *)&pkt->hdr.ack_id, 2);
  n_written += olen + 1;
  base64_encode(dst + n_written, dst_size - n_written, &olen, (uint8_t *)&pkt->data, pkt->len - sizeof(pkt_header_t));
  n_written += olen + 1;
  dst[n_written++] = ']';
  return n_written;
}

/* Receives packets from the radio packet queue, generates a base64 based string and hands it over to the CDC ACM*/
void printer_handler() {
  const struct device *dev;
  static char pkt_descriptor[512];

  pkt_t pkt_buf;
  dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(dev)) {
    LOG_ERR("CDC ACM device not ready");
    return;
  }

  while (1) {
    /* Grab a packet from the queue */
    radio_msgq_get(&pkt_buf, K_FOREVER);

    if ((pkt_buf.len > (sizeof(pkt_t) - 1) || (pkt_buf.len < 8))) {
      LOG_ERR("Received packet with wrong size");
      continue;
    }

    size_t n = packet2string(pkt_descriptor, sizeof(pkt_descriptor), &pkt_buf);
    if (ring_buf_space_get(&cdcacm_ringbuf_tx) >= n) {
      ring_buf_put(&cdcacm_ringbuf_tx, pkt_descriptor, n);
      uart_irq_tx_enable(dev);
    } else
      LOG_DBG("Ringbuf full. Dropping packet descriptor.");

    LOG_DBG("[%08X:%04X:%04X(%u)]", pkt_buf.hdr.dev_id, pkt_buf.hdr.pkt_id, pkt_buf.hdr.ack_id, pkt_buf.len);
  }
}

void main(void) {
  int ret;

  const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

  if (!gpio_is_ready_dt(&led)) {
    return;
  }

  ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
  if (ret < 0) {
    return;
  }

  cdcacm_init();

  radio_init();
  radio_start();

  while (1) {
    ret = gpio_pin_toggle_dt(&led);
    k_msleep(100);
  }
}

K_THREAD_DEFINE(printer, PRINTER_STACK_SIZE, printer_handler, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(cdcacm, CDCACM_STACK_SIZE, cdcacm_handler, NULL, NULL, NULL, 7, 0, 0);