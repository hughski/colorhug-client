/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <string.h>

#include "ch-common.h"

/**
 * ch_strerror:
 **/
const gchar *
ch_strerror (ChError error_enum)
{
	const char *str = NULL;
	switch (error_enum) {
	case CH_ERROR_NONE:
		str = "Success";
		break;
	case CH_ERROR_UNKNOWN_CMD:
		str = "Unknown command";
		break;
	case CH_ERROR_WRONG_UNLOCK_CODE:
		str = "Wrong unlock code";
		break;
	case CH_ERROR_NOT_IMPLEMENTED:
		str = "Not implemented";
		break;
	case CH_ERROR_UNDERFLOW_SENSOR:
		str = "Underflow of sensor";
		break;
	case CH_ERROR_NO_SERIAL:
		str = "No serial";
		break;
	case CH_ERROR_WATCHDOG:
		str = "Watchdog";
		break;
	case CH_ERROR_INVALID_ADDRESS:
		str = "Invalid address";
		break;
	case CH_ERROR_INVALID_LENGTH:
		str = "Invalid length";
		break;
	case CH_ERROR_INVALID_CHECKSUM:
		str = "Invalid checksum";
		break;
	case CH_ERROR_INVALID_VALUE:
		str = "Invalid value";
		break;
	case CH_ERROR_UNKNOWN_CMD_FOR_BOOTLOADER:
		str = "Unknown command for bootloader";
		break;
	case CH_ERROR_OVERFLOW_MULTIPLY:
		str = "Overflow of multiply";
		break;
	case CH_ERROR_OVERFLOW_ADDITION:
		str = "Overflow of addition";
		break;
	case CH_ERROR_OVERFLOW_SENSOR:
		str = "Overflow of sensor";
		break;
	case CH_ERROR_NO_CALIBRATION:
		str = "No calibration";
		break;
	default:
		str = "Unknown error, please report";
		break;
	}
	return str;
}

/**
 * ch_command_to_string:
 **/
const gchar *
ch_command_to_string (guint8 cmd)
{
	const char *str = NULL;
	switch (cmd) {
	case CH_CMD_GET_COLOR_SELECT:
		str = "get-color-select";
		break;
	case CH_CMD_SET_COLOR_SELECT:
		str = "set-color-select";
		break;
	case CH_CMD_GET_MULTIPLIER:
		str = "get-multiplier";
		break;
	case CH_CMD_SET_MULTIPLIER:
		str = "set-multiplier";
		break;
	case CH_CMD_GET_INTEGRAL_TIME:
		str = "get-integral-time";
		break;
	case CH_CMD_SET_INTEGRAL_TIME:
		str = "set-integral-time";
		break;
	case CH_CMD_GET_FIRMWARE_VERSION:
		str = "get-firmare-version";
		break;
	case CH_CMD_GET_CALIBRATION:
		str = "get-calibration";
		break;
	case CH_CMD_SET_CALIBRATION:
		str = "set-calibration";
		break;
	case CH_CMD_GET_SERIAL_NUMBER:
		str = "get-serial-number";
		break;
	case CH_CMD_SET_SERIAL_NUMBER:
		str = "set-serial-number";
		break;
	case CH_CMD_GET_LEDS:
		str = "get-leds";
		break;
	case CH_CMD_SET_LEDS:
		str = "set-leds";
		break;
	case CH_CMD_GET_DARK_OFFSETS:
		str = "get-dark-offsets";
		break;
	case CH_CMD_SET_DARK_OFFSETS:
		str = "set-dark-offsets";
		break;
	case CH_CMD_WRITE_EEPROM:
		str = "write-eeprom";
		break;
	case CH_CMD_TAKE_READING_RAW:
		str = "take-reading-raw";
		break;
	case CH_CMD_TAKE_READINGS:
		str = "take-readings";
		break;
	case CH_CMD_TAKE_READING_XYZ:
		str = "take-reading-xyz";
		break;
	case CH_CMD_RESET:
		str = "reset";
		break;
	case CH_CMD_READ_FLASH:
		str = "read-flash";
		break;
	case CH_CMD_ERASE_FLASH:
		str = "erase-flash";
		break;
	case CH_CMD_WRITE_FLASH:
		str = "write-flash";
		break;
	case CH_CMD_BOOT_FLASH:
		str = "boot-flash";
		break;
	case CH_CMD_SET_FLASH_SUCCESS:
		str = "set-flash-success";
		break;
	default:
		str = "unknown-command";
		break;
	}
	return str;
}
