/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-

   nautilus-view-factory.c: register and create NautilusViews
 
   Copyright (C) 2004 Red Hat Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
  
   Author: Alexander Larsson <alexl@redhat.com>
*/

#include "nautilus-view-factory.h"

static GList *registered_views;

void
nautilus_view_factory_register (NautilusViewInfo *view_info)
{
	g_return_if_fail (view_info != NULL);
	g_return_if_fail (view_info->id != NULL);
	g_return_if_fail (nautilus_view_factory_lookup (view_info->id) == NULL);
	
	registered_views = g_list_append (registered_views, view_info);
}

const NautilusViewInfo *
nautilus_view_factory_lookup (const char *id)
{
	GList *l;
	NautilusViewInfo *view_info;

	g_return_val_if_fail (id != NULL, NULL);

	
	for (l = registered_views; l != NULL; l = l->next) {
		view_info = l->data;
		
		if (strcmp (view_info->id, id) == 0) {
			return view_info;
		}
	}
	return NULL;
}

NautilusView *
nautilus_view_factory_create (const char *id,
			      NautilusWindowInfo *window)
{
	const NautilusViewInfo *view_info;

	view_info = nautilus_view_factory_lookup (id);
	if (view_info == NULL) {
		return NULL;
	}

	return view_info->create (window);
}

gboolean
nautilus_view_factory_view_supports_uri (const char *id,
					 const char *uri,
					 GnomeVFSFileType file_type,
					 const char *mime_type)
{
	const NautilusViewInfo *view_info;

	view_info = nautilus_view_factory_lookup (id);
	if (view_info == NULL) {
		return FALSE;
	}

	return view_info->supports_uri (uri, file_type, mime_type);
	
}

GList *
nautilus_view_factory_get_views_for_uri (const char *uri,
					 GnomeVFSFileType file_type,
					 const char *mime_type)
{
	GList *l, *res;
	const NautilusViewInfo *view_info;

	res = NULL;
	
	for (l = registered_views; l != NULL; l = l->next) {
		view_info = l->data;

		if (view_info->supports_uri (uri, file_type, mime_type)) {
			res = g_list_prepend (res, g_strdup (view_info->id));
		}
	}
	
	return g_list_reverse (res);
}

