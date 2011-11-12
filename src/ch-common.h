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

#ifndef CH_COMMON_H
#define CH_COMMON_H

/* device constants */
#define	CH_USB_VID				0x04d8
#define	CH_USB_PID				0xf8da
#define	CH_USB_CONFIG				0x0001
#define	CH_USB_INTERFACE			0x0000
#define	CH_USB_HID_EP				0x0001
#define	CH_USB_HID_EP_IN			(CH_USB_HID_EP | 0x80)
#define	CH_USB_HID_EP_OUT			(CH_USB_HID_EP | 0x00)
#define	CH_USB_HID_EP_SIZE			64

/**
 * CH_CMD_GET_COLOR_SELECT:
 *
 * Get the color select state.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][1:color_select]
 **/
#define	CH_CMD_GET_COLOR_SELECT			0x01

/**
 * CH_CMD_SET_COLOR_SELECT:
 *
 * Set the color select state.
 *
 * IN:  [1:cmd][1:color_select]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_COLOR_SELECT			0x02

/**
 * CH_CMD_GET_MULTIPLIER:
 *
 * Gets the multiplier value.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][1:multiplier_value]
 **/
#define	CH_CMD_GET_MULTIPLIER			0x03

/**
 * CH_CMD_SET_MULTIPLIER:
 *
 * Sets the multiplier value.
 *
 * IN:  [1:cmd][1:multiplier_value]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_MULTIPLIER			0x04

/**
 * CH_CMD_GET_INTERGRAL_TIME:
 *
 * Gets the integral time.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][2:integral_time]
 **/
#define	CH_CMD_GET_INTERGRAL_TIME		0x05

/**
 * CH_CMD_SET_INTERGRAL_TIME:
 *
 * Sets the integral time.
 *
 * IN:  [1:cmd][2:integral_time]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_INTERGRAL_TIME		0x06

/**
 * CH_CMD_GET_FIRMWARE_VERSION:
 *
 * Gets the firmware version.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][2:major][2:minor][2:micro]
 **/
#define	CH_CMD_GET_FIRMWARE_VERSION		0x07

/**
 * CH_CMD_GET_CALIBRATION:
 *
 * Gets the calibration matrix.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][4*9:(float)matrix_value]
 **/
#define	CH_CMD_GET_CALIBRATION			0x09

/**
 * CH_CMD_SET_CALIBRATION:
 *
 * Sets the calibration matrix.
 *
 * IN:  [1:cmd][4*9:(float)matrix_value]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_CALIBRATION			0x0a

/**
 * CH_CMD_GET_SERIAL_NUMBER:
 *
 * Gets the device serial number.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][4:serial_number]
 **/
#define	CH_CMD_GET_SERIAL_NUMBER		0x0b

/**
 * CH_CMD_SET_SERIAL_NUMBER:
 *
 * Sets the device serial number.
 *
 * IN:  [1:cmd][4:serial_number]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_SERIAL_NUMBER		0x0c

/**
 * CH_CMD_GET_LEDS:
 *
 * Get the LED state.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][1:led_state]
 **/
#define	CH_CMD_GET_LEDS				0x0d

/**
 * CH_CMD_SET_LEDS:
 *
 * Set the LED state.
 *
 * IN:  [1:cmd][1:led_state]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_LEDS				0x0e

/**
 * CH_CMD_GET_DARK_OFFSETS:
 *
 * Get the dark offsets.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][2:red][2:green][2:blue]
 **/
#define	CH_CMD_GET_DARK_OFFSETS			0x0f

/**
 * CH_CMD_SET_DARK_OFFSETS:
 *
 * Set the dark offsets.
 *
 * IN:  [1:cmd][2:red][2:green][2:blue]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_SET_DARK_OFFSETS			0x10

/**
 * CH_CMD_WRITE_EEPROM:
 *
 * Write values to EEPROM.
 *
 * IN:  [1:cmd][8:eeprom_magic]
 * OUT: [1:retval][1:cmd]
 **/
#define	CH_CMD_WRITE_EEPROM			0x20

/**
 * CH_CMD_TAKE_READING_RAW:
 *
 * Take a raw reading.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][2:count]
 **/
#define	CH_CMD_TAKE_READING_RAW			0x21

/**
 * CH_CMD_TAKE_READINGS:
 *
 * Take a reading taking into account dark offsets.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][2:red][2:green][2:blue]
 **/
#define	CH_CMD_TAKE_READINGS			0x22

/**
 * CH_CMD_TAKE_READING_XYZ:
 *
 * Take a reading taking into account dark offsets and calibration.
 *
 * IN:  [1:cmd]
 * OUT: [1:retval][1:cmd][4*3:(float)value]
 **/
#define	CH_CMD_TAKE_READING_XYZ			0x23

/* secret code */
#define	CH_WRITE_EEPROM_MAGIC			"Un1c0rn2"

/* input and output buffer offsets */
#define	CH_BUFFER_INPUT_CMD			0x00
#define	CH_BUFFER_INPUT_DATA			0x01
#define	CH_BUFFER_OUTPUT_RETVAL			0x00
#define	CH_BUFFER_OUTPUT_CMD			0x01
#define	CH_BUFFER_OUTPUT_DATA			0x02

/* approximate sample times */
#define CH_INTEGRAL_TIME_VALUE_5MS		0x0300
#define CH_INTEGRAL_TIME_VALUE_50MS		0x1f00
#define CH_INTEGRAL_TIME_VALUE_100MS		0x3a00
#define CH_INTEGRAL_TIME_VALUE_200MS		0x7500
#define CH_INTEGRAL_TIME_VALUE_MAX		0xffff

/* which color to select */
typedef enum {
	CH_COLOR_SELECT_RED,
	CH_COLOR_SELECT_WHITE,
	CH_COLOR_SELECT_BLUE,
	CH_COLOR_SELECT_GREEN
} ChColorSelect;

/* what frequency divider to use */
typedef enum {
	CH_FREQ_SCALE_0,
	CH_FREQ_SCALE_20,
	CH_FREQ_SCALE_2,
	CH_FREQ_SCALE_100
} ChFreqScale;

/* fatal error morse code */
typedef enum {
	CH_FATAL_ERROR_NONE,
	CH_FATAL_ERROR_UNKNOWN_CMD,
	CH_FATAL_ERROR_WRONG_UNLOCK_CODE,
	CH_FATAL_ERROR_NOT_IMPLEMENTED,
	CH_FATAL_ERROR_UNDERFLOW,
	CH_FATAL_ERROR_NO_SERIAL,
	CH_FATAL_ERROR_LAST
} ChFatalError;

#endif
