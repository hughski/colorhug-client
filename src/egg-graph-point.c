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

#include "egg-graph-point.h"

EggGraphPoint *
egg_graph_point_copy (const EggGraphPoint *cobj)
{
	EggGraphPoint *obj;
	obj = g_new0 (EggGraphPoint, 1);
	obj->x = cobj->x;
	obj->y = cobj->y;
	obj->color = cobj->color;
	return obj;
}

EggGraphPoint *
egg_graph_point_new (void)
{
	EggGraphPoint *obj;
	obj = g_new0 (EggGraphPoint, 1);
	obj->x = 0.0f;
	obj->y = 0.0f;
	obj->color = 0x0;
	return obj;
}

void
egg_graph_point_free (EggGraphPoint *obj)
{
	if (obj == NULL)
		return;
	g_free (obj);
}

