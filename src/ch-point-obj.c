/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <glib.h>

#include "ch-point-obj.h"

/**
 * ch_point_obj_copy:
 **/
ChPointObj *
ch_point_obj_copy (const ChPointObj *cobj)
{
	ChPointObj *obj;
	obj = g_new0 (ChPointObj, 1);
	obj->x = cobj->x;
	obj->y = cobj->y;
	obj->color = cobj->color;
	return obj;
}

/**
 * ch_point_obj_new:
 **/
ChPointObj *
ch_point_obj_new (void)
{
	ChPointObj *obj;
	obj = g_new0 (ChPointObj, 1);
	obj->x = 0.0f;
	obj->y = 0.0f;
	obj->color = 0x0;
	return obj;
}

/**
 * ch_point_obj_free:
 **/
void
ch_point_obj_free (ChPointObj *obj)
{
	if (obj == NULL)
		return;
	g_free (obj);
}

