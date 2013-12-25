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

#define _GNU_SOURCE

#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>

#include "../src/monitor.h"

static GMainLoop* loop = NULL;

gboolean
_test_callback_never_triggered_callback (const char* path)
{
    g_main_loop_quit (loop);
    g_test_fail ();
    return FALSE;
}

void
test_callback_never_triggered (void)
{
    monitor_directory ("/fake", _test_callback_never_triggered_callback);
    loop = g_main_loop_new (NULL, FALSE);

    g_timeout_add_seconds (0.5, (GSourceFunc) g_main_loop_quit, loop);
    g_main_loop_run (loop);
    g_main_loop_unref (loop);
}

gboolean
_create_crash_file (const char* directory)
{
    char* path = NULL;
    int fd;

    asprintf (&path, "%s/fake.crash", directory);
    fd = creat (path, 0600);
    if (fd < 0) {
        g_warning ("Couldn't create temporary crash file.");
        g_test_fail ();
        free (path);
        return FALSE;
    }
    close (fd);
    free (path);

    return FALSE;
}

gboolean
_create_upload_file (const char* directory)
{
    char* path = NULL;
    int fd;
    struct stat path_stat;
    struct utimbuf new_time;

    asprintf (&path, "%s/fake.upload", directory);
    fd = creat (path, 0600);
    if (fd < 0) {
        g_warning ("Couldn't create temporary upload file.");
        g_test_fail ();
        free (path);
        return FALSE;
    }
    close (fd);

    stat (path, &path_stat);
    new_time.actime = path_stat.st_atime;
    new_time.modtime = path_stat.st_mtime + 1;
    if (utime (path, &new_time) < 0) {
        g_warning ("Couldn't update utime.");
        g_test_fail ();
    }
    free (path);
    return FALSE;
}

gboolean
_test_callback_triggered_once (const char* crash_file)
{
    if (!g_str_has_suffix (crash_file, ".crash"))
        g_test_fail ();
    g_main_loop_quit (loop);
    return TRUE;
}

void
_test_callback_timeout (void)
{
    g_main_loop_quit (loop);
    g_test_fail ();
}

void
test_callback_triggered_once (void)
{
    char template[12] = "/tmp/XXXXXX";
    char* path = NULL;
    const char* fake_files[] = { "crash", "upload", "uploaded", NULL };
    const char** p = fake_files;
    struct stat fake_stat;
    int id;

    mktemp (template);
    if (*template == '\0') {
        g_warning ("Couldn't create temporary file.");
        g_test_fail ();
        return;
    }

    if (mkdir (template, 0755) < 0) {
        g_warning ("Couldn't create temporary directory.");
        g_test_fail ();
        return;
    }

    g_idle_add ((GSourceFunc) _create_crash_file, template);
    g_idle_add ((GSourceFunc) _create_upload_file, template);
    id = g_timeout_add_seconds (5, (GSourceFunc) _test_callback_timeout, NULL);
    monitor_directory (template, _test_callback_triggered_once);
    loop = g_main_loop_new (NULL, FALSE);

    g_main_loop_run (loop);
    g_source_remove (id);
    g_main_loop_unref (loop);

    while (*p) {
        asprintf (&path, "%s/fake.%s", template, *p);
        if (stat (path, &fake_stat)) {
            g_warning ("Expected file %s doesn't exist.", path);
            g_test_fail ();
            free (path);
            return;
        }
        if (unlink (path) < 0) {
            g_warning ("Couldn't remove temporary file, %s", path);
            g_test_fail ();
            free (path);
            return;
        }
        p++;
        free (path);
    }

    if (rmdir (template) < 0) {
        g_warning ("Couldn't remove temporary directory.");
        g_test_fail ();
        return;
    }
}

void
test_callback_not_triggered_without_upload_file (void)
{
    char template[12] = "/tmp/XXXXXX";
    int id;

    mktemp (template);
    if (*template == '\0') {
        g_warning ("Couldn't create temporary file.");
        g_test_fail ();
        return;
    }

    if (mkdir (template, 0755) < 0) {
        g_warning ("Couldn't create temporary directory.");
        g_test_fail ();
        return;
    }

    g_idle_add ((GSourceFunc) _create_crash_file, template);
    loop = g_main_loop_new (NULL, FALSE);
    id = g_timeout_add_seconds (2, (GSourceFunc) g_main_loop_quit, loop);

    monitor_directory (template, _test_callback_never_triggered_callback);
    g_main_loop_run (loop);
    g_source_remove (id);
    g_main_loop_unref (loop);
}

int
main (int argc, char** argv)
{
    g_type_init ();
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/whoopsie/callback-never-triggered",
                     test_callback_never_triggered);
    g_test_add_func ("/whoopsie/callback-triggered-once",
                     test_callback_triggered_once);
    g_test_add_func ("/whoopsie/callback-not-triggered-without-upload-file",
                     test_callback_not_triggered_without_upload_file);
    return g_test_run ();
}
