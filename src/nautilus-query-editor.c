/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Copyright (C) 2005 Red Hat, Inc.
 *
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; see the file COPYING.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Alexander Larsson <alexl@redhat.com>
 *
 */

#include <config.h>
#include "nautilus-query-editor.h"

#include <string.h>
#include <libnautilus-private/nautilus-marshal.h>
#include <glib/gi18n.h>
#include <eel/eel-gtk-macros.h>
#include <eel/eel-glib-extensions.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkbindings.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkcombobox.h>
#include "gtk/gtkliststore.h"
#include <gtk/gtkfilechooserbutton.h>
#include "gtk/gtkcelllayout.h"
#include "gtk/gtkcellrenderertext.h"
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>

typedef enum {
	NAUTILUS_QUERY_EDITOR_ROW_LOCATION,
	NAUTILUS_QUERY_EDITOR_ROW_TYPE,
	
	NAUTILUS_QUERY_EDITOR_ROW_LAST
} NautilusQueryEditorRowType;

typedef struct {
	NautilusQueryEditorRowType type;
	NautilusQueryEditor *editor;
	GtkWidget *hbox;
	GtkWidget *combo;

	GtkWidget *type_widget;
	
	void *data;
} NautilusQueryEditorRow;


typedef struct {
	const char *name;
	GtkWidget * (*create_widgets)      (NautilusQueryEditorRow *row);
	void        (*add_to_query)        (NautilusQueryEditorRow *row,
					    NautilusQuery          *query);
	void        (*free_data)           (NautilusQueryEditorRow *row);
	void        (*add_rows_from_query) (NautilusQueryEditor *editor,
					    NautilusQuery *query);
} NautilusQueryEditorRowOps;

struct NautilusQueryEditorDetails {
	gboolean is_indexed;
	GtkWidget *entry;
	gboolean change_frozen;
	guint typing_timeout_id;
	gboolean is_visible;
	GtkWidget *invisible_vbox;
	GtkWidget *visible_vbox;

	GList *rows;
	
	NautilusSearchBar *bar;
};

enum {
	CHANGED,
	CANCEL,
	LAST_SIGNAL
}; 

static guint signals[LAST_SIGNAL];

static void  nautilus_query_editor_class_init       (NautilusQueryEditorClass *class);
static void  nautilus_query_editor_init             (NautilusQueryEditor      *editor);

static void entry_activate_cb (GtkWidget *entry, NautilusQueryEditor *editor);
static void entry_changed_cb  (GtkWidget *entry, NautilusQueryEditor *editor);
static void nautilus_query_editor_changed_force (NautilusQueryEditor *editor,
						 gboolean             force);
static void nautilus_query_editor_changed (NautilusQueryEditor *editor);
static NautilusQueryEditorRow * nautilus_query_editor_add_row (NautilusQueryEditor *editor,
							       NautilusQueryEditorRowType type);

static GtkWidget *location_row_create_widgets  (NautilusQueryEditorRow *row);
static void       location_row_add_to_query    (NautilusQueryEditorRow *row,
					        NautilusQuery          *query);
static void       location_row_free_data       (NautilusQueryEditorRow *row);
static void       location_add_rows_from_query (NautilusQueryEditor    *editor,
					        NautilusQuery          *query);
static GtkWidget *type_row_create_widgets      (NautilusQueryEditorRow *row);
static void       type_row_add_to_query        (NautilusQueryEditorRow *row,
					        NautilusQuery          *query);
static void       type_row_free_data           (NautilusQueryEditorRow *row);
static void       type_add_rows_from_query     (NautilusQueryEditor    *editor,
					        NautilusQuery          *query);



static NautilusQueryEditorRowOps row_type[] = {
	{ N_("Location"),
	  location_row_create_widgets,
	  location_row_add_to_query,
	  location_row_free_data,
	  location_add_rows_from_query
	},
	{ N_("File Type"),
	  type_row_create_widgets,
	  type_row_add_to_query,
	  type_row_free_data,
	  type_add_rows_from_query
	},
};

EEL_CLASS_BOILERPLATE (NautilusQueryEditor,
		       nautilus_query_editor,
		       GTK_TYPE_VBOX)

static void
nautilus_query_editor_finalize (GObject *object)
{
	NautilusQueryEditor *editor;

	editor = NAUTILUS_QUERY_EDITOR (object);

	g_free (editor->details);

	EEL_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
nautilus_query_editor_dispose (GObject *object)
{
	NautilusQueryEditor *editor;

	editor = NAUTILUS_QUERY_EDITOR (object);
	

	if (editor->details->bar != NULL) {
		g_signal_handlers_disconnect_by_func (editor->details->entry,
						      entry_activate_cb,
						      editor);
		g_signal_handlers_disconnect_by_func (editor->details->entry,
						      entry_changed_cb,
						      editor);
		
		nautilus_search_bar_return_entry (editor->details->bar);
		eel_remove_weak_pointer (&editor->details->bar);
	}
	
	EEL_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
nautilus_query_editor_class_init (NautilusQueryEditorClass *class)
{
	GObjectClass *gobject_class;
	GtkBindingSet *binding_set;

	gobject_class = G_OBJECT_CLASS (class);
	gobject_class->finalize = nautilus_query_editor_finalize;
        gobject_class->dispose = nautilus_query_editor_dispose;

	signals[CHANGED] =
		g_signal_new ("changed",
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NautilusQueryEditorClass, changed),
		              NULL, NULL,
		              nautilus_marshal_VOID__OBJECT_BOOLEAN,
		              G_TYPE_NONE, 2, NAUTILUS_TYPE_QUERY, G_TYPE_BOOLEAN);

	signals[CANCEL] =
		g_signal_new ("cancel",
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_LAST | GTK_RUN_ACTION,
		              G_STRUCT_OFFSET (NautilusQueryEditorClass, cancel),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	binding_set = gtk_binding_set_by_class (class);
	gtk_binding_entry_add_signal (binding_set, GDK_Escape, 0, "cancel", 0);
}

static void
entry_activate_cb (GtkWidget *entry, NautilusQueryEditor *editor)
{
	if (editor->details->typing_timeout_id) {
		g_source_remove (editor->details->typing_timeout_id);
		editor->details->typing_timeout_id = 0;
	}

	nautilus_query_editor_changed_force (editor, TRUE);
}

static gboolean
typing_timeout_cb (gpointer user_data)
{
	NautilusQueryEditor *editor;

	editor = NAUTILUS_QUERY_EDITOR (user_data);

	nautilus_query_editor_changed (editor);

	editor->details->typing_timeout_id = 0;

	return FALSE;
}

#define TYPING_TIMEOUT 750

static void
entry_changed_cb (GtkWidget *entry, NautilusQueryEditor *editor)
{
	if (editor->details->change_frozen) {
		return;
	}

	if (editor->details->typing_timeout_id) {
		g_source_remove (editor->details->typing_timeout_id);
	}

	editor->details->typing_timeout_id =
		g_timeout_add (TYPING_TIMEOUT,
			       typing_timeout_cb,
			       editor);
}

static void
edit_clicked (GtkButton *button, NautilusQueryEditor *editor)
{
	nautilus_query_editor_set_visible (editor, TRUE);
}

/* Location */

static GtkWidget *
location_row_create_widgets (NautilusQueryEditorRow *row)
{
	GtkWidget *chooser;
	
	chooser = gtk_file_chooser_button_new (_("Select folder search in"),
					       GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (chooser), TRUE);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (chooser),
					     g_get_home_dir ());
	gtk_widget_show (chooser);

	g_signal_connect_swapped (chooser, "current-folder-changed",
				  G_CALLBACK (nautilus_query_editor_changed),
				  row->editor);
		
	gtk_box_pack_start (GTK_BOX (row->hbox), chooser, FALSE, FALSE, 0);
	
	return chooser;
}

static void
location_row_add_to_query (NautilusQueryEditorRow *row,
			   NautilusQuery          *query)
{
	char *folder, *uri;
	
	folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (row->type_widget));
	uri = gnome_vfs_get_uri_from_local_path (folder);
	g_free (folder);

	nautilus_query_set_location (query, uri);
	g_free (uri);
}

static void
location_row_free_data (NautilusQueryEditorRow *row)
{
}

static void
location_add_rows_from_query (NautilusQueryEditor    *editor,
			      NautilusQuery          *query)
{
	NautilusQueryEditorRow *row;
	char *uri, *folder;
	
	uri = nautilus_query_get_location (query);

	if (uri == NULL) {
		return;
	}
	folder = gnome_vfs_get_local_path_from_uri (uri);
	g_free (uri);
	if (folder == NULL) {
		return;
	}
	
	row = nautilus_query_editor_add_row (editor,
					     NAUTILUS_QUERY_EDITOR_ROW_LOCATION);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (row->type_widget),
					     folder);
	
	g_free (folder);
}


/* Type */

static gboolean
type_separator_func (GtkTreeModel      *model,
		     GtkTreeIter       *iter,
		     gpointer           data)
{
	char *text;
	gboolean res;
	
	gtk_tree_model_get (model, iter, 0, &text, -1);

	res = strcmp (text, "---") == 0;
	
	g_free (text);
	return res;
}

struct {
	char *name;
	char *mimetypes[20];
} mime_type_groups[] = {
	{ N_("Documents"),
	  { "application/rtf",
	    "application/msword",
	    "application/vnd.sun.xml.writer",
	    "application/vnd.sun.xml.writer.global",
	    "application/vnd.sun.xml.writer.template",
	    "application/vnd.oasis.opendocument.text",
	    "application/vnd.oasis.opendocument.text-template",
	    "application/x-abiword",
	    "application/x-applix-word",
	    "application/x-mswrite",
	    "application/docbook+xml",
	    "application/x-kword",
	    "application/x-kword-crypt",
	    "application/x-lyx",
	    NULL
	  }
	},
	{ N_("Music"),
	  { "application/ogg",
	    "audio/ac3",
	    "audio/basic",
	    "audio/midi",
	    "audio/x-flac",
	    "audio/mp4",
	    "audio/mpeg",
	    "audio/x-mpeg",
	    "audio/x-ms-asx",
	    "audio/x-pn-realaudio",
	    NULL
	  }
	},
	{ N_("Video"),
	  { "video/mp4",
	    "video/3gpp",
	    "video/mpeg",
	    "video/quicktime",
	    "video/vivo",
	    "video/x-avi",
	    "video/x-mng",
	    "video/x-ms-asf",
	    "video/x-ms-wmv",
	    "video/x-msvideo",
	    "video/x-nsv",
	    "video/x-real-video",
	    NULL
	  }
	},
	{ N_("Picture"),
	  { "application/vnd.oasis.opendocument.image",
	    "application/x-krita",
	    "image/bmp",
	    "image/cgm",
	    "image/gif",
	    "image/jpeg",
	    "image/jpeg2000",
	    "image/png",
	    "image/svg+xml",
	    "image/tiff",
	    "image/x-compressed-xcf",
	    "image/x-pcx",
	    "image/x-photo-cd",
	    "image/x-psd",
	    "image/x-tga",
	    "image/x-xcf",
	    NULL
	  }
	},
	{ N_("Illustration"),
	  { "application/illustrator",
	    "application/vnd.corel-draw",
	    "application/vnd.stardivision.draw",
	    "application/vnd.oasis.opendocument.graphics",
	    "application/x-dia-diagram",
	    "application/x-karbon",
	    "application/x-killustrator",
	    "application/x-kivio",
	    "application/x-kontour",
	    "application/x-wpg",
	    NULL
	  }
	},
	{ N_("Spreadsheet"),
	  { "application/vnd.lotus-1-2-3",
	    "application/vnd.ms-excel",
	    "application/vnd.stardivision.calc",
	    "application/vnd.sun.xml.calc",
	    "application/vnd.oasis.opendocument.spreadsheet",
	    "application/x-applix-spreadsheet",
	    "application/x-gnumeric",
	    "application/x-kspread",
	    "application/x-kspread-crypt",
	    "application/x-quattropro",
	    "application/x-sc",
	    "application/x-siag",
	    NULL
	  }
	},
	{ N_("Presentation"),
	  { "application/vnd.ms-powerpoint",
	    "application/vnd.sun.xml.impress",
	    "application/vnd.oasis.opendocument.presentation",
	    "application/x-magicpoint",
	    "application/x-kpresenter",
	    NULL
	  }
	},
	{ N_("Pdf / Postscript"),
	  { "application/pdf",
	    "application/postscript",
	    "application/x-dvi",
	    "image/x-eps",
	    NULL
	  }
	},
	{ N_("Text File"),
	  { "text/plain",
	    NULL
	  }
	}
};

static void
type_combo_changed (GtkComboBox *combo_box, NautilusQueryEditorRow *row)
{
	GtkTreeIter iter;
	gboolean other;
	GtkTreeModel *model;

	if (!gtk_combo_box_get_active_iter  (GTK_COMBO_BOX (row->type_widget),
					     &iter)) {
		return;
	}

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (row->type_widget));
	gtk_tree_model_get (model, &iter, 3, &other, -1);

	if (other) {
		/* TODO: Ask for other mimetype and add it to list + select it */
		/* But can't read list of mimetypes atm */
	}
	
	nautilus_query_editor_changed (row->editor);
}

static void
type_add_custom_type (NautilusQueryEditorRow *row,
		      const char *mime_type,
		      const char *description,
		      GtkTreeIter *iter)
{
	GtkTreeModel *model;
	GtkListStore *store;
	
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (row->type_widget));
	store = GTK_LIST_STORE (model);

	gtk_list_store_append (store, iter);
	gtk_list_store_set (store, iter,
			    0, description,
			    2, mime_type,
			    -1);
}


static GtkWidget *
type_row_create_widgets (NautilusQueryEditorRow *row)
{
	GtkWidget *combo;
	GtkCellRenderer *cell;
	GtkListStore *store;
	GtkTreeIter iter;
	int i;

	store = gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_BOOLEAN);
	combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
	g_object_unref (store);
	
	cell = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cell, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), cell,
					"text", 0,
					NULL);
	gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (combo),
					      type_separator_func,
					      NULL, NULL);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, _("Any"), -1);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "---",  -1);

	for (i = 0; i < G_N_ELEMENTS (mime_type_groups); i++) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    0, gettext (mime_type_groups[i].name),
				    1, mime_type_groups[i].mimetypes,
				    -1);
	}

#if 0 /* Disable this for now, as there is no way to read list of mimetypes */
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "---",  -1);
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, _("Other Type..."), 3, TRUE, -1);
#endif

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	
	g_signal_connect (combo, "changed",
			  G_CALLBACK (type_combo_changed),
			  row);

	gtk_widget_show (combo);
	
	gtk_box_pack_start (GTK_BOX (row->hbox), combo, FALSE, FALSE, 0);
	
	return combo;
}

static void
type_row_add_to_query (NautilusQueryEditorRow *row,
		       NautilusQuery          *query)
{
	GtkTreeIter iter;
	char **mimetypes;
	char *mimetype;
	GtkTreeModel *model;

	if (!gtk_combo_box_get_active_iter  (GTK_COMBO_BOX (row->type_widget),
					     &iter)) {
		return;
	}

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (row->type_widget));
	gtk_tree_model_get (model, &iter, 1, &mimetypes, 2, &mimetype, -1);

	if (mimetypes != NULL) {
		while (*mimetypes != NULL) {
			nautilus_query_add_mime_type (query, *mimetypes);
			mimetypes++;
		}
	}
	if (mimetype) {
		nautilus_query_add_mime_type (query, mimetype);
		g_free (mimetype);
	}
}

static void
type_row_free_data (NautilusQueryEditorRow *row)
{
}

static gboolean
all_group_types_in_list (char **group_types, GList *mime_types)
{
	GList *l;
	char **group_type;
	char *mime_type;
	gboolean found;

	group_type = group_types;
	while (*group_type != NULL) {
		found = FALSE;

		for (l = mime_types; l != NULL; l = l->next) {
			mime_type = l->data;

			if (strcmp (mime_type, *group_type) == 0) {
				found = TRUE;
				break;
			}
		}
		
		if (!found) {
			return FALSE;
		}
		group_type++;
	}
	return TRUE;
}

static GList *
remove_group_types_from_list (char **group_types, GList *mime_types)
{
	GList *l, *next;
	char **group_type;
	char *mime_type;
	gboolean found;

	group_type = group_types;
	while (*group_type != NULL) {
		found = FALSE;

		for (l = mime_types; l != NULL; l = next) {
			mime_type = l->data;
			next = l->next;

			if (strcmp (mime_type, *group_type) == 0) {
				mime_types = g_list_remove_link (mime_types, l);
				g_free (mime_type);
				break;
			}
		}
		
		group_type++;
	}
	return mime_types;
}


static void
type_add_rows_from_query (NautilusQueryEditor    *editor,
			  NautilusQuery          *query)
{
	GList *mime_types;
	char *mime_type;
	const char *desc;
	NautilusQueryEditorRow *row;
	GtkTreeIter iter;
	int i;
	GtkTreeModel *model;
	GList *l;

	mime_types = nautilus_query_get_mime_types (query);

	if (mime_types == NULL) {
		return;
	}
	
	for (i = 0; i < G_N_ELEMENTS (mime_type_groups); i++) {
		if (all_group_types_in_list (mime_type_groups[i].mimetypes,
					     mime_types)) {
			mime_types = remove_group_types_from_list (mime_type_groups[i].mimetypes,
								   mime_types);

			row = nautilus_query_editor_add_row (editor,
							     NAUTILUS_QUERY_EDITOR_ROW_TYPE);

			model = gtk_combo_box_get_model (GTK_COMBO_BOX (row->type_widget));

			gtk_tree_model_iter_nth_child (model, &iter, NULL, i + 2);
			gtk_combo_box_set_active_iter  (GTK_COMBO_BOX (row->type_widget),
							&iter);
		}
	}

	for (l = mime_types; l != NULL; l = l->next) {
		mime_type = l->data;

		desc = gnome_vfs_mime_get_value (mime_type, "description");
		if (desc == NULL) {
			desc = mime_type;
		}

		row = nautilus_query_editor_add_row (editor,
						     NAUTILUS_QUERY_EDITOR_ROW_TYPE);
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (row->type_widget));
		
		type_add_custom_type (row, mime_type, desc, &iter);
		gtk_combo_box_set_active_iter  (GTK_COMBO_BOX (row->type_widget),
						&iter);
	}
	
	eel_g_list_free_deep (mime_types);

}

/* End of row types */

static NautilusQueryEditorRowType
get_next_free_type (NautilusQueryEditor *editor)
{
	NautilusQueryEditorRow *row;
	NautilusQueryEditorRowType type;
	gboolean found;
	GList *l;

	
	for (type = 0; type < NAUTILUS_QUERY_EDITOR_ROW_LAST; type++) {
		found = FALSE;
		for (l = editor->details->rows; l != NULL; l = l->next) {
			row = l->data;
			if (row->type == type) {
				found = TRUE;
				break;
			}
		}
		if (!found) {
			return type;
		}
	}
	return NAUTILUS_QUERY_EDITOR_ROW_TYPE;
}

static void
remove_row_cb (GtkButton *clicked_button, NautilusQueryEditorRow *row)
{
	NautilusQueryEditor *editor;

	editor = row->editor;
	gtk_container_remove (GTK_CONTAINER (editor->details->visible_vbox),
			      row->hbox);
	
	editor->details->rows = g_list_remove (editor->details->rows, row);

	row_type[row->type].free_data (row);
	g_free (row);

	nautilus_query_editor_changed (editor);
}

static void
create_type_widgets (NautilusQueryEditorRow *row)
{
	row->type_widget = row_type[row->type].create_widgets (row);
}

static void
row_type_combo_changed_cb (GtkComboBox *combo_box, NautilusQueryEditorRow *row)
{
	NautilusQueryEditorRowType type;

	type = gtk_combo_box_get_active (combo_box);

	if (type == row->type) {
		return;
	}

	if (row->type_widget != NULL) {
		gtk_widget_destroy (row->type_widget);
		row->type_widget = NULL;
	}

	row_type[row->type].free_data (row);
	row->data = NULL;

	row->type = type;
	
	create_type_widgets (row);

	nautilus_query_editor_changed (row->editor);
}

static NautilusQueryEditorRow *
nautilus_query_editor_add_row (NautilusQueryEditor *editor,
			       NautilusQueryEditorRowType type)
{
	GtkWidget *hbox, *button, *image, *combo;
	NautilusQueryEditorRow *row;
	int i;

	row = g_new0 (NautilusQueryEditorRow, 1);
	row->editor = editor;
	row->type = type;
	
	hbox = gtk_hbox_new (FALSE, 6);
	row->hbox = hbox;
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (editor->details->visible_vbox), hbox, FALSE, FALSE, 0);

	combo = gtk_combo_box_new_text ();
	row->combo = combo;
	for (i = 0; i < NAUTILUS_QUERY_EDITOR_ROW_LAST; i++) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (combo), gettext (row_type[i].name));
	}
	gtk_widget_show (combo);
	gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), row->type);

	editor->details->rows = g_list_append (editor->details->rows, row);

	g_signal_connect (combo, "changed",
			  G_CALLBACK (row_type_combo_changed_cb), row);
	
	create_type_widgets (row);
	
	button = gtk_button_new ();
	image = gtk_image_new_from_stock (GTK_STOCK_REMOVE,
					  GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add (GTK_CONTAINER (button), image);
	gtk_widget_show (image);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_show (button);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (remove_row_cb), row);

	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);

	return row;
}

static void
go_search_cb (GtkButton *clicked_button, NautilusQueryEditor *editor)
{
	nautilus_query_editor_changed_force (editor, TRUE);
}

static void
add_new_row_cb (GtkButton *clicked_button, NautilusQueryEditor *editor)
{
	nautilus_query_editor_add_row (editor, get_next_free_type (editor));
	nautilus_query_editor_changed (editor);
}

static void
nautilus_query_editor_init (NautilusQueryEditor *editor)
{
	GtkWidget *hbox, *label, *button;

	editor->details = g_new0 (NautilusQueryEditorDetails, 1);
	editor->details->is_visible = TRUE;

	editor->details->invisible_vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (editor), editor->details->invisible_vbox,
			    FALSE, FALSE, 0);
	editor->details->visible_vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (editor), editor->details->visible_vbox,
			    FALSE, FALSE, 0);
	/* Only show visible vbox */
	gtk_widget_show (editor->details->visible_vbox);

	/* Create invisible part: */
	hbox = gtk_hbox_new (FALSE, 6);
	gtk_box_pack_start (GTK_BOX (editor->details->invisible_vbox),
			    hbox, FALSE, FALSE, 0);
	gtk_widget_show (hbox);
	
	label = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (label), _("<b>Search Folder</b>"));
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	gtk_widget_show (label);
	
	button = gtk_button_new_with_label (_("Edit"));
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	gtk_widget_show (button);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (edit_clicked), editor);
}

void
nautilus_query_editor_set_default_query (NautilusQueryEditor *editor)
{
	if (!editor->details->is_indexed) {
		nautilus_query_editor_add_row (editor, NAUTILUS_QUERY_EDITOR_ROW_LOCATION);
		nautilus_query_editor_changed (editor);
	}
}

static void
finish_first_line (NautilusQueryEditor *editor, GtkWidget *hbox, gboolean use_go)
{
	GtkWidget *button, *image;

	button = gtk_button_new ();
	image = gtk_image_new_from_stock (GTK_STOCK_ADD,
					  GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add (GTK_CONTAINER (button), image);
	gtk_widget_show (image);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_show (button);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (add_new_row_cb), editor);
	
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);

	if (!editor->details->is_indexed) {
		if (use_go) {
			button = gtk_button_new_with_label (_("Go"));
		} else {
			button = gtk_button_new_with_label (_("Reload"));
		}
		gtk_widget_show (button);
		
		g_signal_connect (button, "clicked",
				  G_CALLBACK (go_search_cb), editor);
		
		gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	}
}

static void
setup_internal_entry (NautilusQueryEditor *editor)
{
	GtkWidget *hbox, *label;
	
	/* Create visible part: */
	hbox = gtk_hbox_new (FALSE, 6);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (editor->details->visible_vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new ("");
	gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>_Search for:</b>"));
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	editor->details->entry = gtk_entry_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), editor->details->entry);
	gtk_box_pack_start (GTK_BOX (hbox), editor->details->entry, TRUE, TRUE, 0);

	g_signal_connect (editor->details->entry, "activate",
			  G_CALLBACK (entry_activate_cb), editor);
	g_signal_connect (editor->details->entry, "changed",
			  G_CALLBACK (entry_changed_cb), editor);
	gtk_widget_show (editor->details->entry);

	finish_first_line (editor, hbox, TRUE);
}

static void
setup_external_entry (NautilusQueryEditor *editor, GtkWidget *entry)
{
	GtkWidget *hbox, *label;
	
	/* Create visible part: */
	hbox = gtk_hbox_new (FALSE, 6);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (editor->details->visible_vbox), hbox, FALSE, FALSE, 0);

	label = gtk_label_new (_("Search results"));
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	
	editor->details->entry = entry;
	g_signal_connect (editor->details->entry, "activate",
			  G_CALLBACK (entry_activate_cb), editor);
	g_signal_connect (editor->details->entry, "changed",
			  G_CALLBACK (entry_changed_cb), editor);

	finish_first_line (editor, hbox, FALSE);

}

void
nautilus_query_editor_set_visible (NautilusQueryEditor *editor,
				   gboolean visible)
{
	editor->details->is_visible = visible;
	if (visible) {
		gtk_widget_show (editor->details->visible_vbox);
		gtk_widget_hide (editor->details->invisible_vbox);
	} else {
		gtk_widget_hide (editor->details->visible_vbox);
		gtk_widget_show (editor->details->invisible_vbox);
	}
}

static gboolean
query_is_valid (NautilusQueryEditor *editor)
{
	const char *text;

	text = gtk_entry_get_text (GTK_ENTRY (editor->details->entry));

	return text != NULL && text[0] != '\0';
}

static void
nautilus_query_editor_changed_force (NautilusQueryEditor *editor, gboolean force_reload)
{
	NautilusQuery *query;

	if (editor->details->change_frozen) {
		return;
	}
	
	if (query_is_valid (editor)) {
		query = nautilus_query_editor_get_query (editor);
		g_signal_emit (editor, signals[CHANGED], 0,
			       query, editor->details->is_indexed || force_reload);
		g_object_unref (query);
	}
}

static void
nautilus_query_editor_changed (NautilusQueryEditor *editor)
{
	nautilus_query_editor_changed_force (editor, FALSE);
}

void
nautilus_query_editor_grab_focus (NautilusQueryEditor *editor)
{
	gtk_widget_grab_focus (editor->details->entry);
}

NautilusQuery *
nautilus_query_editor_get_query (NautilusQueryEditor *editor)
{
	const char *query_text;
	NautilusQuery *query;
	GList *l;
	NautilusQueryEditorRow *row;

	query_text = gtk_entry_get_text (GTK_ENTRY (editor->details->entry));

	/* Empty string is a NULL query */
	if (query_text && query_text[0] == '\0') {
		return NULL;
	}
	
	query = nautilus_query_new ();
	nautilus_query_set_text (query, query_text);

	for (l = editor->details->rows; l != NULL; l = l->next) {
		row = l->data;
		
		row_type[row->type].add_to_query (row, query);
	}
	
	return query;
}

void
nautilus_query_editor_clear_query (NautilusQueryEditor *editor)
{
	editor->details->change_frozen = TRUE;
	gtk_entry_set_text (GTK_ENTRY (editor->details->entry), "");
	editor->details->change_frozen = FALSE;
}

GtkWidget *
nautilus_query_editor_new (gboolean start_hidden, gboolean is_indexed)
{
	GtkWidget *editor;

	editor = g_object_new (NAUTILUS_TYPE_QUERY_EDITOR, NULL);

	NAUTILUS_QUERY_EDITOR (editor)->details->is_indexed = is_indexed;

	nautilus_query_editor_set_visible (NAUTILUS_QUERY_EDITOR (editor),
					   !start_hidden);
	
	setup_internal_entry (NAUTILUS_QUERY_EDITOR (editor));
		
	return editor;
}

GtkWidget*
nautilus_query_editor_new_with_bar (gboolean start_hidden,
				    gboolean is_indexed,
				    NautilusSearchBar *bar)
{
	GtkWidget *entry;
	NautilusQueryEditor *editor;

	editor = NAUTILUS_QUERY_EDITOR (g_object_new (NAUTILUS_TYPE_QUERY_EDITOR, NULL));
	editor->details->is_indexed = is_indexed;

	nautilus_query_editor_set_visible (editor, !start_hidden);

	editor->details->bar = bar;
	eel_add_weak_pointer (&editor->details->bar);
	
	entry = nautilus_search_bar_borrow_entry (bar);
	setup_external_entry (editor, entry);
	
	return GTK_WIDGET (editor);
}

void
nautilus_query_editor_set_query (NautilusQueryEditor *editor, NautilusQuery *query)
{
	NautilusQueryEditorRowType type;
	const char *text;

	if (!query) {
		nautilus_query_editor_clear_query (editor);
		return;
	}

	text = nautilus_query_get_text (query);
	if (!text) {
		text = "";
	}

	editor->details->change_frozen = TRUE;
	gtk_entry_set_text (GTK_ENTRY (editor->details->entry), text);

	for (type = 0; type < NAUTILUS_QUERY_EDITOR_ROW_LAST; type++) {
		row_type[type].add_rows_from_query (editor, query);
	}
	
	editor->details->change_frozen = FALSE;
}