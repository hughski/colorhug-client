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
#include <math.h>

#include "ch-math.h"
#include "ch-common.h"

/**
 * ch_packed_float_to_double:
 *
 * @pf: A %ChPackedFloat
 * @value: a value in IEEE floating point format
 *
 * Converts a packed float to a double.
 **/
void
ch_packed_float_to_double (const ChPackedFloat *pf, gdouble *value)
{
	g_return_if_fail (value != NULL);
	g_return_if_fail (pf != NULL);
	*value = pf->raw / (gdouble) 0xffff;
}

/**
 * ch_double_to_packed_float:
 *
 * @pf: A %ChPackedFloat
 * @value: a value in IEEE floating point format
 *
 * Converts a double number to a packed float.
 **/
void
ch_double_to_packed_float (gdouble value, ChPackedFloat *pf)
{
	g_return_if_fail (pf != NULL);
	g_return_if_fail (value <= 0x7fff);
	g_return_if_fail (value >= -0x7fff);
	pf->raw = value * (gdouble) 0xffff;
}

/**
 * ch_packed_float_add:
 *
 * @pf1: A %ChPackedFloat
 * @pf1: A %ChPackedFloat
 * @result: A %ChPackedFloat
 *
 * Adds two packed floats together using only integer maths.
 *
 * @return: an error code
 **/
ChFatalError
ch_packed_float_add (const ChPackedFloat *pf1,
		     const ChPackedFloat *pf2,
		     ChPackedFloat *result)
{
	gint32 pf1_tmp;
	gint32 pf2_tmp;

	g_return_val_if_fail (pf1 != NULL, CH_FATAL_ERROR_INVALID_VALUE);
	g_return_val_if_fail (pf2 != NULL, CH_FATAL_ERROR_INVALID_VALUE);
	g_return_val_if_fail (result != NULL, CH_FATAL_ERROR_INVALID_VALUE);

	/* check overflow */
	pf1_tmp = pf1->raw / 0xffff;
	pf2_tmp = pf2->raw / 0xffff;
	if (pf1_tmp + pf2_tmp > 0x7fff)
		return CH_FATAL_ERROR_OVERFLOW_ADDITION;

	/* do the proper result */
	result->raw = pf1->raw + pf2->raw;
	return CH_FATAL_ERROR_NONE;
}

/**
 * ch_packed_float_multiply:
 *
 * @pf1: A %ChPackedFloat
 * @pf1: A %ChPackedFloat
 * @result: A %ChPackedFloat
 *
 * Multiplies two packed floats together using only integer maths.
 *
 * @return: an error code
 **/
ChFatalError
ch_packed_float_multiply (const ChPackedFloat *pf1,
			  const ChPackedFloat *pf2,
			  ChPackedFloat *result)
{
	gint32 mult_result;
	gint32 mult_divisor;
	gint i;

	g_return_val_if_fail (pf1 != NULL, CH_FATAL_ERROR_INVALID_VALUE);
	g_return_val_if_fail (pf2 != NULL, CH_FATAL_ERROR_INVALID_VALUE);
	g_return_val_if_fail (result != NULL, CH_FATAL_ERROR_INVALID_VALUE);

	/* trivial: two numbers < 1.0 can be safely handled
	 * within 32 bits */
	if (pf1->raw < 0x10000 && pf2->raw < 0x10000)
		result->raw = (pf1->raw * pf2->raw) / 0x10000;

	/* find a divisor that can multiply these numbers with the
	 * greatest precision and with the temporary result still
	 * staying within 32 bits */
	for (i = 2; i < 0xff; i *= 2) {

		/* just do the multiplication */
		mult_result = (pf1->raw / i) * (pf2->raw / i);

		/* detect overflow */
		if (ABS((mult_result / pf1->raw) - (pf2->raw / (i * i))) > 1)
			continue;

		/* calculate post-multiply divisor */
		mult_divisor = 0x10000 / (i * i);
		result->raw = mult_result / mult_divisor;
		return CH_FATAL_ERROR_NONE;
	}

	return CH_FATAL_ERROR_OVERFLOW_MULTIPLY;
}
