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
#define PRINTER_STACK_SIZE 8192
#define CDCACM_STACK_SIZE 4096

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
      uint8_t overflow_buffer[64];
      uint8_t *ring_buf_slice;

      /* Claim some space on the ringbuffer */
      uint32_t rb_size = ring_buf_put_claim(&cdcacm_ringbuf_rx, &ring_buf_slice, 64);
      if (rb_size > 0) {
        recv_len = uart_fifo_read(dev, ring_buf_slice, rb_size);
        ring_buf_put_finish(&cdcacm_ringbuf_rx, recv_len);
        k_event_set(&uart_rx_evt, 0xFFFFFFFF);

        /* If we can't allocate space on the ringbuffer, drop the UART data */
      } else {
        recv_len = uart_fifo_read(dev, overflow_buffer, 64);
        ring_buf_put_finish(&cdcacm_ringbuf_rx, 0);
        LOG_ERR("CDC Uart ringbuffer overflow");
      }

      if (recv_len < 0) {
        LOG_ERR("Failed to read UART FIFO");
      }
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

static int process_uart_pkt(pkt_t *dst, char *pkt_str, size_t n) {
  size_t n_written;
  unsigned int i = 0, j = 0;

  while (pkt_str[i] != '\0') {
    if (++i == n)
      return -1;
  }

  if (base64_decode((uint8_t *)&dst->hdr.dev_id, 4, &n_written, pkt_str, i - j) < 0)
    return -1;
  j = ++i;

  while (pkt_str[i] != '\0') {
    if (++i == n)
      return -1;
  }

  if (base64_decode((uint8_t *)&dst->hdr.pkt_id, 2, &n_written, pkt_str, i - j) < 0)
    return -1;
  j = ++i;

  while (pkt_str[i] != '\0') {
    if (++i == n)
      return -1;
  }

  if (base64_decode((uint8_t *)&dst->hdr.ack_id, 2, &n_written, pkt_str, i - j) < 0)
    return -1;
  i++;

  if (base64_decode((uint8_t *)&dst->data, PKT_PAYLOAD_SIZE, &n_written, pkt_str, n - i) < 0)
    return -1;

  dst->len = sizeof(pkt_header_t) + n_written;
  return 0;
}

void cdcacm_handler(void) {
  const struct device *dev;

  int ret;

  ret = usb_enable(NULL);

  if (ret != 0) {
    LOG_ERR("Failed to enable USB");
    return;
  }

  dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(dev)) {
    LOG_ERR("CDC ACM device not ready");
    return;
  }

  uart_irq_callback_set(dev, interrupt_handler);

  /* Enable rx interrupts */
  uart_irq_rx_enable(dev);

  if (!ring_buf_is_empty(&cdcacm_ringbuf_tx))
    uart_irq_tx_enable(dev);

  char pkt_string_buf[1024];
  unsigned int n_copied;
  char *ring_buf_slice;
  uint32_t rb_size;
  pkt_t pkt_buf;
  int rc;

  /* This means that no start of a packet has been found */
  int pkt_string_buf_idx = -1;

  while (1) {
    if ((rb_size = ring_buf_get_claim(&cdcacm_ringbuf_rx, (uint8_t **)&ring_buf_slice, 64)) == 0)
      k_event_wait(&uart_rx_evt, 0xFFFFFFFF, true, K_FOREVER);

    for (n_copied = 0; n_copied < rb_size; n_copied++) {
      /* If we have not detected the start of a packet yet */
      if (pkt_string_buf_idx < 0) {
        /* Check if this is the start of a packet*/
        if (ring_buf_slice[n_copied] == '[') {
          pkt_string_buf_idx = 0;
          LOG_INF("Start of packet detected");
        }
        continue;
      }

      /* Is this the end of a packet? */
      if (ring_buf_slice[n_copied] == ']') {
        if ((rc = process_uart_pkt(&pkt_buf, pkt_string_buf, pkt_string_buf_idx)) >= 0)
          LOG_DBG("Processed packet with %08X:%04X:%04X and %u byte payload", pkt_buf.hdr.dev_id, pkt_buf.hdr.pkt_id,
                  pkt_buf.hdr.ack_id, pkt_buf.len);
        else
          LOG_ERR("Processing failed with %d", rc);
        pkt_string_buf_idx = -1;
      } else {
        pkt_string_buf[pkt_string_buf_idx++] = ring_buf_slice[n_copied];
        if (pkt_string_buf_idx == sizeof(pkt_string_buf)) {
          LOG_ERR("Buffer is full, no packet detected");
          pkt_string_buf_idx = -1;
          break;
        }
      }
    }

    ring_buf_get_finish(&cdcacm_ringbuf_rx, n_copied);
  }
}

void printer_handler() {
  /* Receives packets from the radio packet queue, generates a base64 based string and hands it over to the CDC ACM*/
  const struct device *dev;

  char pkt_descriptor[1024];

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

    size_t olen;
    size_t n_written = 0;

    pkt_descriptor[n_written++] = '[';
    base64_encode(pkt_descriptor + n_written, sizeof(pkt_descriptor), &olen, (uint8_t *)&pkt_buf.hdr.dev_id, 4);
    /* Make sure to also include the trailing \0 in the string as a delimiter*/
    n_written += olen + 1;
    base64_encode(pkt_descriptor + n_written, sizeof(pkt_descriptor) - n_written, &olen, (uint8_t *)&pkt_buf.hdr.pkt_id,
                  2);
    n_written += olen + 1;
    base64_encode(pkt_descriptor + n_written, sizeof(pkt_descriptor) - n_written, &olen, (uint8_t *)&pkt_buf.hdr.ack_id,
                  2);
    n_written += olen + 1;
    base64_encode(pkt_descriptor + n_written, sizeof(pkt_descriptor) - n_written, &olen, (uint8_t *)&pkt_buf.data,
                  pkt_buf.len - 8);
    n_written += olen + 1;
    pkt_descriptor[n_written++] = ']';

    if (ring_buf_space_get(&cdcacm_ringbuf_tx) >= n_written) {
      ring_buf_put(&cdcacm_ringbuf_tx, pkt_descriptor, n_written);
      uart_irq_tx_enable(dev);
    } else
      LOG_DBG("Dropped packet descriptor");

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

  radio_init();
  radio_start();

  while (1) {
    ret = gpio_pin_toggle_dt(&led);
    k_msleep(100);
  }
}

K_THREAD_DEFINE(printer, PRINTER_STACK_SIZE, printer_handler, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(cdcacm, CDCACM_STACK_SIZE, cdcacm_handler, NULL, NULL, NULL, 7, 0, 0);