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

LOG_MODULE_REGISTER(cdc_acm_echo, LOG_LEVEL_INF);

#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];

struct ring_buf ringbuf;

uint8_t cmd_buf[512];

static void interrupt_handler(const struct device *dev, void *user_data) {
  ARG_UNUSED(user_data);

  while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
    if (uart_irq_rx_ready(dev)) {
      int recv_len, rb_len;
      uint8_t buffer[64];
      size_t len = MIN(ring_buf_space_get(&ringbuf), sizeof(buffer));

      recv_len = uart_fifo_read(dev, buffer, len);
      if (recv_len < 0) {
        LOG_ERR("Failed to read UART FIFO");
        recv_len = 0;
      };

#if 0
      rb_len = ring_buf_put(&ringbuf, buffer, recv_len);
      if (rb_len < recv_len) {
        LOG_ERR("Drop %u bytes", recv_len - rb_len);
      }

      LOG_DBG("tty fifo -> ringbuf %d bytes", rb_len);
      if (rb_len) {
        uart_irq_tx_enable(dev);
      }
#endif
    }

    if (uart_irq_tx_ready(dev)) {
      uint8_t buffer[64];
      int rb_len, send_len;

      rb_len = ring_buf_get(&ringbuf, buffer, sizeof(buffer));
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

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
void cdc_acm_thread(void) {
  const struct device *dev;

  uint32_t baudrate, dtr = 0U;
  int ret;

  dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
  if (!device_is_ready(dev)) {
    LOG_ERR("CDC ACM device not ready");
    return;
  }

  ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

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

  uart_irq_callback_set(dev, interrupt_handler);

  /* Enable rx interrupts */
  uart_irq_rx_enable(dev);

  if (!ring_buf_is_empty(&ringbuf))
    uart_irq_tx_enable(dev);

  while (1) {
    k_msleep(100);
  }
}

void main(void) {

  int ret;

  if (!gpio_is_ready_dt(&led)) {
    return;
  }

  ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
  if (ret < 0) {
    return;
  }

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
  ret = enable_usb_device_next();
#else
  ret = usb_enable(NULL);
#endif

  if (ret != 0) {
    LOG_ERR("Failed to enable USB");
    return;
  }

  while (1) {
    ret = gpio_pin_toggle_dt(&led);
    k_msleep(100);
  }
}

char pkt_descriptor[1024];
uint16_t hex_lut[256];

void print_handler() {

  const struct device *dev;
  char **buf_ptr;
  pkt_t pkt_buf;
  dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

  /* Prepare lookup table for rapid conversion to ASCII Hex */
  for (unsigned int i = 0; i < 256; i++) {
    sprintf((char *)&hex_lut[i], "%02X", i);
  }

  while (1) {

    /* Grab a packet from the queue */
    k_msgq_get(&pkt_mq, &pkt_buf, K_FOREVER);

    int n = sprintf(pkt_descriptor, ":%08X:%04X:%04X:", pkt_buf.dev_id,
                    pkt_buf.pkt_id, pkt_buf.acknowledgement_id);

    uint16_t *ptr = (uint16_t *)(pkt_descriptor + n);
    for (unsigned int i = 0; i < pkt_buf.len - 8; i++) {
      *ptr++ = hex_lut[pkt_buf.data[i]];
    }
    n += 2 * (pkt_buf.len - 8);
    n += sprintf(pkt_descriptor + n, ":\r\n");

    /* Wait for space in ringbuffer */
    if (ring_buf_space_get(&ringbuf) >= (pkt_buf.len + 1)) {
      ring_buf_put(&ringbuf, pkt_descriptor, n);
      uart_irq_tx_enable(dev);
    } else
      LOG_ERR("Drop packet descriptor");

    printf(pkt_descriptor);
  }
}

K_THREAD_DEFINE(printer, 2048, print_handler, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(cdc_acm, 1024, cdc_acm_thread, NULL, NULL, NULL, 7, 0, 0);