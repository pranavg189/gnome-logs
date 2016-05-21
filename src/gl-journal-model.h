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

typedef enum
{
    SEARCH_TYPE_EXACT,
    SEARCH_TYPE_SUBSTRING
} GlQuerySearchType;

/* Details of match fields */
typedef struct GlQueryItem
{
    gchar *field_name;
    gchar *field_value;
    GlQuerySearchType search_type;
    gboolean is_case_sensitive;
    gboolean bool_operator_used;
} GlQueryItem;

/* Resultant Query passed to Journal model from eventviewlist */
typedef struct GlQuery
{
    GPtrArray *queryitems;   /* array of GlQuery Objects passed through eventviewlist */
} GlQuery;

#define GL_TYPE_JOURNAL_MODEL gl_journal_model_get_type()
G_DECLARE_FINAL_TYPE (GlJournalModel, gl_journal_model, GL, JOURNAL_MODEL, GObject)

GlJournalModel *        gl_journal_model_new                            (void);

void                    gl_journal_model_set_query                  (GlJournalModel *model, GlQuery *query);
gboolean                gl_journal_model_get_loading                    (GlJournalModel *model);

void                    gl_journal_model_fetch_more_entries             (GlJournalModel *model,
                                                                         gboolean        all);

GArray *                gl_journal_model_get_boot_ids                   (GlJournalModel *model);

gchar *                 gl_journal_model_get_current_boot_time          (GlJournalModel *model,
                                                                         const gchar *boot_match);
void					gl_journal_model_search_init 				    (GlJournalModel *model,
                                  										 gchar *search_string,
                                  										 gboolean parameters[]);

void gl_query_add_match (GlQuery *query,gchar *field_name, gchar *field_value, GlQuerySearchType search_type, gboolean case_sensitive);

GlQuery * 				gl_journal_model_get_query						(GlJournalModel *model);

#endif
