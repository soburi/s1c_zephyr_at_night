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

#define SLEEP_TIME_MS	1
#define CAN_MESSAGE_ID_SELF   0x28
#define CAN_MESSAGE_ID_TARGET 0x28

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

void send_status_can_msg(uint32_t canid, uint8_t enabled)
{
	struct can_frame frame = {0};
	int ret;

	frame.id = canid;
	frame.dlc = 1;
	frame.data[0] = enabled;

	ret = can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (ret != 0) {
		printk("Zenoh -> CAN failed for 0x%03x (%d)\n", frame.id, ret);
		return;
	}
	printk("Zenoh -> CAN: 0x%03x (%u bytes)\n", frame.id, frame.dlc);
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

void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	bool enabled = 0;

	if (led.port) {
		enabled = toggle_led(&led);
	}

	send_status_can_msg(CAN_MESSAGE_ID_TARGET, enabled);

	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
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

	const struct can_filter filter = {
		.id = CAN_MESSAGE_ID_SELF,
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

	printk("Press the button\n");

	/* CAN受信処理でLED状態を変化させるので、LED状態を変化させるループは削除。
	 * 無期限の待ちに入る
	 */
	k_sleep(K_FOREVER);

	return 0;
}
