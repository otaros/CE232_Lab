#include <ssd1306.h>
#include <font8x8_basic.h>
#include <driver/i2c.h>
#include "sdkconfig.h"
#include <string.h>
#include "esp_log.h"

void ssd1306_init()
{
	esp_err_t espRc;

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_CMD_STREAM, true);

	i2c_master_write_byte(cmd, OLED_CMD_SET_CHARGE_PUMP, true);
	i2c_master_write_byte(cmd, 0x14, true);

	i2c_master_write_byte(cmd, OLED_CMD_SET_SEGMENT_REMAP, true); // reverse left-right mapping
	i2c_master_write_byte(cmd, OLED_CMD_SET_COM_SCAN_MODE, true); // reverse up-bottom mapping

	i2c_master_write_byte(cmd, OLED_CMD_DISPLAY_NORMAL, true);
	i2c_master_write_byte(cmd, OLED_CMD_DISPLAY_OFF, true);
	i2c_master_write_byte(cmd, OLED_CMD_DISPLAY_ON, true);
	i2c_master_stop(cmd);

	espRc = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
	if (espRc == ESP_OK)
	{
		ESP_LOGI(TAG, "OLED configured successfully");
	}
	else
	{
		ESP_LOGE(TAG, "OLED configuration failed. code: 0x%.2X", espRc);
	}
	i2c_cmd_link_delete(cmd);
}

void ssd1306_display_text(const void *arg_text)
{
	i2c_cmd_handle_t cmd;
	char *text = (char *)arg_text;
	uint8_t text_len = strlen(text);

	uint8_t cur_page = 0;

	cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);

	i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_CMD_STREAM, true);
	i2c_master_write_byte(cmd, OLED_CMD_SET_LOWER_COLUMN_START | 0, true);		  // reset column - choose column --> 0
	i2c_master_write_byte(cmd, OLED_CMD_SET_HIGHER_COLUMN_START | 0, true);		  // reset line - choose line --> 0
	i2c_master_write_byte(cmd, OLED_CMD_SET_PAGE_START_ADDRESS | cur_page, true); // reset page

	i2c_master_stop(cmd);
	i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);

	for (uint8_t i = 0; i < text_len; i++)
	{
		if (text[i] == '\n')
		{
			cmd = i2c_cmd_link_create();
			i2c_master_start(cmd);
			i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);

			i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_CMD_STREAM, true);
			i2c_master_write_byte(cmd, OLED_CMD_SET_LOWER_COLUMN_START | 0, true); // reset column
			i2c_master_write_byte(cmd, OLED_CMD_SET_HIGHER_COLUMN_START | 0, true);
			i2c_master_write_byte(cmd, OLED_CMD_SET_PAGE_START_ADDRESS | ++cur_page, true); // increment page

			i2c_master_stop(cmd);
			i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
			i2c_cmd_link_delete(cmd);
		}
		else
		{
			cmd = i2c_cmd_link_create();
			i2c_master_start(cmd);
			i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);

			i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_DATA_STREAM, true);
			i2c_master_write(cmd, font8x8_basic_tr[(uint8_t)text[i]], 8, true);

			i2c_master_stop(cmd);
			i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
			i2c_cmd_link_delete(cmd);
		}
	}
}

void ssd1306_display_clear()
{
	i2c_cmd_handle_t cmd;

	uint8_t clear[128];
	memset(clear, 0, sizeof(clear));
	for (uint8_t i = 0; i < 8; i++)
	{
		cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
		i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_CMD_SINGLE, true);
		i2c_master_write_byte(cmd, OLED_CMD_SET_PAGE_START_ADDRESS | i, true);

		i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_DATA_STREAM, true);
		i2c_master_write(cmd, clear, 128, true);
		i2c_master_stop(cmd);
		esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
		if (ret != ESP_OK)
		{
			ESP_LOGE(TAG, "Error: i2c_master_cmd_begin() failed, ret=%d", ret);
		}
		i2c_cmd_link_delete(cmd);
	}
}

void ssd1306_draw_bitmap(const uint8_t *bitmap, uint8_t x, uint8_t y, uint8_t height, uint8_t width)
{
	i2c_cmd_handle_t cmd;
	uint8_t page_start, page_end;
	uint8_t column_start, column_end;
	uint8_t page, column;

	// Calculate the start and end pages and columns for the bitmap
	page_start = y / 8;
	page_end = (y + height - 1) / 8;
	column_start = x;
	column_end = x + width - 1;

	// Loop through each page and column, writing the bitmap data to the display
	for (page = page_start; page <= page_end; page++)
	{
		cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);

		i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_CMD_STREAM, true);
		i2c_master_write_byte(cmd, OLED_CMD_SET_LOWER_COLUMN_START | 0, true); // reset column
		i2c_master_write_byte(cmd, OLED_CMD_SET_HIGHER_COLUMN_START, true);
		i2c_master_write_byte(cmd, OLED_CMD_SET_PAGE_START_ADDRESS | page, true); // set page

		i2c_master_stop(cmd);
		i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
		i2c_cmd_link_delete(cmd);

		for (column = column_start; column <= column_end; column++)
		{
			uint8_t data = bitmap[(page - page_start) * width + (column - column_start)];
			cmd = i2c_cmd_link_create();
			i2c_master_start(cmd);
			i2c_master_write_byte(cmd, (OLED_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);

			i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_DATA_STREAM, true);
			i2c_master_write_byte(cmd, data, true);

			i2c_master_stop(cmd);
			i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
			i2c_cmd_link_delete(cmd);
		}
	}
}
