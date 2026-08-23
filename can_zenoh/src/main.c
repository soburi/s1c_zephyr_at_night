/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zenoh-pico.h>

#define LED_NODE DT_ALIAS(led0)
#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
#define LOCATOR_SIZE 96
#define KEY_SIZE 96
#define CAN_MESSAGE_ID 0x28
#define COMM_WORK_QUEUE_STACK_SIZE 1024
#define COMM_WORK_QUEUE_PRIORITY 5

/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define SW0_NODE	DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;

/*
 * The led0 devicetree alias is optional. If present, we'll use it
 * to turn on the LED whenever the button is pressed.
 */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios,
						     {0});

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct device *const zenoh_uart_dev = DEVICE_DT_GET(DT_ALIAS(zenoh_uart));

static z_owned_session_t session;

static bool toggle_led(struct gpio_dt_spec *led)
{
	int led_state = 0;

	if (gpio_pin_get_dt(led) == 0) {
		led_state = 1;
	} else {
		led_state = 0;
	}

	(void)gpio_pin_set_dt(led, led_state);
	return led_state;
}

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
}

/**
 * CAN送信処理
 */
static void can_send_work_handler(struct k_work *work)
{
	const struct can_frame frame = {
		.id = CAN_MESSAGE_ID,
		.dlc = 1,
		.data = { 1 },
	};
	int ret;

	ARG_UNUSED(work);

	ret = can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (ret != 0) {
		printk("CAN send failed (%d)\n", ret);
	} else {
		printk("CAN message sent\n");
	}
}

static int hex_digit(char value)
{
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return value - 'a' + 10;
	if (value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -EINVAL;
}

static int can_id_from_key(const char *key, size_t key_len, uint32_t *id)
{
	const char *prefix = CONFIG_APP_ZENOH_KEY_PREFIX;
	const size_t prefix_len = strlen(prefix);

	if (key_len != prefix_len + sizeof("/000/tx") - 1 ||
	    memcmp(key, prefix, prefix_len) != 0 || key[prefix_len] != '/' ||
	    memcmp(&key[prefix_len + 4], "/tx", 3) != 0) {
		return -EINVAL;
	}

	*id = 0;
	for (size_t i = prefix_len + 1; i < prefix_len + 4; ++i) {
		const int digit = hex_digit(key[i]);
		if (digit < 0) return -EINVAL;
		*id = (*id << 4) | (uint32_t)digit;
	}
	return *id <= CAN_STD_ID_MASK ? 0 : -EINVAL;
}

/**
 * CANメッセージを受け取ったときの動作
 * LEDを反転させる
 */
static void can_received(const struct device *dev, struct can_frame *frame,
			 void *user_data)
{
	bool enabled = 0;

	if (led.port) {
		enabled = toggle_led(&led);
	}


	char key[KEY_SIZE];
	z_view_keyexpr_t keyexpr;
	z_owned_bytes_t payload;
	int ret;

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	if ((frame->flags & CAN_FRAME_RTR) != 0) return;

	ret = snprintf(key, sizeof(key), "%s/%03x/rx",
		       CONFIG_APP_ZENOH_KEY_PREFIX, frame->id);
	if (ret < 0 || (size_t)ret >= sizeof(key)) {
		printk("CAN -> Zenoh key is too long\n");
		return;
	}
	z_view_keyexpr_from_str_unchecked(&keyexpr, key);
	if (z_bytes_copy_from_buf(&payload, frame->data, frame->dlc) < 0) {
		printk("CAN -> Zenoh payload allocation failed\n");
		return;
	}
	if (z_put(z_loan(session), z_loan(keyexpr), z_move(payload), NULL) < 0) {
		printk("CAN -> Zenoh failed: %s\n", key);
		return;
	}
	if (led.port != NULL) (void)toggle_led(&led);
	printk("CAN -> Zenoh: 0x%03x (%u bytes) -> %s\n",
	       frame->id, frame->dlc, key);
}

/* Zenoh RX callback: send immediately, without an intermediate queue. */
static void on_zenoh_sample(z_loaned_sample_t *sample, void *context)
{
	const z_loaned_bytes_t *payload = z_sample_payload(sample);
	struct can_frame frame = {0};
	z_bytes_reader_t reader;
	const size_t len = z_bytes_len(payload);
	int ret;
	bool enabled = 0;

	if (led.port) {
		enabled = toggle_led(&led);
	}

	if (len > CAN_MAX_DLEN) {
		printk("Zenoh -> CAN ignored: payload is %u bytes (max %u)\n",
		       (unsigned int)len, CAN_MAX_DLEN);
		return;
	}

	z_view_string_t key;
	z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
	printk("RX %.*s: LED -> %s\n", (int)z_string_len(z_loan(key)),
		z_string_data(z_loan(key)), enabled ? "on" : "off");

	if (can_id_from_key(z_string_data(z_loan(key)),
			    z_string_len(z_loan(key)), &frame.id) != 0) {
		printk("Zenoh -> CAN ignored: invalid key\n");
		return;
	}
	frame.dlc = (uint8_t)len;
	reader = z_bytes_get_reader(payload);
	if (z_bytes_reader_read(&reader, frame.data, len) != len) {
		printk("Zenoh -> CAN failed: could not read payload\n");
		return;
	}
	ret = can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (ret != 0) {
		printk("Zenoh -> CAN failed for 0x%03x (%d)\n", frame.id, ret);
		return;
	}
	printk("Zenoh -> CAN: 0x%03x (%u bytes)\n", frame.id, frame.dlc);
}

/*
 * main
 */
int main(void)
{
	char subscribe_key[KEY_SIZE];
	z_view_keyexpr_t sub_key;
	z_owned_closure_sample_t callback;
	z_owned_subscriber_t subscriber;
	int ret;

	if (!device_is_ready(can_dev)) {
		printk("CAN device is not ready\n");
		return 0;
	}

	if (!device_is_ready(zenoh_uart_dev)) {
		printk("Zenoh UART device is not ready\n");
		return 0;
	}

	const struct can_filter filter = {
		.id = 0,
		.mask = 0,
	};

	ret = can_add_rx_filter(can_dev, can_received, NULL, &filter);
	if (ret < 0) {
		printk("CAN receive filter registration failed (%d)\n", ret);
		return 0;
	}

	ret = can_start(can_dev);
	if (ret != 0) {
		printk("CAN start failed (%d)\n", ret);
		return 0;
	}

	char locator[LOCATOR_SIZE];
	(void)snprintf(locator, sizeof(locator), "serial/%s#baudrate=%d",
		zenoh_uart_dev->name, CONFIG_APP_ZENOH_BAUDRATE);
	(void)snprintf(subscribe_key, sizeof(subscribe_key), "%s/*/tx",
		       CONFIG_APP_ZENOH_KEY_PREFIX);
	printk("Connecting to zenohd at %s\n", locator);

	z_owned_config_t config;
	z_config_default(&config);
	if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
	    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) < 0) {
		printk("Failed to create Zenoh configuration\n");
		z_drop(z_move(config));
		return 0;
	}

	if (z_open(&session, z_move(config), NULL) < 0) {
		printk("Could not open the Zenoh session; check UART and zenohd\n");
		return 0;
	}

	z_view_keyexpr_from_str_unchecked(&sub_key, subscribe_key);
	z_closure(&callback, on_zenoh_sample, NULL, NULL);
	if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(sub_key),
				 z_move(callback), NULL) < 0) {
		printk("Could not subscribe to %s\n", subscribe_key);
		z_drop(z_move(session));
		return 0;
	}

	if (ret != 0) {
		printk("CAN start failed (%d)\n", ret);
		z_drop(z_move(subscriber));
		z_drop(z_move(session));
		return 0;
	}

	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return 0;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

	if (led.port && !gpio_is_ready_dt(&led)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, led.port->name);
		led.port = NULL;
	}
	if (led.port) {
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, led.port->name, led.pin);
			led.port = NULL;
		} else {
			printk("Set up LED at %s pin %d\n", led.port->name, led.pin);
		}
	}

	printk("Ready: CAN -> %s/<ID>/rx, %s -> CAN\n",
		CONFIG_APP_ZENOH_KEY_PREFIX, subscribe_key);

	k_sleep(K_FOREVER);

	return 0;
}
