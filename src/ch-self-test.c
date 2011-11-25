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
#include "ch-math.h"

#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <gusb.h>
#include <math.h>

static void
ch_test_math_convert_func (void)
{
	ChPackedFloat pf;
	gdouble value = 0.0f;

	/* test packing */
	g_assert_cmpint (sizeof (ChPackedFloat), ==, 4);

	/* test converting to packed struct */
	value = 3.1415927f;
	ch_double_to_packed_float (value, &pf);
	g_assert_cmpint (pf.offset, ==, 3);
	g_assert_cmpint (pf.fraction, <, 0x249f);
	g_assert_cmpint (pf.fraction, >, 0x240f);

	/* test converting to packed struct */
	value = -3.1415927f;
	ch_double_to_packed_float (value, &pf);
	g_assert_cmpint (pf.offset, ==, -4);
	g_assert_cmpint (pf.fraction, <, (0x240f ^ 0xffff));
	g_assert_cmpint (pf.fraction, >, (0x249f ^ 0xffff));

	/* test converting positive to float */
	pf.offset = 3;
	pf.fraction = 0x243c;
	ch_packed_float_to_double (&pf, &value);
	g_assert_cmpfloat (value, >, 3.1415);
	g_assert_cmpfloat (value, <, 3.1416);

	/* test converting negative to float */
	pf.offset = -4;
	pf.fraction = 0x243b ^ 0xffff;
	ch_packed_float_to_double (&pf, &value);
	g_assert_cmpfloat (value, >, -3.1416);
	g_assert_cmpfloat (value, <, -3.1415);

	/* test converting zero */
	value = 0.0f;
	ch_double_to_packed_float (value, &pf);
	g_assert_cmpint (pf.offset, ==, 0);
	g_assert_cmpint (pf.fraction, ==, 0);
	ch_packed_float_to_double (&pf, &value);
	g_assert_cmpfloat (value, >, -0.001f);
	g_assert_cmpfloat (value, <, +0.001f);

	/* test converting positive */
	value = +1.4f;
	ch_double_to_packed_float (value, &pf);
	g_assert_cmpint (pf.offset, ==, 1);
	g_assert_cmpint (pf.fraction, <, 0x6668);
	g_assert_cmpint (pf.fraction, >, 0x6663);
	ch_packed_float_to_double (&pf, &value);
	g_assert_cmpfloat (value, <, 1.41);
	g_assert_cmpfloat (value, >, 1.39);

	/* test converting negative */
	value = -1.4f;
	ch_double_to_packed_float (value, &pf);
	g_assert_cmpint (pf.offset, ==, -2);
	g_assert_cmpint (pf.fraction, <, (0x6662 ^ 0xffff));
	g_assert_cmpint (pf.fraction, >, (0x6668 ^ 0xffff));
	ch_packed_float_to_double (&pf, &value);
	g_assert_cmpfloat (value, <, -1.39);
	g_assert_cmpfloat (value, >, -1.41);

	/* test converting negative max */
	value = -0x7fff;
	ch_double_to_packed_float (value, &pf);
	g_assert_cmpint (pf.offset, ==, -32767);
	g_assert_cmpint (pf.fraction, ==, 0);
	ch_packed_float_to_double (&pf, &value);
	g_assert_cmpfloat (value, >, -32768.0001);
	g_assert_cmpfloat (value, <, +32767.9999);
}

static void
ch_test_math_add_func (void)
{
	ChPackedFloat pf;
	ChPackedFloat pf_tmp;
	ChPackedFloat pf_result;
	gdouble value = 0.0f;
	guint8 rc;

	/* test addition */
	ch_double_to_packed_float (3.90f, &pf);
	ch_double_to_packed_float (1.40f, &pf_tmp);
	rc = ch_packed_float_add (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, 5.299);
	g_assert_cmpfloat (value, <, 5.310);

	/* test addition with both negative */
	ch_double_to_packed_float (-3.90f, &pf);
	ch_double_to_packed_float (-1.40f, &pf_tmp);
	rc = ch_packed_float_add (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, -5.301);
	g_assert_cmpfloat (value, <, -5.299);

	/* test addition with negative */
	ch_double_to_packed_float (3.20f, &pf);
	ch_double_to_packed_float (-1.50f, &pf_tmp);
	rc = ch_packed_float_add (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, <, 1.701);
	g_assert_cmpfloat (value, >, 1.699);

	/* test addition with negative */
	ch_double_to_packed_float (3.20f, &pf);
	ch_double_to_packed_float (-10.50f, &pf_tmp);
	rc = ch_packed_float_add (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, -7.301);
	g_assert_cmpfloat (value, <, -7.299);

	/* test addition overflow */
	ch_double_to_packed_float (0x7fff, &pf);
	ch_double_to_packed_float (0x7fff, &pf_tmp);
	rc = ch_packed_float_add (&pf, &pf_tmp, &pf_result);
//	g_assert_cmpint (rc, ==, CH_ERROR_OVERFLOW_ADDITION);
}


static void
ch_test_math_multiply_func (void)
{
	ChPackedFloat pf;
	ChPackedFloat pf_tmp;
	ChPackedFloat pf_result;
	gdouble value = 0.0f;
	gdouble value1;
	gdouble value2;
	guint8 rc;

	/* test safe multiplication */
	ch_double_to_packed_float (0.25f, &pf);
	ch_double_to_packed_float (0.50f, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, 0.1249);
	g_assert_cmpfloat (value, <, 0.1251);

	/* test multiplication we have to scale */
	ch_double_to_packed_float (3.90f, &pf);
	ch_double_to_packed_float (1.40f, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, 5.45);
	g_assert_cmpfloat (value, <, 5.47);

	/* test multiplication we have to scale a lot */
	ch_double_to_packed_float (3.90f, &pf);
	ch_double_to_packed_float (200.0f, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, 778.9);
	g_assert_cmpfloat (value, <, 780.1);

	/* test multiplication of negative */
	ch_double_to_packed_float (3.90f, &pf);
	ch_double_to_packed_float (-1.4f, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, <, -5.45);
	g_assert_cmpfloat (value, >, -5.47);

	/* test multiplication of double negative */
	ch_double_to_packed_float (-3.90f, &pf);
	ch_double_to_packed_float (-1.4f, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, 5.45);
	g_assert_cmpfloat (value, <, 5.47);

	/* test multiplication of very different numbers */
	ch_double_to_packed_float (0.072587f, &pf);
	ch_double_to_packed_float (80.0f, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_NONE);
	ch_packed_float_to_double (&pf_result, &value);
	g_assert_cmpfloat (value, >, 5.79);
	g_assert_cmpfloat (value, <, 5.81);

	/* be evil */
	for (value1 = -127; value1 < +127; value1 += 0.5f) {
		for (value2 = -127; value2 < +127; value2 += 0.5f) {
			ch_double_to_packed_float (value1, &pf);
			ch_double_to_packed_float (value2, &pf_tmp);
			rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
			g_assert_cmpint (rc, ==, CH_ERROR_NONE);
			ch_packed_float_to_double (&pf_result, &value);
			g_assert_cmpfloat (value, >, (value1 * value2) - 0.01);
			g_assert_cmpfloat (value, <, (value1 * value2) + 0.01);
		}
	}

	/* test multiplication overflow */
	ch_double_to_packed_float (0x4fff, &pf);
	ch_double_to_packed_float (0x4, &pf_tmp);
	rc = ch_packed_float_multiply (&pf, &pf_tmp, &pf_result);
	g_assert_cmpint (rc, ==, CH_ERROR_OVERFLOW_MULTIPLY);
}

static void
ch_test_state_func (void)
{
	ChClient *client;
	ChColorSelect color_select = 0;
	ChFreqScale multiplier = 0;
	gboolean ret;
	GError *error = NULL;
	guint16 integral_time = 0;
	guint8 leds;

	/* new device */
	client = ch_client_new ();

	/* load the device */
	ret = ch_client_load (client, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify LEDs */
	ret = ch_client_set_leds (client,
				  3,
				  0,
				  0x00,
				  0x00,
				  &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_leds (client,
				  &leds,
				  &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (leds, ==, 3);

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
	GError *error = NULL;
	guint16 major = 0;
	guint16 micro = 0;
	guint16 minor = 0;
	gdouble red = 0;
	gdouble green = 0;
	gdouble blue = 0;
	gdouble post_scale = 0;
	gdouble post_scale_tmp = 0;
	gdouble pre_scale = 0;
	gdouble pre_scale_tmp = 0;
	guint64 serial_number = 0;
	gdouble calibration[9];
	gdouble calibration_tmp[9];
	gchar desc[24];

	/* new device */
	client = ch_client_new ();

	/* load the device */
	ret = ch_client_load (client, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* write eeprom with wrong code */
	ret = ch_client_write_eeprom (client,
				      "hello dave",
				      &error);
	g_assert_error (error, 1, 0);
	g_assert (!ret);
	g_clear_error (&error);

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
	ret = ch_client_get_firmware_ver (client,
					  &major,
					  &minor,
					  &micro,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (major, ==, 1);
	g_assert_cmpint (minor, ==, 0);
	g_assert_cmpint (micro, >, 0);

	/* verify dark offsets */
	ret = ch_client_set_dark_offsets (client,
					  0.12, 0.34, 0.56,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_get_dark_offsets (client,
					  &red,
					  &green,
					  &blue,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (red, ==, 0.12);
	g_assert_cmpint (green, ==, 0.34);
	g_assert_cmpint (blue, ==, 0.56);

	/* verify calibration */
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
					 0,
					 calibration,
					 "test0",
					 &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_set_calibration (client,
					 1,
					 calibration,
					 "test1",
					 &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = ch_client_set_calibration (client,
					 0,
					 calibration,
					 "test0",
					 &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* read back data */
	ret = ch_client_get_calibration (client,
					 0,
					 calibration_tmp,
					 desc,
					 &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (memcmp (calibration_tmp,
			  calibration,
			  sizeof (gfloat) * 9) == 0);
	g_assert_cmpstr (desc, ==, "test0");
	ret = ch_client_get_calibration (client,
					 1,
					 calibration_tmp,
					 desc,
					 &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (memcmp (calibration_tmp,
			  calibration,
			  sizeof (gfloat) * 9) == 0);
	g_assert_cmpstr (desc, ==, "test1");

	/* verify post scale */
	post_scale = 127.8f;
	ret = ch_client_set_post_scale (client,
					post_scale,
					&error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = ch_client_get_post_scale (client,
					&post_scale_tmp,
					&error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpfloat (fabs (post_scale - post_scale_tmp), <, 0.0001);

	/* verify pre scale */
	pre_scale = 1.23f;
	ret = ch_client_set_pre_scale (client,
				       pre_scale,
				       &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = ch_client_get_pre_scale (client,
					&pre_scale_tmp,
					&error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpfloat (fabs (pre_scale - pre_scale_tmp), <, 0.0001);

#if 0
	/* write eeprom */
	ret = ch_client_write_eeprom (client,
				      CH_WRITE_EEPROM_MAGIC,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);
#endif

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
					CH_FREQ_SCALE_100,
					&error);
	g_assert_no_error (error);
	g_assert (ret);

	/* set integral */
	ret = ch_client_set_integral_time (client,
					   0xffff,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* take a reading from the hardware */
	ret = ch_client_take_reading_raw (client,
					  &take_reading,
					  &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (take_reading, >, 0);

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
	g_test_add_func ("/ColorHug/math-convert", ch_test_math_convert_func);
	g_test_add_func ("/ColorHug/math-add", ch_test_math_add_func);
	g_test_add_func ("/ColorHug/math-multiply", ch_test_math_multiply_func);
	g_test_add_func ("/ColorHug/state", ch_test_state_func);
	g_test_add_func ("/ColorHug/eeprom", ch_test_eeprom_func);
	g_test_add_func ("/ColorHug/reading", ch_test_reading_func);
	return g_test_run ();
}

