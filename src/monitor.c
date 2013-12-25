/* whoopsie
 * 
 * Copyright © 2011-2012 Canonical Ltd.
 * Author: Evan Dandrea <evan.dandrea@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE
#define _GNU_SOURCE

#include <glib.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "monitor.h"
#include "utils.h"

void
create_file (const char* upload, FileCreationCallback callback)
{
    char* upload_file = NULL;
    char* crash_file = NULL;

    crash_file = change_file_extension (upload, ".crash");
    if (!crash_file) {
        g_print ("Unable to parse the upload file path.\n");
        return;
    }

    if (already_handled_report (crash_file)) {
        /* Ensure that if we ended up here because of an error, that we do not
         * attempt to process this report again. */
        if (!mark_handled (crash_file))
            g_print ("Unable to mark report as seen (%s).\n", crash_file);

    } else if (callback (crash_file)) {
        /* We successfully uploaded the report */
        if (!mark_handled (crash_file))
            g_print ("Unable to mark report as seen (%s).\n", crash_file);
    }

    free (crash_file);
    free (upload_file);
}

void
changed_event (GFileMonitor* monitor, GFile *file, GFile *other_file,
               GFileMonitorEvent event_type, FileCreationCallback callback)
{
    char* path = NULL;
    const char *ext = NULL;

    path = g_file_get_path (file);
    if (!path)
        return;

    /* Find the file extension. */
    ext = strrchr (path, '.');
    if (!ext || !path) {
        if (path)
            g_free (path);
        return;
    }
    if (strcmp (ext, ".upload") == 0) {
        if ((event_type == G_FILE_MONITOR_EVENT_CREATED) ||
            (event_type == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED))
            create_file (path, callback);
    }
    g_free (path);
}

void
monitor_directory (const char* directory, FileCreationCallback callback)
{
    GFileMonitor* monitor = NULL;
    GError* err = NULL;
    GFile* path = NULL;

    mkdir (directory, 0755);
    path = g_file_new_for_path (directory);
    monitor = g_file_monitor_directory (path, G_FILE_MONITOR_NONE, NULL, &err);
    g_object_unref (path);
    if (err) {
        g_print ("Unable to monitor %s: %s\n", directory, err->message);
        g_error_free (err);
    } else {
        g_signal_connect (monitor, "changed", G_CALLBACK (changed_event),
                          callback);
    }
}

