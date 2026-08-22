/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define COMM_WORK_QUEUE_STACK_SIZE 1024
#define COMM_WORK_QUEUE_PRIORITY 5

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct gpio_callback button_cb;
static struct k_work_q comm_work_q;
static struct k_work can_send_work;
K_THREAD_STACK_DEFINE(comm_work_q_stack, COMM_WORK_QUEUE_STACK_SIZE);

static void can_send_work_handler(struct k_work *work)
{
	const struct can_frame frame = {
		.id = CONFIG_BOARD_TX_CAN_ID,
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

static void button_pressed(const struct device *dev,
			   struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit_to_queue(&comm_work_q, &can_send_work);
}

static void can_received(const struct device *dev, struct can_frame *frame,
			 void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	gpio_pin_toggle_dt(&led);
	printk("CAN message received with ID 0x%03x\n", frame->id);
}

int main(void)
{
	const struct can_filter filter = {
		.id = CONFIG_BOARD_RX_CAN_ID,
		.mask = CAN_STD_ID_MASK,
	};
	int ret;

	if (!device_is_ready(can_dev)) {
		printk("CAN device is not ready\n");
		return 0;
	}

	if (!gpio_is_ready_dt(&button) || !gpio_is_ready_dt(&led)) {
		printk("Button or LED GPIO is not ready\n");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		printk("LED configuration failed (%d)\n", ret);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Button configuration failed (%d)\n", ret);
		return 0;
	}

	k_work_queue_start(&comm_work_q, comm_work_q_stack,
			   K_THREAD_STACK_SIZEOF(comm_work_q_stack),
			   COMM_WORK_QUEUE_PRIORITY, NULL);
	k_work_init(&can_send_work, can_send_work_handler);

	gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb);
	if (ret != 0) {
		printk("Button callback registration failed (%d)\n", ret);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Button interrupt configuration failed (%d)\n", ret);
		return 0;
	}

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

	printk("Ready: TX ID 0x%03x, RX ID 0x%03x; press the button to send\n",
	       CONFIG_BOARD_TX_CAN_ID, CONFIG_BOARD_RX_CAN_ID);
	k_sleep(K_FOREVER);

	return 0;
}
