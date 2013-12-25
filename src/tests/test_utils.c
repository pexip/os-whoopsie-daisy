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
#include <glib-object.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>

#include "../src/utils.h"

static void
test_response_string (void)
{
    struct response_string s;
    init_response_string (&s);
    grow_response_string (&s, "foo", 3);
    grow_response_string (&s, "bar", 3);
    if (s.p[6] != '\0' || strcmp (s.p, "foobar") != 0)
        g_test_fail ();
    if (s.length != 6)
        g_test_fail ();
    destroy_response_string (&s);
    if (s.p != NULL || s.length != 0)
        g_test_fail ();
}

static void
test_change_file_extension (void)
{
    char* new = NULL;

    new = change_file_extension (NULL, ".upload");
    if (new) {
        g_test_fail ();
        g_free (new);
    }
    new = change_file_extension ("", ".upload");
    if (new) {
        g_test_fail ();
        g_free (new);
    }

    new = change_file_extension ("foo", ".upload");
    if (new) {
        g_test_fail ();
        g_free (new);
    }

    new = change_file_extension ("foo.crash", "");
    if (!new) {
        g_test_fail ();
    } else if (strcmp (new, "foo") != 0) {
        g_test_fail ();
        g_free (new);
    }

    new = change_file_extension ("foo.crash", ".upload");
    if (!new)
        g_test_fail ();
    else if (strcmp (new, "foo.upload") != 0) {
        g_test_fail ();
        g_free (new);
    }
}

static void
test_already_handled_report (void)
{
    int fd;
    char template[12] = "/tmp/XXXXXX";
    char* crash_file = NULL;
    char* uploaded_file = NULL;
    char* upload_file = NULL;

    struct stat upload_stat;
    struct stat uploaded_stat;
    struct utimbuf new_time;

    mktemp (template);
    if (*template == '\0')
        g_warning ("Couldn't create temporary file.");

    asprintf (&crash_file, "%s.crash", template);
    asprintf (&upload_file, "%s.upload", template);
    asprintf (&uploaded_file, "%s.uploaded", template);

    /* .crash file does not exist */
    if (!already_handled_report (crash_file))
        g_test_fail ();

    /* Crash file without an extension */
    fd = creat (template, 0600);
    close (fd);

    if (!already_handled_report (template))
        g_test_fail ();
    if (unlink (template) < 0)
        g_warning ("Unable to remove %s.", template);

    /* .uploaded file does not exist (we haven't processed this crash) */
    fd = creat (crash_file, 0600);
    close (fd);
    fd = creat (upload_file, 0600);
    close (fd);

    if (already_handled_report (crash_file))
        g_test_fail ();
    if (unlink (crash_file) < 0)
        g_warning ("Unable to remove %s.", crash_file);
    if (unlink (upload_file) < 0)
        g_warning ("Unable to remove %s.", upload_file);

    /* .uploaded file is newer than .crash file (we've processed this crash) */
    fd = creat (crash_file, 0600);
    close (fd);
    fd = creat (upload_file, 0600);
    close (fd);
    fd = creat (uploaded_file, 0600);
    close (fd);

    stat (uploaded_file, &uploaded_stat);
    new_time.actime = uploaded_stat.st_atime;
    new_time.modtime = uploaded_stat.st_mtime + 1;
    if (utime (uploaded_file, &new_time) < 0)
        g_warning ("Could not update utime.");

    if (!already_handled_report (crash_file))
        g_test_fail ();

    /* .upload file is newer than .uploaded file (we haven't processed this
     * updated crash) */

    stat (upload_file, &upload_stat);
    new_time.actime = upload_stat.st_atime;
    new_time.modtime = uploaded_stat.st_mtime + 2;
    if (utime (upload_file, &new_time) < 0)
        g_warning ("Could not update utime.");

    if (already_handled_report (crash_file))
        g_test_fail ();

    if (unlink (crash_file) < 0)
        g_warning ("Unable to remove %s.", crash_file);
    if (unlink (upload_file) < 0)
        g_warning ("Unable to remove %s.", upload_file);
    if (unlink (uploaded_file) < 0)
        g_warning ("Unable to remove %s.", uploaded_file);
}

static void
test_mark_handled (void)
{
    char template[12] = "/tmp/XXXXXX";
    char* crash_file = NULL;
    char* uploaded_file = NULL;
    struct stat uploaded_stat;

    mktemp (template);
    asprintf (&crash_file, "%s.crash", template);
    asprintf (&uploaded_file, "%s.uploaded", template);
    if (!mark_handled (crash_file)) {
        g_test_fail ();
        goto out;
    }

    if (stat (uploaded_file, &uploaded_stat) < 0) {
        g_test_fail ();
        goto out;
    }
    if (!(S_ISREG(uploaded_stat.st_mode)))
        g_test_fail ();
    if ((uploaded_stat.st_mode & 0600) != 0600)
        g_test_fail ();

    if (unlink (uploaded_file) < 0)
        g_warning ("Unable to remove %s.", uploaded_file);

out:
    if (crash_file)
        free (crash_file);
    if (uploaded_file)
        free (uploaded_file);
}

int
main (int argc, char** argv)
{
    g_type_init ();
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/whoopsie/change-file-extension", test_change_file_extension);
    g_test_add_func ("/whoopsie/already-handled-report", test_already_handled_report);
    g_test_add_func ("/whoopsie/response-string", test_response_string);
    g_test_add_func ("/whoopsie/mark-handled", test_mark_handled);
    return g_test_run ();
}
