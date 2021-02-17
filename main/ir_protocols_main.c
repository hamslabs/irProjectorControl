/* IR protocols example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/rmt.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "ir_tools.h"

#define USB_POWER_DETECT_PIN 5
#define LED_PIN 14

static const char *TAG = "example";

static rmt_channel_t example_tx_channel = RMT_CHANNEL_0;


/**
 * @brief Send a command and repeat
 *
 */

static void ir_tx_sendcommand(ir_builder_t *a_ir_builder, uint32_t a_addr, uint32_t a_cmd, uint32_t a_delay)
{
	rmt_item32_t *items = NULL;
	uint32_t length = 0;

	ESP_LOGI(TAG, "Send command 0x%x to address 0x%x", a_cmd, a_addr);

	// Send new key code
	ESP_ERROR_CHECK(a_ir_builder->build_frame(a_ir_builder, a_addr, a_cmd));
	ESP_ERROR_CHECK(a_ir_builder->get_result(a_ir_builder, &items, &length));

	//To send data according to the waveform items.
	rmt_write_items(example_tx_channel, items, length, false);

	// Send repeat code
	vTaskDelay(pdMS_TO_TICKS(a_ir_builder->repeat_period_ms));
	ESP_ERROR_CHECK(a_ir_builder->build_repeat_frame(a_ir_builder));
	ESP_ERROR_CHECK(a_ir_builder->get_result(a_ir_builder, &items, &length));
	rmt_write_items(example_tx_channel, items, length, false);

	if (a_delay)
		vTaskDelay(pdMS_TO_TICKS(a_delay));
	return;
}

/**
 * @brief RMT Transmit Task
 *
 */
static void example_ir_tx_task(void *arg)
{
	ir_builder_t *ir_builder = NULL;
	const uint64_t ext_wakeup_pin_mask = 1ULL << USB_POWER_DETECT_PIN;
	const uint64_t led_pin_mask = 1ULL << LED_PIN;

	gpio_config_t confUSBPower = {
		.pin_bit_mask = ext_wakeup_pin_mask,
		.mode = GPIO_MODE_INPUT,
		.pull_down_en = GPIO_PULLDOWN_ENABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&confUSBPower);

	gpio_config_t confLED = {
		.pin_bit_mask = led_pin_mask,
		.mode = GPIO_MODE_OUTPUT,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&confLED);
	gpio_set_level(LED_PIN, 0);

	ESP_LOGI(TAG, "Strted TX task");
	rmt_config_t rmt_tx_config = RMT_DEFAULT_CONFIG_TX(CONFIG_EXAMPLE_RMT_TX_GPIO, example_tx_channel);
	rmt_tx_config.tx_config.carrier_en = true;
	rmt_config(&rmt_tx_config);
	rmt_driver_install(example_tx_channel, 0, 0);
	ir_builder_config_t ir_builder_config = IR_BUILDER_DEFAULT_CONFIG((ir_dev_t)example_tx_channel);
	ir_builder_config.flags |= IR_TOOLS_FLAGS_PROTO_EXT; // Using extended IR protocols (both NEC and RC5 have extended version)
#if CONFIG_EXAMPLE_IR_PROTOCOL_NEC
	ir_builder = ir_builder_rmt_new_nec(&ir_builder_config);
#elif CONFIG_EXAMPLE_IR_PROTOCOL_RC5
	ir_builder = ir_builder_rmt_new_rc5(&ir_builder_config);
#endif

	// codes for the projector remote
	uint32_t addr		= 0xf483;
	uint32_t powerCmd	= 0xe817;
	uint32_t rightCmd	= 0xf00f;
	uint32_t downCmd	= 0xf30c;
	uint32_t OKCmd		= 0xea15;

	// delay a bit to make sure the projector is powered too
	vTaskDelay(pdMS_TO_TICKS(2000));
	while(1)
	{

		// power on the projector and wait a long time
		// sine it takes a while for it to be ready
		ir_tx_sendcommand(ir_builder, addr, powerCmd, 15000);

		// right-right-ok selects media on the USB flash
		ir_tx_sendcommand(ir_builder, addr, rightCmd, 500);
		ir_tx_sendcommand(ir_builder, addr, rightCmd, 500);
		ir_tx_sendcommand(ir_builder, addr, OKCmd, 2000);
	
		// down-ok plays the first file
		ir_tx_sendcommand(ir_builder, addr, downCmd, 500);
		ir_tx_sendcommand(ir_builder, addr, OKCmd, 500);
	
		// wait till power is off - the whole thing is turned off
		// Make sure its off for over 2 seconds
		int offCount = 0;
		while (offCount < 5)
		{
			if (gpio_get_level(USB_POWER_DETECT_PIN) == 0)
				offCount++;
			vTaskDelay(pdMS_TO_TICKS(500));
		}
	
		// power-power shuts off the projector
		gpio_set_level(LED_PIN, 1);
		ir_tx_sendcommand(ir_builder, addr, powerCmd, 1000);
		ir_tx_sendcommand(ir_builder, addr, powerCmd, 500);
	
		vTaskDelay(pdMS_TO_TICKS(2500));
		gpio_set_level(LED_PIN, 0);

		bool powerState = gpio_get_level(USB_POWER_DETECT_PIN);
		while(!powerState)
		{

			// go to deep sleep and wakeup when USB power is back
	    	esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
			rtc_gpio_isolate(GPIO_NUM_12);
			esp_deep_sleep_start();

			// wait a bit to see if it's just a glitch
			vTaskDelay(pdMS_TO_TICKS(500));

			// if USBPower is still high, not a glich, indicate we're awake
			powerState = gpio_get_level(USB_POWER_DETECT_PIN);
		}
	}


	// all done undo everything
	ir_builder->del(ir_builder);
	rmt_driver_uninstall(example_tx_channel);
	vTaskDelete(NULL);
}

void app_main(void)
{
	xTaskCreate(example_ir_tx_task, "ir_tx_task", 2048, NULL, 10, NULL);
}
