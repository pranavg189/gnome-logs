/*
 *  GNOME Logs - View and search logs
 *  Copyright (C) 2013  Red Hat, Inc.
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

#ifndef GL_UTIL_H_
#define GL_UTIL_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * GlUtilClockFormat:
 * @GL_UTIL_CLOCK_FORMAT_24HR: 24-hour clock format
 * @GL_UTIL_CLOCK_FORMAT_12HR: 12-hour clock format
 *
 * Keep in sync with the enum in the org.gnome.desktop.interface schema. */
typedef enum
{
    GL_UTIL_CLOCK_FORMAT_24HR,
    GL_UTIL_CLOCK_FORMAT_12HR
} GlUtilClockFormat;

void gl_util_on_css_provider_parsing_error (GtkCssProvider *provider,
                                            GtkCssSection *section,
                                            GError *error,
                                            G_GNUC_UNUSED gpointer user_data);
gchar * gl_util_timestamp_to_display (guint64 microsecs,
                                      GlUtilClockFormat format);

G_END_DECLS

#endif /* GL_UTIL_H_ */
