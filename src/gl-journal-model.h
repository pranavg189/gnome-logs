/*
 *  GNOME Logs - View and search logs
 *  Copyright (C) 2015  Lars Uebernickel <lars@uebernic.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GL_JOURNAL_MODEL_H
#define GL_JOURNAL_MODEL_H

#include <gio/gio.h>

#define GL_TYPE_QUERY_ITEM gl_query_item_get_type()
G_DECLARE_FINAL_TYPE (GlQueryItem, gl_query_item, GL, QUERY_ITEM, GObject)

typedef struct
{
    /*< private >*/
    GObject parent_instance;
} GlQuery;

typedef struct
{
    /*< private >*/
    GObjectClass parent_class;
} GlQueryClass;

#define GL_TYPE_QUERY (gl_query_get_type())
#define GL_QUERY(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), GL_TYPE_QUERY, GlQuery))

#define GL_TYPE_JOURNAL_MODEL gl_journal_model_get_type()
G_DECLARE_FINAL_TYPE (GlJournalModel, gl_journal_model, GL, JOURNAL_MODEL, GObject)

GlJournalModel *        gl_journal_model_new                            (void);

GType gl_query_result_get_type (void);
GType gl_query_get_type (void);

GlQuery *				gl_query_new 									(void);

void                    gl_journal_model_process_query                  (GlJournalModel *model);
gboolean                gl_journal_model_get_loading                    (GlJournalModel *model);

void                    gl_journal_model_fetch_more_entries             (GlJournalModel *model,
                                                                         gboolean        all);

GArray *                gl_journal_model_get_boot_ids                   (GlJournalModel *model);

gchar *                 gl_journal_model_get_current_boot_time          (GlJournalModel *model,
                                                                         const gchar *boot_match);
void					gl_journal_model_search_init 				    (GlJournalModel *model,
                                  										 gchar *search_string,
                                  										 gboolean parameters[]);

void gl_query_add_match (GlQuery *query,gchar *field_name, gchar *field_value, gchar *search_text, gboolean search_type);

GlQuery * 				gl_journal_model_get_query						(GlJournalModel *model);

void					gl_journal_model_set_query						(GlJournalModel *model, GlQuery *query);

#endif
