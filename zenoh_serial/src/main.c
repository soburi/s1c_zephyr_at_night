/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NOTE: If you are looking into an implementation of button events with
 * debouncing, check out `input` subsystem and `samples/subsys/input/input_dump`
 * example instead.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

#include <zenoh-pico.h>

#define SLEEP_TIME_MS	1
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

static struct k_work_q workq;
static struct k_work can_send_work;
static struct k_work zenoh_publish_work;
K_THREAD_STACK_DEFINE(workq_stack, COMM_WORK_QUEUE_STACK_SIZE);

z_owned_publisher_t publisher;

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
	k_work_submit_to_queue(&workq, &zenoh_publish_work);
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

	printk("CAN message received with ID 0x%03x\n", frame->id);
}

static void zenoh_publish_work_handler(struct k_work *work)
{
	bool enabled = toggle_led(&led);
	const char *state = enabled ? "on" : "off";
	z_owned_bytes_t payload;

	z_bytes_copy_from_str(&payload, state);
	if (z_publisher_put(z_loan(publisher), z_move(payload), NULL) < 0) {
		printk("Button publish failed\n");
	} else {
		if (enabled) {
			printk("Button: LED -> on,");
		} else {
			printk("Button: LED -> off,");
		}
		printk("TX %s = %s\n", CONFIG_APP_ZENOH_PUB_KEY, state);
	}
}

static void on_sample(z_loaned_sample_t *sample, void *context)
{
	bool enabled = 0;

	if (led.port) {
		enabled = toggle_led(&led);
	}

	z_view_string_t key;
	z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
	printk("RX %.*s: LED -> %s\n", (int)z_string_len(z_loan(key)),
		z_string_data(z_loan(key)), enabled ? "on" : "off");
}

/*
 * main
 */
int main(void)
{
	int ret;

	if (!device_is_ready(can_dev)) {
		printk("CAN device is not ready\n");
		return 0;
	}

	if (!device_is_ready(zenoh_uart_dev)) {
		printk("CAN device is not ready\n");
		return 0;
	}

	const struct can_filter filter = {
		.id = CAN_MESSAGE_ID,
		.mask = CAN_STD_ID_MASK,
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
	printk("Connecting to zenohd at %s\n", locator);

	z_owned_config_t config;
	z_config_default(&config);
	if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
	    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) < 0) {
		printk("Failed to create Zenoh configuration\n");
		z_drop(z_move(config));
		return 0;
	}

	z_owned_session_t session;
	if (z_open(&session, z_move(config), NULL) < 0) {
		printk("Could not open the Zenoh session; check UART and zenohd\n");
		return 0;
	}


	z_view_keyexpr_t pub_key;
	z_view_keyexpr_from_str_unchecked(&pub_key, CONFIG_APP_ZENOH_PUB_KEY);
	if (z_declare_publisher(z_loan(session), &publisher, z_loan(pub_key), NULL) < 0) {
		printk("Could not declare publisher for %s\n", CONFIG_APP_ZENOH_PUB_KEY);
		z_drop(z_move(session));
		return 0;
	}

	z_view_keyexpr_t sub_key;
	z_view_keyexpr_from_str_unchecked(&sub_key, CONFIG_APP_ZENOH_SUB_KEY);
	z_owned_closure_sample_t callback;
	z_closure(&callback, on_sample, NULL, NULL);
	z_owned_subscriber_t subscriber;
	if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(sub_key),
				 z_move(callback), NULL) < 0) {
		printk("Could not subscribe to %s\n", CONFIG_APP_ZENOH_SUB_KEY);
		z_drop(z_move(session));
		z_drop(z_move(subscriber));
		return 0;
	}


	k_work_queue_start(&workq, workq_stack,
			   K_THREAD_STACK_SIZEOF(workq_stack),
			   COMM_WORK_QUEUE_PRIORITY, NULL);
	k_work_init(&can_send_work, can_send_work_handler);
	k_work_init(&zenoh_publish_work, zenoh_publish_work_handler);

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

	printk("Ready: button PUB %s, remote SUB %s\n", CONFIG_APP_ZENOH_PUB_KEY,
		CONFIG_APP_ZENOH_SUB_KEY);

	k_sleep(K_FOREVER);

	return 0;
}
