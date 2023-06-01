/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sample echo app for CDC ACM class
 *
 * Sample app for USB CDC ACM class driver. The received data is echoed back
 * to the serial port.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
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

      uint32_t rb_size = ring_buf_put_claim(&cdcacm_ringbuf_rx, &ring_buf_slice, 64);
      if (rb_size == 64) {
        recv_len = uart_fifo_read(dev, ring_buf_slice, 64);
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

static void process_uart_pkt(char *pkt, size_t n) {
  LOG_INF("Detected a packet with length %u", n);
}

void cdcacm_handler(void) {
  const struct device *dev;

  uint32_t baudrate, dtr = 0U;
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

#if 0
  LOG_INF("Wait for DTR");

  while (true) {
    uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
    if (dtr) {
      break;
    } else {
      /* Give CPU resources to low priority threads. */
      k_sleep(K_MSEC(100));
    }
  }

  LOG_INF("DTR set");

  /* They are optional, we use them to test the interrupt endpoint */
  ret = uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
  if (ret) {
    LOG_WRN("Failed to set DCD, ret code %d", ret);
  }

  ret = uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);
  if (ret) {
    LOG_WRN("Failed to set DSR, ret code %d", ret);
  }

  /* Wait 100ms for the host to do all settings */
  k_msleep(100);

  ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
  if (ret) {
    LOG_WRN("Failed to get baudrate, ret code %d", ret);
  } else {
    LOG_INF("Baudrate detected: %d", baudrate);
  }
#endif
  uart_irq_callback_set(dev, interrupt_handler);

  /* Enable rx interrupts */
  uart_irq_rx_enable(dev);

  if (!ring_buf_is_empty(&cdcacm_ringbuf_tx))
    uart_irq_tx_enable(dev);

  char rcv_buffer[1024];
  unsigned int n_copied;
  char *rcv_buf_ptr;
  uint32_t rb_size;

  int rcv_buffer_idx = -1;

  while (1) {
    if ((rb_size = ring_buf_get_claim(&cdcacm_ringbuf_rx, (uint8_t **)&rcv_buf_ptr, 64)) == 0)
      k_event_wait(&uart_rx_evt, 0xFFFFFFFF, true, K_FOREVER);

    for (n_copied = 0; n_copied < rb_size; n_copied++) {
      /* If we have not detected the start of a packet */
      if (rcv_buffer_idx < 0) {
        /* Check if this is the start of a packet*/
        if (rcv_buf_ptr[n_copied] == '[') {
          rcv_buffer_idx = 0;
          LOG_INF("Start of packet detected");
        }
        continue;
      }

      /* Is this the end of a packet? */
      if (rcv_buf_ptr[n_copied] == ']') {
        process_uart_pkt(rcv_buffer, rcv_buffer_idx);
        rcv_buffer_idx = -1;
      } else {
        rcv_buffer[rcv_buffer_idx++] = rcv_buf_ptr[n_copied];
        if (rcv_buffer_idx == sizeof(rcv_buffer)) {
          LOG_ERR("Buffer is full, no packet detected");
          rcv_buffer_idx = -1;
          break;
        }
      }
    }

    ring_buf_get_finish(&cdcacm_ringbuf_rx, n_copied);
  }
}

void printer_handler() {
  const struct device *dev;

  char pkt_descriptor[1024];
  uint16_t hex_lut[256];

  pkt_t pkt_buf;
  dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(dev)) {
    LOG_ERR("CDC ACM device not ready");
    return;
  }

  /* Prepare lookup table for rapid conversion to ASCII Hex */
  for (unsigned int i = 0; i < 256; i++) {
    sprintf((char *)&hex_lut[i], "%02X", i);
  }

  while (1) {
    /* Grab a packet from the queue */
    radio_msgq_get(&pkt_buf, K_FOREVER);

    if ((pkt_buf.len > (sizeof(pkt_t) - 1) || (pkt_buf.len < 8))) {
      /* Something is wrong with this packet! */
      LOG_ERR("Received packet with wrong size");
      continue;
    }

    int n = sprintf(pkt_descriptor, ":%08X:%04X:%04X:", pkt_buf.dev_id, pkt_buf.pkt_id, pkt_buf.acknowledgement_id);

    uint16_t *ptr = (uint16_t *)(pkt_descriptor + n);
    for (unsigned int i = 0; i < pkt_buf.len - 8; i++) {
      *ptr++ = hex_lut[pkt_buf.data[i]];
    }

    n += 2 * (pkt_buf.len - 8);
    n += sprintf(pkt_descriptor + n, ":\r\n");

    if (ring_buf_space_get(&cdcacm_ringbuf_tx) >= (pkt_buf.len + 1)) {
      ring_buf_put(&cdcacm_ringbuf_tx, pkt_descriptor, n);
      uart_irq_tx_enable(dev);
    } else
      LOG_DBG("Dropped packet descriptor");

    LOG_DBG("%s", pkt_descriptor);
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