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

#include <string.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include "utils.h"

void
init_response_string (struct response_string* resp)
{
    resp->length = 0;
    resp->p = g_malloc (1);
    resp->p[0] = '\0';
}

void
grow_response_string (struct response_string* resp, char* str, size_t length)
{
    size_t new_length = resp->length + length;
    resp->p = g_realloc (resp->p, new_length + 1);
    memcpy (resp->p + resp->length, str, length);
    resp->p[new_length] = '\0';
    resp->length = new_length;
}

void
destroy_response_string (struct response_string* resp)
{
    if (!resp || !(resp->p))
        return;
    g_free (resp->p);
    resp->p = NULL;
    resp->length = 0;
}

char*
change_file_extension (const char* path, const char* extension)
{
    /* Ex. (/var/crash/foo.crash, .upload) -> /var/crash/foo.upload */
    char* new_path = NULL;
    const char* p = NULL;
    int len = 0;
    int i = 0;
    int ext_len = 0;

    if (!extension || !path)
        return NULL;

    /* Find the file extension. */
    p = path;
    while (*p != '\0') {
        if (*p == '.')
            len = i;
        p++; i++;
    }
    if (len <= 0)
        return NULL;

    ext_len = strlen (extension);
    new_path = g_malloc (len + ext_len + 1);
    memcpy (new_path, path, len);
    strncpy (new_path + len, extension, ext_len);
    /* Ensure the new string is NUL terminated */
    new_path[len + ext_len] = '\0';
    return new_path;
}

gboolean
already_handled_report (const char* crash_file)
{
    /* If the .uploaded path exists and .uploaded is newer than .upload */
    char* upload_file = NULL;
    char* uploaded_file = NULL;
    struct stat uploaded_stat;
    struct stat upload_stat;
    struct stat crash_stat;

    if (stat (crash_file, &crash_stat) < 0)
        /* .crash doesn't exist; don't process. */
        return TRUE;

    upload_file = change_file_extension (crash_file, ".upload");
    if (!upload_file)
        /* Bogus crash file; don't process. */
        return TRUE;

    if (stat (upload_file, &upload_stat) < 0)
        /* .upload doesn't exist; don't process. */
        return TRUE;

    free (upload_file);
    
    uploaded_file = change_file_extension (crash_file, ".uploaded");
    if (stat (uploaded_file, &uploaded_stat) < 0) {
        /* .uploaded doesn't exist. */
        free (uploaded_file);
        return FALSE;
    }

    free (uploaded_file);

    /* If .uploaded is newer than .upload, then we've already handled this
     * report */
    return (uploaded_stat.st_mtime > upload_stat.st_mtime);
}

gboolean
mark_handled (const char* crash_file)
{
    char* uploaded_file = NULL;
    int fd;

    uploaded_file = change_file_extension (crash_file, ".uploaded");
    if (!uploaded_file)
        return FALSE;

    fd = creat (uploaded_file, 0600);
    free (uploaded_file);
    
    if (fd < 0)
        return FALSE;
    else {
        close (fd);
        return TRUE;
    }
}
