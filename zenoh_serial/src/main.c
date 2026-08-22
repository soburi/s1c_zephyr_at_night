/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zenoh-pico.h>

LOG_MODULE_REGISTER(zenoh_serial, LOG_LEVEL_INF);

#define ZENOH_UART_NODE DT_ALIAS(zenoh_uart)

#if !DT_NODE_EXISTS(ZENOH_UART_NODE)
#error "The board overlay must define the zenoh-uart devicetree alias"
#endif

#define LOCATOR_SIZE 96
#define PAYLOAD_SIZE 96

static void on_sample(z_loaned_sample_t *sample, void *context)
{
	ARG_UNUSED(context);

	z_view_string_t key;
	z_owned_string_t payload;

	z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key);
	if (z_bytes_to_string(z_sample_payload(sample), &payload) < 0) {
		LOG_WRN("Received a payload that could not be converted to a string");
		return;
	}

	LOG_INF("RX %.*s = %.*s",
		(int)z_string_len(z_loan(key)), z_string_data(z_loan(key)),
		(int)z_string_len(z_loan(payload)), z_string_data(z_loan(payload)));
	z_drop(z_move(payload));
}

int main(void)
{
	char locator[LOCATOR_SIZE];
	char text[PAYLOAD_SIZE];
	unsigned int sequence = 0;

	(void)snprintf(locator, sizeof(locator), "serial/%s#baudrate=%d",
		DEVICE_DT_NAME(ZENOH_UART_NODE), CONFIG_APP_ZENOH_BAUDRATE);
	LOG_INF("Connecting to zenohd at %s", locator);

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
		LOG_ERR("Could not open the Zenoh session; check UART wiring and zenohd");
		return 0;
	}
	LOG_INF("Zenoh session opened");

	z_view_keyexpr_t sub_key;
	z_view_keyexpr_from_str_unchecked(&sub_key, CONFIG_APP_ZENOH_SUB_KEY);
	z_owned_closure_sample_t callback;
	z_closure(&callback, on_sample, NULL, NULL);
	z_owned_subscriber_t subscriber;
	if (z_declare_subscriber(z_loan(session), &subscriber, z_loan(sub_key),
				 z_move(callback), NULL) < 0) {
		LOG_ERR("Could not subscribe to %s", CONFIG_APP_ZENOH_SUB_KEY);
		z_drop(z_move(session));
		return 0;
	}

	z_view_keyexpr_t pub_key;
	z_view_keyexpr_from_str_unchecked(&pub_key, CONFIG_APP_ZENOH_PUB_KEY);
	z_owned_publisher_t publisher;
	if (z_declare_publisher(z_loan(session), &publisher, z_loan(pub_key), NULL) < 0) {
		LOG_ERR("Could not declare publisher for %s", CONFIG_APP_ZENOH_PUB_KEY);
		z_drop(z_move(subscriber));
		z_drop(z_move(session));
		return 0;
	}

	LOG_INF("PUB %s, SUB %s", CONFIG_APP_ZENOH_PUB_KEY,
		CONFIG_APP_ZENOH_SUB_KEY);
	while (true) {
		z_sleep_ms(CONFIG_APP_ZENOH_PUB_PERIOD_MS);
		(void)snprintf(text, sizeof(text), "hello from Zephyr #%u", sequence++);

		z_owned_bytes_t payload;
		z_bytes_copy_from_str(&payload, text);
		if (z_publisher_put(z_loan(publisher), z_move(payload), NULL) < 0) {
			LOG_WRN("Publish failed");
		} else {
			LOG_INF("TX %s = %s", CONFIG_APP_ZENOH_PUB_KEY, text);
		}
	}

	return 0;
}

