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
#include "ch-client.h"

#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <gusb.h>

static void
ch_test_state_func (void)
{
	ChClient *client;
	ChColorSelect color_select = 0;
	ChFreqScale multiplier = 0;
	gboolean ret;
	GError *error = NULL;
	guint16 integral_time = 0;

	/* new device */
	client = ch_client_new ();

	/* load the device */
	ret = ch_client_load (client, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify color select */
	ret = ch_client_set_color_select (client,
					  CH_COLOR_SELECT_BLUE,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_color_select (client,
					  &color_select,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (color_select, ==, CH_COLOR_SELECT_BLUE);

	/* verify multiplier */
	ret = ch_client_set_multiplier (client,
					CH_FREQ_SCALE_2,
					&error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_multiplier (client,
					&multiplier,
					&error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (multiplier, ==, CH_FREQ_SCALE_2);

	/* verify integral */
	ret = ch_client_set_integral_time (client,
					   100,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_integral_time (client,
					   &integral_time,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (integral_time, ==, 100);

	g_object_unref (client);
}

static void
ch_test_eeprom_func (void)
{
	ChClient *client;
	gboolean ret;
	gboolean write_protect = FALSE;
	GError *error = NULL;
	guint16 major = 0;
	guint16 micro = 0;
	guint16 minor = 0;
	guint64 serial_number = 0;
	gfloat *calibration = NULL;
	gfloat *calibration_tmp = NULL;

	/* new device */
	client = ch_client_new ();

	/* load the device */
	ret = ch_client_load (client, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* unset write protect */
	ret = ch_client_set_write_protect (client,
					   CH_WRITE_PROTECT_UNLOCK_MAGIC,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_write_protect (client,
					   &write_protect,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (write_protect);

	/* unset write protect */
	ret = ch_client_set_write_protect (client,
					   "hello dave",
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_write_protect (client,
					   &write_protect,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (write_protect);

	/* set write protect with magic */
	ret = ch_client_set_write_protect (client,
					   CH_WRITE_PROTECT_LOCK_MAGIC,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_write_protect (client,
					   &write_protect,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!write_protect);

	/* verify serial number */
	ret = ch_client_set_serial_number (client,
					   12345678,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_serial_number (client,
					   &serial_number,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (serial_number, ==, 12345678);

	/* verify firmware */
	ret = ch_client_set_firmware_ver (client,
					  1, 2, 3,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_firmware_ver (client,
					  &major,
					  &minor,
					  &micro,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (major, ==, 1);
	g_assert_cmpint (minor, ==, 2);
	g_assert_cmpint (micro, ==, 3);

	/* verify calibration */
	calibration = g_new0 (gfloat, 9);
	calibration[0] = 1.0f;
	calibration[1] = 2.0f;
	calibration[2] = 3.0f;
	calibration[3] = 4.0f;
	calibration[4] = 5.0f;
	calibration[5] = 6.0f;
	calibration[6] = 7.0f;
	calibration[7] = 8.0f;
	calibration[8] = 9.0f;
	ret = ch_client_set_calibration (client,
					 calibration,
					 &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_free (calibration);

	ret = ch_client_get_calibration (client,
					 &calibration_tmp,
					 &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (memcmp (calibration_tmp,
			  calibration,
			  sizeof (gfloat) * 9) == 0);
	g_free (calibration_tmp);

	g_object_unref (client);
}

static void
ch_test_reading_func (void)
{
	ChClient *client;
	gboolean ret;
	GError *error = NULL;
	guint16 take_reading = 0;

	/* new device */
	client = ch_client_new ();

	/* load the device */
	ret = ch_client_load (client, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* set color select */
	ret = ch_client_set_color_select (client,
					  CH_COLOR_SELECT_WHITE,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* set multiplier */
	ret = ch_client_set_multiplier (client,
					CH_FREQ_SCALE_2,
					&error);
	g_assert_no_error (error);
	g_assert (ret);

	/* set integral */
	ret = ch_client_set_integral_time (client,
					   100,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* take a reading from the hardware */
	ret = ch_client_take_reading (client,
				      &take_reading,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (take_reading, ==, 12345678);

	g_object_unref (client);
}

int
main (int argc, char **argv)
{
	g_type_init ();
	g_thread_init (NULL);
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/ColorHug/state", ch_test_state_func);
	g_test_add_func ("/ColorHug/eeprom", ch_test_eeprom_func);
	g_test_add_func ("/ColorHug/reading", ch_test_reading_func);
	return g_test_run ();
}

