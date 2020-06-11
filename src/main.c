/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <bsd.h>
#include <lte_lc.h>
#include <at_cmd.h>
#include <at_notif.h>
#include <net/mqtt.h>
#include <net/socket.h>
#include <net/bsdlib.h>
#include <net/aws_jobs.h>
#include <net/aws_fota.h>
#include <dfu/mcuboot.h>
#include <power/reboot.h>
#include <device.h>
#include <drivers/sensor.h>
#include <sys/__assert.h>

BUILD_ASSERT_MSG(!IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT),
			"This sample does not support auto init and connect");

#if defined(CONFIG_USE_NRF_CLOUD)
#define NRF_CLOUD_SECURITY_TAG 16842753
#endif

#if defined(CONFIG_PROVISION_CERTIFICATES)
#include "certificates.h"
#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem_key_mgmt.h>
#endif
#endif

#include "comms.h"

#define IMEI_LEN 15
#define CLIENT_ID_LEN (IMEI_LEN + 4)

static u8_t client_id_buf[CLIENT_ID_LEN+1];

/* Buffers for MQTT client. */
static u8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

/* The mqtt client struct */
static struct mqtt_client client;

/* MQTT Broker details. */
static struct sockaddr_storage broker_storage;

/* File descriptor */
static struct pollfd fds;

/* Connected flag */
static bool connected;

/* Set to true when application should teardown and reboot */
static bool do_reboot;

/* Requested flag */
static struct request requested;

#if defined(CONFIG_BSD_LIBRARY)
/**@brief Recoverable BSD library error. */
void bsd_recoverable_error_handler(uint32_t err)
{
	printk("bsdlib recoverable error: %u\n", err);
}
#endif /* defined(CONFIG_BSD_LIBRARY) */

/* BH1749 device sensor threshold definition */
#define THRESHOLD_UPPER                 50
#define THRESHOLD_LOWER                 0
#define TRIGGER_ON_DATA_READY           0

#define pow2(x) ((x) * (x))

static K_SEM_DEFINE(sem_bh1749, 0, 1);
static K_SEM_DEFINE(sem_adxl372, 0, 1);

#if !defined(CONFIG_USE_NRF_CLOUD)
/* Topic for updating shadow topic with version number */
#define AWS "$aws/things/"
#define UPDATE_DELTA_TOPIC AWS "%s/shadow/update"
#define SHADOW_STATE_UPDATE \
"{\"state\":{\"reported\":{\"nrfcloud__dfu_v1__app_v\":\"%s\"}}}"

/* Variables for sensor data */
struct sensor_value temp, press, humidity, gas_res;
struct sensor_value accel[3];

static int update_device_shadow_version(struct mqtt_client *const client)
{
	struct mqtt_publish_param param;
	char update_delta_topic[strlen(AWS) + strlen("/shadow/update") +
				CLIENT_ID_LEN + 1];
	u8_t shadow_update_payload[CONFIG_DEVICE_SHADOW_PAYLOAD_SIZE];

	int ret = snprintf(update_delta_topic,
			   sizeof(update_delta_topic),
			   UPDATE_DELTA_TOPIC,
			   client->client_id.utf8);
	u32_t update_delta_topic_len = ret;

	if (ret >= sizeof(update_delta_topic)) {
		return -ENOMEM;
	} else if (ret < 0) {
		return ret;
	}

	ret = snprintf(shadow_update_payload,
		       sizeof(shadow_update_payload),
		       SHADOW_STATE_UPDATE,
		       CONFIG_APP_VERSION);
	u32_t shadow_update_payload_len = ret;

	if (ret >= sizeof(shadow_update_payload)) {
		return -ENOMEM;
	} else if (ret < 0) {
		return ret;
	}

	param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	param.message.topic.topic.utf8 = update_delta_topic;
	param.message.topic.topic.size = update_delta_topic_len;
	param.message.payload.data = shadow_update_payload;
	param.message.payload.len = shadow_update_payload_len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	return mqtt_publish(client, &param);
}
#endif /* !defined(CONFIG_USE_NRF_CLOUD) */

/**@brief Function to print strings without null-termination. */
static void data_print(u8_t *prefix, u8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	printk("%s%s\n", prefix, buf);
}

/**@brief Function to publish data on the configured topic
 */
static int data_publish(struct mqtt_client *c, enum mqtt_qos qos, u8_t *topic,
	u8_t *data, size_t len)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	printk("to topic: %s len: %u\n",
		topic,
		(unsigned int)strlen(topic));

	return mqtt_publish(c, &param);
}

/**@brief Function to subscribe to the configured topic
 */
static int subscribe(u8_t *topic)
{
	struct mqtt_topic subscribe_topic = {
		.topic = {
			.utf8 = topic,
			.size = strlen(topic)
		},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE
	};

	const struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic,
		.list_count = 1,
		.message_id = 1000
	};

	printk("Subscribing to: %s len %u\n", topic,
		(unsigned int)strlen(topic));

	return mqtt_subscribe(&client, &subscription_list);
}


/**@brief Function to read the published payload.
 */
static int publish_get_payload(struct mqtt_client *c,
			       u8_t *write_buf,
			       size_t length)
{
	u8_t *buf = write_buf;
	u8_t *end = buf + length;

	if (length > sizeof(payload_buf)) {
		return -EMSGSIZE;
	}
	while (buf < end) {
		int ret = mqtt_read_publish_payload(c, buf, end - buf);

		if (ret < 0) {
			int err;

			if (ret != -EAGAIN) {
				return ret;
			}

			printk("mqtt_read_publish_payload: EAGAIN\n");

			err = poll(&fds, 1, K_SECONDS(CONFIG_MQTT_KEEPALIVE));
			if (err > 0 && (fds.revents & POLLIN) == POLLIN) {
				continue;
			} else {
				return -EIO;
			}
		}

		if (ret == 0) {
			return -EIO;
		}

		buf += ret;
	}
	return 0;
}

/**@brief MQTT client event handler */
void mqtt_evt_handler(struct mqtt_client * const c,
		      const struct mqtt_evt *evt)
{
	int err;

	err = aws_fota_mqtt_evt_handler(c, evt);
	if (err == 0) {
		/* Event handled by FOTA library so we can skip it */
		return;
	} else if (err < 0) {
		printk("aws_fota_mqtt_evt_handler: Failed! %d\n", err);
	}

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			printk("MQTT connect failed %d\n", evt->result);
			break;
		}

		connected = true;
		err = subscribe(SSLA_TOPIC);
		if (err) {
			printk("Unable to subsribe to topic %s: %d\n", SSLA_TOPIC, err);
		}
		printk("[%s:%d] MQTT client connected!\n", __func__, __LINE__);
#if !defined(CONFIG_USE_NRF_CLOUD)
		err = update_device_shadow_version(c);
		if (err) {
			printk("Unable to update device shadow err: %d\n", err);
		}
#endif
		break;

	case MQTT_EVT_DISCONNECT:
		printk("[%s:%d] MQTT client disconnected %d\n", __func__,
		       __LINE__, evt->result);
		connected = false;
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &evt->param.publish;

		printk("[%s:%d] MQTT PUBLISH result=%d len=%d\n", __func__,
		       __LINE__, evt->result, p->message.payload.len);
		err = publish_get_payload(c,
					  payload_buf,
					  p->message.payload.len);
		if (err < 0) {
			printk("mqtt_read_publish_payload: Failed! %d\n", err);
			printk("Disconnecting MQTT client...\n");

			err = mqtt_disconnect(c);
			if (err) {
				printk("Could not disconnect: %d\n", err);
			}
		}
		else {
			data_print("Received: ", payload_buf, p->message.payload.len);

			/*if (strstr(requested, payload_buf)) {
				printk("requested publish all data \n");
			}*/
		}

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = p->message_id
			};

			/* Send acknowledgment. */
			err = mqtt_publish_qos1_ack(c, &ack);
			if (err) {
				printk("unable to ack\n");
			}
		}

		break;
	}

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			printk("MQTT PUBACK error %d\n", evt->result);
			break;
		}

		printk("[%s:%d] PUBACK packet id: %u\n", __func__, __LINE__,
		       evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0) {
			printk("MQTT SUBACK error %d\n", evt->result);
			break;
		}

		printk("[%s:%d] SUBACK packet id: %u\n", __func__, __LINE__,
		       evt->param.suback.message_id);
		break;

	default:
		printk("[%s:%d] default: %d\n", __func__, __LINE__,
		       evt->type);
		break;
	}
}


/**@brief Resolves the configured hostname and
 * initializes the MQTT broker structure
 */
static void broker_init(const char *hostname)
{
	int err;
	char addr_str[INET6_ADDRSTRLEN];
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

	err = getaddrinfo(hostname, NULL, &hints, &result);
	if (err) {
		printk("ERROR: getaddrinfo failed %d\n", err);

		return;
	}

	addr = result;
	err = -ENOENT;

	while (addr != NULL) {
		/* IPv4 Address. */
		if (addr->ai_addrlen == sizeof(struct sockaddr_in)) {
			struct sockaddr_in *broker =
				((struct sockaddr_in *)&broker_storage);

			broker->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
				->sin_addr.s_addr;
			broker->sin_family = AF_INET;
			broker->sin_port = htons(CONFIG_MQTT_BROKER_PORT);

			inet_ntop(AF_INET, &broker->sin_addr, addr_str,
				  sizeof(addr_str));
			printk("IPv4 Address %s\n", addr_str);
			break;
		} else if (addr->ai_addrlen == sizeof(struct sockaddr_in6)) {
			/* IPv6 Address. */
			struct sockaddr_in6 *broker =
				((struct sockaddr_in6 *)&broker_storage);

			memcpy(broker->sin6_addr.s6_addr,
				((struct sockaddr_in6 *)addr->ai_addr)
				->sin6_addr.s6_addr,
				sizeof(struct in6_addr));
			broker->sin6_family = AF_INET6;
			broker->sin6_port = htons(CONFIG_MQTT_BROKER_PORT);

			inet_ntop(AF_INET6, &broker->sin6_addr, addr_str,
				  sizeof(addr_str));
			printk("IPv6 Address %s\n", addr_str);
			break;
		} else {
			printk("error: ai_addrlen = %u should be %u or %u\n",
				(unsigned int)addr->ai_addrlen,
				(unsigned int)sizeof(struct sockaddr_in),
				(unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
		break;
	}

	/* Free the address. */
	freeaddrinfo(result);
}
#if defined(CONFIG_PROVISION_CERTIFICATES)
#warning Not for prodcution use. This should only be used once to provisioning the certificates please deselect the provision certificates configuration and compile again.
#define MAX_OF_2 MAX(sizeof(CLOUD_CA_CERTIFICATE),\
		     sizeof(CLOUD_CLIENT_PRIVATE_KEY))
#define MAX_LEN MAX(MAX_OF_2, sizeof(CLOUD_CLIENT_PUBLIC_CERTIFICATE))
static u8_t certificates[][MAX_LEN] = {{CLOUD_CA_CERTIFICATE},
				       {CLOUD_CLIENT_PRIVATE_KEY},
				       {CLOUD_CLIENT_PUBLIC_CERTIFICATE} };
static const size_t cert_len[] = {
	sizeof(CLOUD_CA_CERTIFICATE) - 1, sizeof(CLOUD_CLIENT_PRIVATE_KEY) - 1,
	sizeof(CLOUD_CLIENT_PUBLIC_CERTIFICATE) - 1
};
static int provision_certificates(void)
{
	int err;

	printk("************************* WARNING *************************\n");
	printk("%s called do not use this in production!\n", __func__);
	printk("This will store the certificates in readable flash and leave\n");
	printk("them exposed on modem_traces. Only use this once for\n");
	printk("provisioning certificates for development to reduce flash tear."
		"\n");
	printk("************************* WARNING *************************\n");
	nrf_sec_tag_t sec_tag = CONFIG_CLOUD_CERT_SEC_TAG;
	enum modem_key_mgnt_cred_type cred[] = {
		MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
		MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
		MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
	};

	/* Delete certificates */
	for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
		err = modem_key_mgmt_delete(sec_tag, type);
		printk("modem_key_mgmt_delete(%u, %d) => result=%d\n",
				sec_tag, type, err);
	}

	/* Write certificates */
	for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
		err = modem_key_mgmt_write(sec_tag, cred[type],
				certificates[type], cert_len[type]);
		printk("modem_key_mgmt_write => result=%d\n", err);
	}
	return 0;
}
#endif

static int client_id_get(char *id_buf)
{
	enum at_cmd_state at_state;
	char imei_buf[IMEI_LEN + 5];
	int err = at_cmd_write("AT+CGSN", imei_buf, (IMEI_LEN + 5), &at_state);

	if (err) {
		printk("Error when trying to do at_cmd_write: %d, at_state: %d",
			err, at_state);
	}

	snprintf(id_buf, CLIENT_ID_LEN + 1, "nrf-%s", imei_buf);

	return 0;
}

/**@brief Initialize the MQTT client structure */
static int client_init(struct mqtt_client *client, char *hostname)
{
	mqtt_client_init(client);
	broker_init(hostname);
	int ret = client_id_get(client_id_buf);

	printk("client_id: %s\n", client_id_buf);
	if (ret != 0) {
		return ret;
	}

	/* MQTT client configuration */
	client->broker = &broker_storage;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = client_id_buf;
	client->client_id.size = strlen(client_id_buf);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_SECURE;

	static sec_tag_t sec_tag_list[] = {
#ifdef CONFIG_USE_NRF_CLOUD
		NRF_CLOUD_SECURITY_TAG
#else
		CONFIG_CLOUD_CERT_SEC_TAG
#endif
	};
	struct mqtt_sec_config *tls_config = &(client->transport).tls.config;

	tls_config->peer_verify = 2;
	tls_config->cipher_list = NULL;
	tls_config->cipher_count = 0;
	tls_config->sec_tag_count = ARRAY_SIZE(sec_tag_list);
	tls_config->sec_tag_list = sec_tag_list;
	tls_config->hostname = hostname;

	return 0;
}

/**@brief Initialize the file descriptor structure used by poll. */
static int fds_init(struct mqtt_client *c)
{
	fds.fd = c->transport.tls.sock;
	fds.events = POLLIN;
	return 0;
}

/**@brief Configures AT Command interface to the modem link. */
static void at_configure(void)
{
	int err;

	err = at_notif_init();
	__ASSERT(err == 0, "AT Notify could not be initialized.");
	err = at_cmd_init();
	__ASSERT(err == 0, "AT CMD could not be established.");
}

static void aws_fota_cb_handler(enum aws_fota_evt_id evt)
{
	int err;

	switch (evt) {
	case AWS_FOTA_EVT_DONE:
		printk("AWS_FOTA_EVT_DONE, rebooting to apply update.\n");
		do_reboot = true;
		break;

	case AWS_FOTA_EVT_ERASE_PENDING:
		printk("AWS_FOTA_EVT_ERASE_PENDING, reboot or disconnect the "
		       "LTE link\n");
		err = lte_lc_offline();
		if (err) {
			printk("Error turning off the LTE link\n");
		}
		break;

	case AWS_FOTA_EVT_ERASE_DONE:
		printk("AWS_FOTA_EVT_ERASE_DONE, reconnecting the LTE link\n");
		err = lte_lc_connect();
		if (err) {
			printk("Error reconnecting the LTE link\n");
		}
		break;

	case AWS_FOTA_EVT_ERROR:
		printk("AWS_FOTA_EVT_ERROR\n");
		break;
	}
}

/* This sets up  the sensor to signal valid data when a threshold
 * value is reached.
 */
static void get_color_data(struct device *dev)
{
	int ret;
	struct sensor_value temp_val;

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_ALL);
	/* The sensor does only support fetching SENSOR_CHAN_ALL */
	if (ret) {
		printk("sensor_sample_fetch failed ret %d\n", ret);
		return;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_RED, &temp_val);
	if (ret) {
		printk("sensor_channel_get failed ret %d\n", ret);
		return;
	}
	printk("BH1749 RED: %d\n", temp_val.val1);

	ret = sensor_channel_get(dev, SENSOR_CHAN_GREEN, &temp_val);
	if (ret) {
		printk("sensor_channel_get failed ret %d\n", ret);
		return;
	}
	printk("BH1749 GREEN: %d\n", temp_val.val1);

	ret = sensor_channel_get(dev, SENSOR_CHAN_BLUE, &temp_val);
	if (ret) {
		printk("sensor_channel_get failed ret %d\n", ret);
		return;
	}
	printk("BH1749 BLUE: %d\n", temp_val.val1);

	ret = sensor_channel_get(dev, SENSOR_CHAN_IR, &temp_val);
	if (ret) {
		printk("sensor_channel_get failed ret %d\n", ret);
		return;
	}
	printk("BH1749 IR: %d\n", temp_val.val1);
	
}

void main(void)
{
	int err;

	printk("MQTT AWS Jobs FOTA Sample, version: %s\n", CONFIG_APP_VERSION);
	printk("Initializing bsdlib\n");
	err = bsdlib_init();
	switch (err) {
	case MODEM_DFU_RESULT_OK:
		printk("Modem firmware update successful!\n");
		printk("Modem will run the new firmware after reboot\n");
		k_thread_suspend(k_current_get());
		break;
	case MODEM_DFU_RESULT_UUID_ERROR:
	case MODEM_DFU_RESULT_AUTH_ERROR:
		printk("Modem firmware update failed\n");
		printk("Modem will run non-updated firmware on reboot.\n");
		break;
	case MODEM_DFU_RESULT_HARDWARE_ERROR:
	case MODEM_DFU_RESULT_INTERNAL_ERROR:
		printk("Modem firmware update failed\n");
		printk("Fatal error.\n");
		break;
	case -1:
		printk("Could not initialize bsdlib.\n");
		printk("Fatal error.\n");
		return;
	default:
		break;
	}
	printk("Initialized bsdlib\n");

	at_configure();

#if defined(CONFIG_PROVISION_CERTIFICATES)
	provision_certificates();
#endif /* CONFIG_PROVISION_CERTIFICATES */

	// Setup ePCO
	enum at_cmd_state at_state;

	err = at_cmd_write("AT%XEPCO=0", NULL, 0, &at_state);
	if (err) {
		printk("Error when trying to do at_cmd_write: %d, at_state: %d", err, at_state);
	}

	printk("LTE Link Connecting ...\n");
	err = lte_lc_init_and_connect();
	__ASSERT(err == 0, "LTE link could not be established.");
	printk("LTE Link Connected!\n");
        printk("APP_VERSION=%s\n", CONFIG_APP_VERSION);

	client_init(&client, CONFIG_MQTT_BROKER_HOSTNAME);

	err = aws_fota_init(&client, aws_fota_cb_handler);
	if (err != 0) {
		printk("ERROR: aws_fota_init %d\n", err);
		return;
	}

	err = mqtt_connect(&client);
	if (err != 0) {
		printk("ERROR: mqtt_connect %d\n", err);
		return;
	}

	err = fds_init(&client);
	if (err != 0) {
		printk("ERROR: fds_init %d\n", err);
		return;
	}

	/* All initializations were successful mark image as working so that we
	 * will not revert upon reboot.
	 */
	boot_write_img_confirmed();

	/* Light sensor BH1749
	 *
	 */
	struct device *dev_bh1749;

	if (IS_ENABLED(CONFIG_LOG_BACKEND_RTT)) {
		/* Give RTT log time to be flushed before executing tests */
		k_sleep(500);
	}
	dev_bh1749 = device_get_binding("BH1749");
	if (dev_bh1749 == NULL) {
		printk("Failed to get device BH1749 binding\n");
		return;
	}
	printk("device is %p, name is %s\n", dev_bh1749, dev_bh1749->config->name);
	

	/* Accelerometer sensor ADXL372
	 *
	 */
	struct device *dev_adxl372 = device_get_binding("ADXL372");
	if (dev_adxl372 == NULL) {
		printk("Could not get accelerometer ADXL372 device\n");
		return;
	}
	printk("device is %p, name is %s\n", dev_adxl372, dev_adxl372->config->name);

	/* Environmental sensor BME280
	 *
	 */
	struct device *dev_bme680 = device_get_binding("BME680");

	printf("device is %p, name is %s\n", dev_bme680, dev_bme680->config->name);

	// BH1749
	get_color_data(dev_bh1749);

	// ADXL372
	if (sensor_sample_fetch(dev_adxl372)) {
		printk("sensor_sample_fetch failed\n");
	}

	sensor_channel_get(dev_adxl372, SENSOR_CHAN_ACCEL_XYZ, accel);

	printf("AX=%.2f AY=%.2f AZ=%.2f (m/s^2)\n",
		sensor_value_to_double(&accel[0]),
		sensor_value_to_double(&accel[1]),
		sensor_value_to_double(&accel[2]));

	// BME680
	sensor_sample_fetch(dev_bme680);
	sensor_channel_get(dev_bme680, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(dev_bme680, SENSOR_CHAN_PRESS, &press);
	sensor_channel_get(dev_bme680, SENSOR_CHAN_HUMIDITY, &humidity);
	sensor_channel_get(dev_bme680, SENSOR_CHAN_GAS_RES, &gas_res);

	printk("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d\n",
			temp.val1, temp.val2, press.val1, press.val2,
			humidity.val1, humidity.val2, gas_res.val1,
			gas_res.val2);

	while (1) {
		err = poll(&fds, 1, K_SECONDS(10));
		if (err < 0) {
			printk("ERROR: poll %d\n", errno);
			break;
		}

		err = mqtt_live(&client);
		if ((err != 0) && (err != -EAGAIN)) {
			printk("ERROR: mqtt_live %d\n", err);
			break;
		}

		if ((fds.revents & POLLIN) == POLLIN) {
			err = mqtt_input(&client);
			if (err != 0) {
				printk("ERROR: mqtt_input %d\n", err);
				break;
			}
		}

		if ((fds.revents & POLLERR) == POLLERR) {
			printk("POLLERR\n");
			break;
		}

		if ((fds.revents & POLLNVAL) == POLLNVAL) {
			printk("POLLNVAL\n");
			break;
		}
                // if the requested is a publish and it is from the application then publish it
		if (requested.tag == PUBLISH_ALL && requested.dest_device_id == 1000) {

			sensor_sample_fetch(dev_bme680);
			sensor_channel_get(dev_bme680, SENSOR_CHAN_AMBIENT_TEMP, &temp);
			sensor_channel_get(dev_bme680, SENSOR_CHAN_PRESS, &press);
			sensor_channel_get(dev_bme680, SENSOR_CHAN_HUMIDITY, &humidity);
			sensor_channel_get(dev_bme680, SENSOR_CHAN_GAS_RES, &gas_res);

			/* Build the MQTT string */
			char buffer[128];
			snprintf(buffer, sizeof(buffer), "T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d\n",
					temp.val1, temp.val2, press.val1, press.val2,
					humidity.val1, humidity.val2, gas_res.val1,
					gas_res.val2);

			char message[5];
			sprintf(message, "%d", temp.val1);

			err = data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, SSLA_TOPIC, message, sizeof(message));

			if (err != 0) {
				printk("ERROR: mqtt_publish %d\n", err);
			}
			else {
				printk("SUCCESS: mqtt_publish %d\n", err);
			}
		}
		if (do_reboot) {
			/* Teardown */
			mqtt_disconnect(&client);
			sys_reboot(0);
		}

		k_sleep(K_MSEC(500));
	}

	printk("Disconnecting MQTT client...\n");

	err = mqtt_disconnect(&client);
	if (err) {
		printk("Could not disconnect MQTT client. Error: %d\n", err);
	}
}
