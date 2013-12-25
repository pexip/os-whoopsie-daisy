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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <assert.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <errno.h>
#include <signal.h>
#include <gcrypt.h>
#include <pwd.h>
#include <grp.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "bson/bson.h"
#include "whoopsie.h"
#include "utils.h"
#include "connectivity.h"
#include "monitor.h"

/* The length of time to wait before processing outstanding crashes, in seconds
 */
#define PROCESS_OUTSTANDING_TIMEOUT 7200

/* If true, we have an active Internet connection. True by default in case we
 * can't bring up GNetworkMonitor */
static gboolean online_state = TRUE;

static GMainLoop* loop = NULL;

/* The URL of the crash database. */
static char* crash_db_url = NULL;

/*  The system UUID, taken from the DMI tables and SHA-512 hashed */
#define HASHLEN 128
static char sha512_system_uuid[HASHLEN + 1] = {0};

/* The URL for sending the initial crash report */
static char* crash_db_submit_url = NULL;

/* Username we will run under */
static const char username[] = "whoopsie";

/* The file descriptor for our instance lock */
static int lock_fd = 0;

/* Options */

static gboolean foreground = FALSE;

static GOptionEntry option_entries[] = {
    { "foreground", 'f', 0, G_OPTION_ARG_NONE, &foreground,
      "Run in the foreground", NULL },
    { NULL }
};

static const char* acceptable_fields[] = {
    "ProblemType",
    "Date",
    "Traceback",
    "Signal",
    "PythonArgs",
    "Package",
    "SourcePackage",
    "PackageArchitecture",
    "Dependencies",
    "MachineType",
    "StacktraceAddressSignature",
    "ApportVersion",
    "DuplicateSignature",
    /* add_os_info */
    "DistroRelease",
    "Uname",
    "Architecture",
    "NonfreeKernelModules",
    /* add_user_info */
    "UserGroups",
    /* add_proc_info */
    "ExecutablePath",
    "InterpreterPath",
    "ExecutableTimestamp",
    "ProcCwd",
    "ProcEnviron",
    "ProcCmdline",
    "ProcStatus",
    "ProcMaps",
    "ProcAttrCurrent",
    /* add_gdb_info */
    "Registers",
    "Disassembly",
    "Stacktrace",
    "ThreadStacktrace",
    "StacktraceTop",
    "AssertionMessage",
    "ProcAttrCurrent",
    "CoreDump",
    /* add_kernel_crash_info */
    "VmCore",

    /* Fields we do not care about: */

    /* We would only want this to see how many bugs would otherwise go
     * unreported: */
    /* "UnreportableReason", */

    /* We'll have our own count in the database. */
    /* "CrashCounter", */

    /* "Title", */
    NULL,
};

static gboolean
is_acceptable_field (const char* field)
{
    const char** p = acceptable_fields;
    while (*p) {
        if (strcmp (*p, field) == 0)
            return TRUE;
        p++;
    }
    return FALSE;
}

static void
parse_arguments (int* argc, char** argv[])
{
    GError* err = NULL;
    GOptionContext* context;

    context = g_option_context_new (NULL);
    g_option_context_add_main_entries (context, option_entries, NULL);
    if (!g_option_context_parse (context, argc, argv, &err)) {
        g_print ("whoopsie: %s\n", err->message);
        g_error_free (err);
        exit (EXIT_FAILURE);
    }
    g_option_context_free (context);
}

gboolean
append_key_value (gpointer key, gpointer value, gpointer bson_string)
{
    /* Takes a key and its value from a #GHashTable and adds it to a BSON string
     * as key and its string value. Return %FALSE on error. */

    bson* str = (bson*) bson_string;
    char* k = (char*) key;
    char* v = (char*) value;

    /* We don't send the core dump in the first upload, as the server might not
     * need it */
    if (!strcmp ("CoreDump", k))
        return TRUE;

    return bson_append_string (str, k, v) != BSON_ERROR;
}

size_t
server_response (char* ptr, size_t size, size_t nmemb, void* s)
{
    struct response_string* resp = (struct response_string*) s;
    grow_response_string (resp, ptr, size * nmemb);
    return size * nmemb;
}

void
split_string (char* head, char** tail)
{
    *tail = strchr (head, ' ');
    if (*tail) {
        **tail = '\0';
        (*tail)++;
    }
}


gboolean
bsonify (GHashTable* report, bson* b, const char** bson_message,
         int* bson_message_len)
{
    /* Attempt to convert a #GHashTable of the report into a BSON string.
     * On error return %FALSE. */

    GHashTableIter iter;
    gpointer key, value;

    *bson_message = NULL;
    *bson_message_len = 0;

    assert (report != NULL);

    bson_init (b);
    if (!bson_data (b))
        return FALSE;

    g_hash_table_iter_init (&iter, report);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        if (!append_key_value (key, value, b))
            return FALSE;
    }
    if (bson_finish (b) == BSON_ERROR)
        return FALSE;

    *bson_message = bson_data (b);
    *bson_message_len = bson_size (b);
    if (*bson_message_len > 0 && *bson_message)
        return TRUE;
    else
        return FALSE;
}

int
upload_report (const char* message_data, int message_len, struct response_string* s)
{
    CURL* curl = NULL;
    CURLcode result_code = 0;
    long response_code = 0;
    struct curl_slist* list = NULL;

    if ((curl = curl_easy_init ()) == NULL) {
        printf ("Couldn't init curl.\n");
        return FALSE;
    }
    curl_easy_setopt (curl, CURLOPT_POST, 1);
    curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 1);
    list = curl_slist_append (list, "Content-Type: application/octet-stream");
    list = curl_slist_append (list, "X-Whoopsie-Version: " VERSION);
    curl_easy_setopt (curl, CURLOPT_URL, crash_db_submit_url);
    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, message_len);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDS, (void*)message_data);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, server_response);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, s);
    curl_easy_setopt (curl, CURLOPT_VERBOSE, 0L);

    result_code = curl_easy_perform (curl);
    curl_slist_free_all(list);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response_code);

    printf ("Sent; server replied with: %s\n",
        curl_easy_strerror (result_code));
    printf ("Response code: %ld\n", response_code);
    curl_easy_cleanup (curl);

    if (result_code != CURLE_OK)
        return result_code;
    else
        return response_code;
}

void
destroy_key_and_value (gpointer key, gpointer value, gpointer user_data)
{
    if (key)
        g_free (key);
    /* The value may be "", which is allocated on the stack. */
    if (value && *(char*)value != '\0')
        g_free (value);
}

GHashTable*
parse_report (const char* report_path, gboolean full_report, GError** error)
{
    /* We'll eventually modify the contents of the report, rather than sending
     * it as-is, to make it more amenable to what the server has to stick in
     * the database, and thus creating less work server-side.
     */

    GMappedFile* fp = NULL;
    GHashTable* hash_table = NULL;
    gchar* contents = NULL;
    gsize file_len = 0;
    /* Our position in the file. */
    gchar* p = NULL;
    /* The end or length of the token. */
    gchar* token_p = NULL;
    char* key = NULL;
    char* value = NULL;
    gchar* value_p = NULL;
    GError* err = NULL;
    gchar* end = NULL;
    int value_length;
    int value_pos;
    gboolean ignore_field = FALSE;

    if (g_file_test (report_path, G_FILE_TEST_IS_SYMLINK) ||
        !g_file_test (report_path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "%s is a symlink or is not a regular file.", report_path);
        return NULL;
    }
    /* TODO handle the file being modified underneath us. */
    fp = g_mapped_file_new (report_path, FALSE, &err);
    if (err) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Unable to map report: %s", err->message);
        g_error_free (err);
        goto error;
    }
        
    contents = g_mapped_file_get_contents (fp);
    file_len = g_mapped_file_get_length (fp);
    end = contents + file_len;
    hash_table = g_hash_table_new (g_str_hash, g_str_equal);
    p = contents;

    while (p < end) {
        /* We're either at the beginning of the file or the start of a line,
         * otherwise this report is corrupted. */
        if (!(p == contents || *(p-1) == '\n')) {
            g_set_error (error, g_quark_from_static_string ("whoopsie-quark"),
                         0, "Malformed report.");
            goto error;
        }
        if (*p == ' ') {
            if (!key && !ignore_field) {
                g_set_error (error,
                             g_quark_from_static_string ("whoopsie-quark"), 0,
                             "Report may not start with a value.");
                goto error;
            }
            /* Skip the space. */
            p++;
            token_p = p;
            while (token_p < end && *token_p != '\n')
                token_p++;

            if (!ignore_field) {
                /* The length of this value string */
                value_length = token_p - p;
                if (value) {
                    /* Space for the leading newline too. */
                    value_pos = value_p - value;
                    value = g_realloc (value, value_pos + 1 + value_length + 1);
                    value_p = value + value_pos;
                    *value_p = '\n';
                    value_p++;
                } else {
                    value = g_realloc (value, value_length + 1);
                    value_p = value;
                }
                memcpy (value_p, p, value_length);
                value_p[value_length] = '\0';
                for (char *c = value_p; c < value_p + value_length; c++)
                    /* If c is a control character. */
                    if (*c >= '\0' && *c < ' ')
                        *c = '?';
                value_p += value_length;
                g_hash_table_insert (hash_table, key, value ? value : "");
            }
            p = token_p + 1;
        } else {
            /* Reset the value pointer. */
            value = NULL;
            /* Key. */
            token_p = p;
            while (token_p < end) {
                if (*token_p != ':') {
                    if (*token_p == '\n') {
                        /* No colon character found on this line */
                        g_set_error (error,
                                g_quark_from_static_string ("whoopsie-quark"),
                                0, "Report key must have a value.");
                        goto error;
                    }
                    token_p++;
                } else if ((*(token_p + 1) == '\n' &&
                            *(token_p + 2) != ' ')) {
                        /* The next line doesn't start with a value */
                        g_set_error (error,
                                g_quark_from_static_string ("whoopsie-quark"),
                                0, "Report key must have a value.");
                        goto error;
                } else {
                    break;
                }
            }
            key = g_malloc ((token_p - p) + 1);
            memcpy (key, p, (token_p - p));
            key[(token_p - p)] = '\0';

            /* Replace any embedded NUL bytes. */
            for (char *c = key; c < key + (token_p - p); c++)
                if (*c >= '\0' && *c < ' ')
                    *c = '?';

            /* Should we send this field? */
            if (!full_report)
                ignore_field = !is_acceptable_field (key);

            /* Eat the semicolon. */
            token_p++;

            /* Skip any leading spaces. */
            while (token_p < end && *token_p == ' ')
                token_p++;

            /* Start of the value. */
            p = token_p;

            while (token_p < end && *token_p != '\n')
                token_p++;
            if ((token_p - p) == 0) {
                /* Empty value. The key likely has a child. */
                value = NULL;
            } else if (ignore_field) {
                value = NULL;
            } else {
                if (!strncmp ("base64", p, 6)) {
                    /* Just a marker that the following lines are base64
                     * encoded. Don't include it in the value. */
                    value = NULL;
                } else {
                    /* Value. */
                    value = g_malloc ((token_p - p) + 1);
                    memcpy (value, p, (token_p - p));
                    value[(token_p - p)] = '\0';
                    for (char *c = value; c < value + (token_p - p); c++)
                        if (*c >= '\0' && *c < ' ')
                            *c = '?';
                    value_p = value + (token_p - p);
                }
            }
            p = token_p + 1;
            if (!ignore_field)
                g_hash_table_insert (hash_table, key, value ? value : "");
            else
                g_free (key);
        }
    }
    g_mapped_file_unref (fp);
    return hash_table;

error:
    if (hash_table) {
        g_hash_table_foreach (hash_table, destroy_key_and_value, NULL);
        g_hash_table_destroy (hash_table);
    }
    g_mapped_file_unref (fp);
    return NULL;
}

gboolean
upload_core (const char* uuid, const char* arch, const char* core_data) {

    CURL* curl = NULL;
    CURLcode result_code = 0;
    long response_code = 0;
    struct curl_slist* list = NULL;
    char* crash_db_core_url = NULL;
    struct response_string s;

    if (asprintf (&crash_db_core_url, "%s/%s/submit-core/%s/%s",
        crash_db_url, uuid, arch, sha512_system_uuid) < 0)
        g_error ("Unable to allocate memory.");

    /* TODO use CURLOPT_READFUNCTION to transparently compress data with
     * Snappy. */
    if ((curl = curl_easy_init ()) == NULL) {
        printf ("Couldn't init curl.\n");
        return FALSE;
    }
    init_response_string (&s);
    curl_easy_setopt (curl, CURLOPT_POST, 1);
    curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 1);
    list = curl_slist_append (list, "Content-Type: application/octet-stream");
    list = curl_slist_append (list, "X-Whoopsie-Version: " VERSION);
    curl_easy_setopt (curl, CURLOPT_URL, crash_db_core_url);
    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDS, (void*)core_data);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, server_response);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt (curl, CURLOPT_VERBOSE, 0L);

    result_code = curl_easy_perform (curl);
    curl_slist_free_all(list);

    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response_code);

    printf ("Sent; server replied with: %s\n",
        curl_easy_strerror (result_code));
    printf ("Response code: %ld\n", response_code);
    curl_easy_cleanup (curl);
    free (crash_db_core_url);
    destroy_response_string (&s);

    return result_code == CURLE_OK && response_code == 200;
}

void
handle_response (GHashTable* report, char* response_data)
{
    char* command = NULL;
    char* core = NULL;
    char* arch = NULL;

    /* Command could be CORE, which requests the core dump, BUG ######, if in a
     * development release, which points to the bug report, or UPDATE, if this
     * is fixed in an update. */
    split_string (response_data, &command);
    if (command) {
        if (strcmp (command, "CORE") == 0) {
            core = g_hash_table_lookup (report, "CoreDump");
            arch = g_hash_table_lookup (report, "Architecture");
            if (core && arch) {
                if (!upload_core (response_data, arch, core))
                    /* We do not retry the upload. Once is a big enough hit to
                     * their Internet connection, and we can always count on
                     * the next person in line to send it. */
                    printf ("Upload of the core dump failed.\n");
            } else
                printf ("Asked for a core dump that we don't have.\n");
        } else
            printf ("Got command: %s\n", command);
    }
}

gboolean
parse_and_upload_report (const char* crash_file)
{
    GHashTable* report = NULL;
    gboolean success = FALSE;
    int message_len = 0;
    const char* message_data = NULL;
    struct response_string s;
    GError* error = NULL;
    bson b[1];
    int response = 0;

    g_print ("Uploading %s.\n", crash_file);
    report = parse_report (crash_file, FALSE, &error);
    if (!report) {
        if (error) {
            g_print ("Unable to parse report (%s): %s\n", crash_file,
                       error->message);
            g_error_free (error);
        } else {
            g_print ("Unable to parse report (%s)\n", crash_file);
        }
        /* Do not keep trying to parse and upload this */
        return TRUE;
    }

    if (!bsonify (report, b, &message_data, &message_len)) {
        g_print ("Unable to bsonify report (%s)\n", crash_file);
        if (bson_data (b))
            bson_destroy (b);
        /* Do not keep trying to parse and upload this */
        success = TRUE;
    } else {
        init_response_string (&s);
        response = upload_report (message_data, message_len, &s);
        if (bson_data (b))
            bson_destroy (b);

        /* If the response code is 400, the server did not like what we sent it.
         * Sending the same thing again is not likely to change that */
        /* TODO check that there aren't 400 responses that we care about seeing
         * again, such as a transient database failure. */
        if (response == 200 || response == 400)
            success = TRUE;
        else
            success = FALSE;

        if (response == 400)
            g_print ("Server replied with:\n%s\n", s.p);

        if (response == 200 && s.length > 0)
            handle_response (report, s.p);
        destroy_response_string (&s);
    }

    g_hash_table_foreach (report, destroy_key_and_value, NULL);
    g_hash_table_destroy (report);

    return success;
}

gboolean
process_existing_files (void)
{
    GDir* dir = NULL;
    const gchar* file = NULL;
    const gchar* ext = NULL;
    char* upload_file = NULL;
    char* crash_file = NULL;

    dir = g_dir_open ("/var/crash", 0, NULL);
    while ((file = g_dir_read_name (dir)) != NULL) {

        upload_file = g_build_filename ("/var/crash", file, NULL);
        ext = strrchr (upload_file, '.');
        if (ext && strcmp(++ext, "upload") != 0) {
            free (upload_file);
            continue;
        }

        crash_file = change_file_extension (upload_file, ".crash");
        if (!crash_file) {
            /* FIXME this would be bad - we'd keep uploading it */
            if (!mark_handled (crash_file))
                g_print ("Unable to mark report as seen (%s)\n", crash_file);
        } else if (already_handled_report (crash_file)) {
            if (!mark_handled (crash_file))
                g_print ("Unable to mark report as seen (%s)\n", crash_file);
        } else if (online_state && parse_and_upload_report (crash_file)) {
            if (!mark_handled (crash_file))
                g_print ("Unable to mark report as seen (%s)\n", crash_file);

        } 

        free (upload_file);
        free (crash_file);
    }
    g_dir_close (dir);

    return TRUE;
}

void daemonize (void)
{
    pid_t pid, sid;
    int i;
    struct rlimit rl = {0};

    if (getrlimit (RLIMIT_NOFILE, &rl) < 0) {
        g_print ("Could not get resource limits.\n");
        exit (EXIT_FAILURE);
    }

    umask (0);
    pid = fork();
    if (pid < 0)
        exit (EXIT_FAILURE);
    if (pid > 0)
        exit (EXIT_SUCCESS);
    sid = setsid ();
    if (sid < 0)
        exit (EXIT_FAILURE);

    if ((chdir ("/")) < 0)
        exit (EXIT_FAILURE);

    for (i = 0; i < rl.rlim_max && i < 1024; i++)
        close (i);
    if ((open ("/dev/null", O_RDWR) != 0) ||
        (dup (0) != 1) ||
        (dup (0) != 2)) {
        g_print ("Could not redirect file descriptors to /dev/null.\n");
        exit (EXIT_FAILURE);
    }
}

void
exit_if_already_running (void)
{
    int rc = 0;
    if (mkdir ("/var/lock/whoopsie", 0755) < 0) {
        if (errno != EEXIST) {
            g_print ("Could not create lock directory.\n");
        }
    }
    lock_fd = open ("/var/lock/whoopsie/lock", O_CREAT | O_RDWR, 0600);
    rc = flock (lock_fd, LOCK_EX | LOCK_NB);
    if (rc) {
        if (EWOULDBLOCK == errno) {
            g_print ("Another instance is already running.\n");
            exit (1);
        } else {
            g_print ("Could not create lock file: %s\n", strerror (errno));
        }
    }
}

static void
handle_signals (int signo)
{
    if (loop)
        g_main_loop_quit (loop);
    else
        exit (0);
}

static void
setup_signals (void)
{
    struct sigaction action;
    sigset_t mask;

    sigemptyset (&mask);
    action.sa_handler = handle_signals;
    action.sa_mask = mask;
    action.sa_flags = 0;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
}

void
hex_to_char (char* buf, const unsigned char *str, int len)
{
    char* p = NULL;
    int i = 0;

    p = buf;
    for (i = 0; i < len; i++) {
        snprintf(p, 3, "%02x", str[i]);
        p += 2;
    }
    buf[2*len] = 0;
}

void
get_system_uuid (char* res, GError** error)
{
    int fp;
    char system_uuid[37] = {0};
    int md_len;
    unsigned char* id = NULL;
    gcry_md_hd_t sha512 = NULL;

    fp = open ("/sys/class/dmi/id/product_uuid", O_RDONLY);
    if (fp < 0) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Could not open the product uuid file.");
        return;
    }
    if (read (fp, system_uuid, 36) == 36) {
        system_uuid[36] = '\0';
        close (fp);
    } else {
        close (fp);
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Got an unexpected length reading the product_uuid.");
        return;
    }

    if (!gcry_check_version (GCRYPT_VERSION)) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "libcrypt version mismatch.");
        return;
    }
    gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
    md_len = gcry_md_get_algo_dlen(GCRY_MD_SHA512);
    if (md_len != HASHLEN / 2) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                 "Received an incorrect size for the SHA512 message digest");
        return;
    }
    if (gcry_md_open (&sha512, GCRY_MD_SHA512, 0) || sha512 == NULL) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
             "Failed to create a SHA512 message digest for the product_uuid.");
        return;
    }
    gcry_md_write (sha512, system_uuid, 36);
    gcry_md_final (sha512);
    id = gcry_md_read (sha512, GCRY_MD_SHA512);
    if (id == NULL) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
             "Failed to read the SHA512 message digest for the product_uuid.");
        gcry_md_close (sha512);
        return;
    }
    hex_to_char (res, id, md_len);
    gcry_md_close (sha512);
}

char*
get_crash_db_url (void)
{
    char* url = NULL;

    url = getenv ("CRASH_DB_URL");
    if (url == NULL)
        return NULL;

    if ((strncasecmp ("http://", url, 7) || url[7] == '\0') &&
        (strncasecmp ("https://", url, 8) || url[8] == '\0'))
        return NULL;
    return g_strdup (url);
}

void
drop_privileges (GError** error)
{
    struct passwd *pw = NULL;

    if (getuid () != 0) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "You must be root to run this program.");
        return;
    }
    if (!(pw = getpwnam (username))) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Failed to find user: %s", username);
        return;
    }

    /* Drop privileges */
    if (setgroups (1, &pw->pw_gid) < 0 ||
        setresgid (pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0 ||
        setresuid (pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Failed to become user: %s", username);
        return;
    }

    if (prctl (PR_SET_DUMPABLE, 1))
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Failed to ensure core dump production.");

    if ((setenv ("USER", username, 1) < 0) ||
        (setenv ("USERNAME", username, 1) < 0)) {
        g_set_error (error, g_quark_from_static_string ("whoopsie-quark"), 0,
                     "Failed to set user environment variables.");
        return;
    }
}

void
network_changed (gboolean available)
{
    g_print (available ? "online\n" : "offline\n");
    if (!available) {
        online_state = FALSE;
        return;
    }

    if (online_state && available)
        return;

    online_state = available;

    if (online_state)
        process_existing_files ();
}

gboolean
check_online_then_upload (const char* crash_file) {

    if (!online_state) {
        g_print ("Not online; processing later (%s).\n", crash_file);
        return FALSE;
    }

    if (!parse_and_upload_report (crash_file)) {
        g_print ("Could not upload; processing later (%s).\n", crash_file);
        return FALSE;
    }

    return TRUE;
}

#ifndef TEST
int
main (int argc, char** argv)
{
    GError* err = NULL;

    setup_signals ();
    parse_arguments (&argc, &argv);

    if ((crash_db_url = get_crash_db_url ()) == NULL) {
        g_print ("Could not get crash database location.\n");
        exit (EXIT_FAILURE);
    }
    get_system_uuid (sha512_system_uuid, &err);
    if (err) {
        g_print ("%s\n", err->message);
        g_error_free (err);
        err = NULL;
    }
    if (*sha512_system_uuid != '\0') {
        if (asprintf (&crash_db_submit_url, "%s/%s",
            crash_db_url, sha512_system_uuid) < 0)
            g_error ("Unable to allocate memory.");
    } else {
        crash_db_submit_url = strdup (crash_db_url);
    }

    drop_privileges (&err);
    if (err) {
        g_print ("%s\n", err->message);
        g_error_free (err);
        exit (EXIT_FAILURE);
    }
    exit_if_already_running ();

    if (!foreground)
        daemonize ();

    g_type_init ();

    /* TODO use curl_share for DNS caching. */
    if (curl_global_init (CURL_GLOBAL_ALL)) {
        g_print ("Unable to initialize curl.\n\n");
        exit (EXIT_FAILURE);
    }

    monitor_directory ("/var/crash", check_online_then_upload);
    monitor_connectivity (crash_db_url, network_changed);
    process_existing_files ();
    g_timeout_add_seconds (PROCESS_OUTSTANDING_TIMEOUT,
                           (GSourceFunc) process_existing_files, NULL);

    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    g_unlink ("/var/lock/whoopsie/lock");
    close (lock_fd);
    curl_global_cleanup ();

    if (crash_db_url)
        free (crash_db_url);
    if (crash_db_submit_url)
        free (crash_db_submit_url);
	return 0;
}
#endif
