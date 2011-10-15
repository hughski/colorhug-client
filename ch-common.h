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

#define	CH_USB_VID				0x0001
#define	CH_USB_PID				0x0002
#define	CH_USB_CONFIG				0x0003
#define	CH_USB_INTERFACE			0x0004

/* device commands */	
#define	CH_CMD_GET_COLOR_SELECT			0x00
#define	CH_CMD_SET_COLOR_SELECT			0x01
#define	CH_CMD_GET_MULTIPLIER			0x02
#define	CH_CMD_SET_MULTIPLIER			0x03
#define	CH_CMD_GET_INTERGRAL_TIME		0x04
#define	CH_CMD_SET_INTERGRAL_TIME		0x05
#define	CH_CMD_GET_FIRMWARE_VERSION		0x06
#define	CH_CMD_SET_FIRMWARE_VERSION		0x07
#define	CH_CMD_GET_CALIBRATION			0x08
#define	CH_CMD_SET_CALIBRATION			0x09
#define	CH_CMD_GET_SERIAL_NUMBER		0x0a
#define	CH_CMD_SET_SERIAL_NUMBER		0x0b
#define	CH_CMD_GET_WRITE_PROTECT		0x0c
#define	CH_CMD_SET_WRITE_PROTECT		0x0d
#define	CH_CMD_TAKE_READING			0x0e
#define	CH_CMD_TAKE_READING_XYZ			0x0f

/* secret code */	
#define	CH_WRITE_PROTECT_MAGIC			"Un1c0rn2"

/* EEPROM addresses */	
#define	CH_EEPROM_ADDR_SERIAL			0x00 /* 10 bytes */
#define	CH_EEPROM_ADDR_FIRMWARE_MAJOR		0x0a /* 2 bytes */
#define	CH_EEPROM_ADDR_FIRMWARE_MINOR		0x0c /* 2 bytes */
#define	CH_EEPROM_ADDR_FIRMWARE_MICRO		0x0e /* 2 bytes */
#define	CH_EEPROM_ADDR_CALIBRATION_MATRIX	0x10 /* 36 bytes */

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

