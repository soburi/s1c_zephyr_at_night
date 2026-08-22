/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zenoh-pico.h>

LOG_MODULE_REGISTER(can_zenoh_gateway, LOG_LEVEL_INF);

#define ZENOH_UART_NODE DT_ALIAS(zenoh_uart)
#define LED_NODE DT_ALIAS(led0)
#define LOCATOR_SIZE 96
#define KEY_SIZE 96
#define EVENT_QUEUE_DEPTH 16

#if !DT_NODE_EXISTS(ZENOH_UART_NODE)
#error "The board overlay must define the zenoh-uart devicetree alias"
#endif
#if !DT_NODE_HAS_STATUS_OKAY(LED_NODE)
#error "The board must provide an enabled led0 alias"
#endif
enum gateway_event_type {
	EVENT_CAN_RECEIVED,
	EVENT_CAN_REQUESTED,
};

struct gateway_event {
	enum gateway_event_type type;
	struct can_frame frame;
};

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
K_MSGQ_DEFINE(gateway_events, sizeof(struct gateway_event), EVENT_QUEUE_DEPTH, 4);

static void queue_can_transmit(uint32_t id, const uint8_t *data, size_t len)
{
	struct gateway_event event = {
		.type = EVENT_CAN_REQUESTED,
		.frame = {
			.id = id,
			.dlc = len,
		},
	};

	if (len > 0) {
		memcpy(event.frame.data, data, len);
	}
	if (k_msgq_put(&gateway_events, &event, K_NO_WAIT) != 0) {
		LOG_WRN("Gateway event queue full; dropping CAN transmit request");
	}
}

static int hex_digit(char value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}
	if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	}
	if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}
	return -EINVAL;
}

static int can_id_from_key(const char *key, size_t key_len, uint32_t *id)
{
	const char *prefix = CONFIG_APP_ZENOH_KEY_PREFIX;
	size_t prefix_len = strlen(prefix);
	int digit;

	if (key_len != prefix_len + sizeof("/000/tx") - 1 ||
	    memcmp(key, prefix, prefix_len) != 0 || key[prefix_len] != '/' ||
	    memcmp(&key[prefix_len + 4], "/tx", 3) != 0) {
		return -EINVAL;
	}

	*id = 0;
	for (size_t i = prefix_len + 1; i < prefix_len + 4; ++i) {
		digit = hex_digit(key[i]);
		if (digit < 0) {
			return -EINVAL;
		}
		*id = (*id << 4) | digit;
	}
	return *id <= CAN_STD_ID_MASK ? 0 : -EINVAL;
}

static void on_can_received(const struct device *dev, struct can_frame *frame,
			    void *user_data)
{
	struct gateway_event event = {
		.type = EVENT_CAN_RECEIVED,
		.frame = *frame,
	};

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	if ((frame->flags & CAN_FRAME_RTR) != 0) {
		return;
	}
	if (k_msgq_put(&gateway_events, &event, K_NO_WAIT) != 0) {
		LOG_WRN("Gateway event queue full; dropping received CAN frame");
	}
}

static void on_zenoh_sample(z_loaned_sample_t *sample, void *context)
{
	const z_loaned_bytes_t *payload = z_sample_payload(sample);
	z_view_string_t key;
	size_t len = z_bytes_len(payload);
	uint8_t data[CAN_MAX_DLEN];
	uint32_t id;
	z_bytes_reader_t reader;

	ARG_UNUSED(context);
	if (len > CAN_MAX_DLEN) {
		LOG_WRN("Zenoh payload is %u bytes; classic CAN allows at most %u",
			(unsigned int)len, CAN_MAX_DLEN);
		return;
	}
	z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
	if (can_id_from_key(z_string_data(z_loan(key)), z_string_len(z_loan(key)),
			    &id) != 0) {
		LOG_WRN("Ignoring invalid CAN transmit key");
		return;
	}

	reader = z_bytes_get_reader(payload);
	if (z_bytes_reader_read(&reader, data, len) != len) {
		LOG_WRN("Could not read Zenoh payload");
		return;
	}
	queue_can_transmit(id, data, len);
}

static int configure_hardware(void)
{
	const struct can_filter filter = {
		.id = 0,
		.mask = 0,
	};
	int ret;

	if (!device_is_ready(can_dev) || !gpio_is_ready_dt(&led)) {
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
		return -EIO;
	}

	ret = can_add_rx_filter(can_dev, on_can_received, NULL, &filter);
	if (ret < 0) {
		return ret;
	}
	return can_start(can_dev);
}

int main(void)
{
	char locator[LOCATOR_SIZE];
	char subscribe_key[KEY_SIZE];
	struct gateway_event event;

	if (configure_hardware() < 0) {
		LOG_ERR("Could not initialize CAN or LED");
		return 0;
	}

	(void)snprintf(locator, sizeof(locator), "serial/%s#baudrate=%d",
		DEVICE_DT_NAME(ZENOH_UART_NODE), CONFIG_APP_ZENOH_BAUDRATE);
	(void)snprintf(subscribe_key, sizeof(subscribe_key), "%s/*/tx",
		CONFIG_APP_ZENOH_KEY_PREFIX);

	z_owned_config_t config;
	z_config_default(&config);
	if (zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client") < 0 ||
	    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator) < 0) {
		LOG_ERR("Failed to create Zenoh configuration");
		z_drop(z_move(config));
		return 0;
	}

	z_owned_session_t session;
	if (z_open(&session, z_move(config), NULL) < 0) {
		LOG_ERR("Could not open Zenoh session at %s", locator);
		return 0;
	}

	z_view_keyexpr_t sub_key;
	z_view_keyexpr_from_str_unchecked(&sub_key, subscribe_key);
	z_owned_closure_sample_t callback;
	z_closure(&callback, on_zenoh_sample, NULL, NULL);
	z_owned_subscriber_t subscriber;
	if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(sub_key),
				 z_move(callback), NULL) < 0) {
		LOG_ERR("Could not subscribe to %s", subscribe_key);
		z_drop(z_move(session));
		return 0;
	}

	LOG_INF("Ready: CAN -> %s/<ID>/rx, %s -> CAN", 
		CONFIG_APP_ZENOH_KEY_PREFIX, subscribe_key);
	while (true) {
		k_msgq_get(&gateway_events, &event, K_FOREVER);
		if (event.type == EVENT_CAN_RECEIVED) {
			char publish_key[KEY_SIZE];
			z_view_keyexpr_t pub_key;
			z_owned_bytes_t payload;

			(void)snprintf(publish_key, sizeof(publish_key), "%s/%03x/rx",
				CONFIG_APP_ZENOH_KEY_PREFIX, event.frame.id);
			z_view_keyexpr_from_str_unchecked(&pub_key, publish_key);
			z_bytes_copy_from_buf(&payload, event.frame.data,
					      can_dlc_to_bytes(event.frame.dlc));
			if (z_put(z_loan(session), z_loan(pub_key), z_move(payload), NULL) < 0) {
				LOG_WRN("Failed to publish received CAN frame");
			} else {
				gpio_pin_toggle_dt(&led);
				LOG_INF("CAN 0x%03x -> %s (%u bytes)", event.frame.id,
					publish_key, can_dlc_to_bytes(event.frame.dlc));
			}
		} else {
			int ret = can_send(can_dev, &event.frame, K_MSEC(100), NULL, NULL);
			if (ret != 0) {
				LOG_WRN("CAN transmit failed (%d)", ret);
			} else {
				LOG_INF("%s -> CAN 0x%03x (%u bytes)", subscribe_key,
					event.frame.id, can_dlc_to_bytes(event.frame.dlc));
			}
		}
	}

	return 0;
}
