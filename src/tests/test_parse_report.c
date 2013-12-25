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
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../src/whoopsie.h"

static void
test_parse_report (void)
{
    int fd;
    char template[12] = "/tmp/XXXXXX";
    char* command[4] = {"/usr/bin/base64", "-d", NULL, NULL};
    char* core  = NULL;
    int val = -1;

    GHashTable* report = NULL;
    int i = 0;
    gpointer key = NULL;
    char* keys[] = {
        "ProblemType",
        "Architecture",
        "Package",
        "Date",
        "DistroRelease",
        "ExecutablePath",
        "ProcCmdline",
        "ProcCwd",
        "ProcEnviron",
        "ProcMaps",
        "ProcStatus",
        "Signal",
        "Uname",
        "UserGroups",
        "CoreDump",
        NULL,
    };
    report = parse_report (TEST_DIR "/data/_usr_bin_gedit.1000.crash",
                           FALSE, NULL);
    g_assert (report != NULL);
    while (keys[i] != NULL) {
        key = NULL;
        key = g_hash_table_lookup (report, keys[i]);
        if (key == NULL)
            g_error ("%s was not found.", keys[i]);
        i++;
    }

    fd = mkstemp (template);
    core = g_hash_table_lookup (report, "CoreDump");
    write (fd, core, strlen(core));
    close (fd);
    command[2] = template;
    g_spawn_sync (NULL, command, NULL, G_SPAWN_STDOUT_TO_DEV_NULL,
                  NULL, NULL, NULL, NULL, &val, NULL);
    if (val != 0) {
        g_test_fail ();
    }
    unlink (template);
}

static void
test_hex_to_char (void)
{
    char buf[9];
    hex_to_char (buf, "\xFF\xFF\xFF\xFF", 4);
    g_assert_cmpstr (buf, ==, "ffffffff");

}

static void
test_get_system_uuid (void)
{
    /* DEADBEEF-1234-1234-1234-DEADBEEF1234 */
    char res[129] = {0};
    get_system_uuid (res, NULL);
    if (getuid () == 0) {
        if (*res == '\0')
            g_test_fail ();
    } else {
        g_print ("Need root to run this complete test: ");
    }
    memset (res, 0, 129);
    seteuid (1000);
    get_system_uuid (res, NULL);
    g_assert (*res == '\0');
    seteuid (0);
}

static void
test_get_crash_db_url (void)
{
    char* url = NULL;
    setenv ("CRASH_DB_URL", "http://localhost:8080", 1);
    url = get_crash_db_url ();
    if (g_strcmp0 (url, "http://localhost:8080")) {
        g_test_fail ();
    } else {
        free (url);
        url = NULL;
    }

    unsetenv ("CRASH_DB_URL");
    url = get_crash_db_url ();
    if (url != NULL) {
        g_test_fail ();
        free (url);
        url = NULL;
    }

    setenv ("CRASH_DB_URL", "http://", 1);
    url = get_crash_db_url ();
    if (url != NULL) {
        g_test_fail ();
        free (url);
    }
    setenv ("CRASH_DB_URL", "httpcolonslashslash", 1);
    url = get_crash_db_url ();
    if (url != NULL) {
        g_test_fail ();
        free (url);
    }
    unsetenv ("CRASH_DB_URL");
}

static void
test_valid_report_empty_value (void)
{
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/valid_empty_value", FALSE, NULL);
    if (report == NULL)
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProblemType"), "Crash"))
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProcMaps"), "004..."))
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProcEnviron"), ""))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

static void
test_valid_report (void)
{
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/valid", FALSE, NULL);
    const char* environ = "LANGUAGE=en_US:en\n"
                         "LC_CTYPE=en_US.UTF-8\n"
                         "LC_COLLATE=en_US.UTF-8\n"
                         "PATH=(custom, user)\n"
                         "LANG=en_US.UTF-8\n"
                         "LC_MESSAGES=en_US.UTF-8\n"
                         "SHELL=/bin/bash";
    if (report == NULL)
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProblemType"), "Crash"))
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "Architecture"), "amd64"))
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "Date"),
                     "Thu Feb  2 17:09:31 2012"))
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProcEnviron"), environ))
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "Uname"),
                     "Linux 3.2.0-12-generic x86_64"))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

static void
test_embedded_carriage_return (void)
{
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/invalid_value2", FALSE, NULL);
    const char* result = "LANGUAGE=en_US:en\n"
                         "LC_CTYPE?=en_US.UTF-8\n"
                         "LC_COLLATE=en_US.UTF-8\n"
                         "PATH=(custom, user)\n"
                         "LANG=en_US.UTF-8\n"
                         "LC_MESSAGES=en_US.UTF-8\n"
                         "SHELL=/bin/bash";
    if (report == NULL)
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProcEnviron"), result))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

static void
test_embedded_carriage_return2 (void)
{
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/invalid_key_special_chars",
                           FALSE, NULL);
    if (report == NULL)
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProblemType"),
                     "Cr?ash"))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

static void
test_no_newline (void)
{
    const char* environ = "LANGUAGE=en_US:en\n"
                         "LC_CTYPE=en_US.UTF-8\n"
                         "LC_COLLATE=en_US.UTF-8\n"
                         "PATH=(custom, user)\n"
                         "LANG=en_US.UTF-8\n"
                         "LC_MESSAGES=en_US.UTF-8\n"
                         "SHELL=/bin/bash";
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/invalid_trailing2",
                           FALSE, NULL);
    if (report == NULL)
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProcEnviron"), environ))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

static void
test_key_embedded_nul (void)
{
    /* Apport does not escape the NUL byte, but we will. */
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/invalid_key_embedded_nul",
                           TRUE, NULL);
    if (report == NULL)
        g_test_fail ();
    else if (!g_hash_table_lookup (report, "Probl?emType"))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}
static void
test_value_embedded_nul (void)
{
    /* Apport does not escape the NUL byte. */
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/invalid_value_embedded_nul",
                           FALSE, NULL);
    if (report == NULL)
        g_test_fail ();
    else if (strcmp (g_hash_table_lookup (report, "ProblemType"), "Cr?ash"))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

static void
test_drop_privileges (void)
{
    GError* err = NULL;
    if (getuid () != 0) {
        drop_privileges (&err);
        if (!err)
            g_test_fail ();
        if (strcmp (err->message, "You must be root to run this program."))
            g_test_fail ();
    } else {
        drop_privileges (&err);
        if (getuid () == 0)
            g_test_fail ();
    }
    if (err)
        g_error_free (err);
}

static void
test_report_expect_error (gconstpointer user_data)
{
    const char* const* path_and_error_msg = user_data;
    GHashTable* report = NULL;
    GError* err = NULL;

    report = parse_report (path_and_error_msg[0], FALSE, &err);
    if (report)
        g_test_fail ();
    if (!err)
        g_test_fail ();
    if (err && !g_str_has_suffix (err->message, path_and_error_msg[1]))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
    if (err)
        g_error_free (err);
}

static void
test_unicode (gconstpointer user_data)
{
    GHashTable* report = NULL;
    report = parse_report (TEST_DIR "/data/crash/unicode", FALSE, NULL);
    if (report == NULL)
        g_test_fail ();
    else if (g_strcmp0 (g_hash_table_lookup (report, "ProcCmdline"), "gedit ♥"))
        g_test_fail ();

    if (report) {
        g_hash_table_foreach (report, destroy_key_and_value, NULL);
        g_hash_table_destroy (report);
    }
}

int
main (int argc, char** argv)
{
    g_type_init ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/whoopsie/parse-report", test_parse_report);
    /* This wont work when running under fakeroot. */
    if (!getenv ("FAKEROOTKEY"))
        g_test_add_func ("/whoopsie/get-system-uuid", test_get_system_uuid);
    g_test_add_func ("/whoopsie/get-crash-db-url", test_get_crash_db_url);
    g_test_add_func ("/whoopsie/hex-to-char", test_hex_to_char);
    g_test_add_func ("/whoopsie/key-embedded-nul", test_key_embedded_nul);
    g_test_add_func ("/whoopsie/value-embedded-nul", test_value_embedded_nul);
    g_test_add_func ("/whoopsie/embedded-carriage-return",
                     test_embedded_carriage_return);
    g_test_add_func ("/whoopsie/embedded-carriage-return2",
                     test_embedded_carriage_return2);
    g_test_add_func ("/whoopsie/valid-report", test_valid_report);
    g_test_add_func ("/whoopsie/valid-report-empty-value",
                     test_valid_report_empty_value);
    g_test_add_func ("/whoopsie/no-newline", test_no_newline);
    g_test_add_func ("/whoopsie/unicode", test_unicode);

    const char* key_no_value_data[] = {
        TEST_DIR "/data/crash/invalid_key_no_value",
        "Report key must have a value." };
    const char* key_no_value_data2[] = {
        TEST_DIR "/data/crash/invalid_key_no_value2",
        "Report key must have a value." };
    const char* symlink_data[] = {
        TEST_DIR "/data/crash/invalid_symlink",
        " is a symlink or is not a regular file." };
    const char* dir_data[] = {
        TEST_DIR "/data/crash/invalid-file-is-dir",
        " is a symlink or is not a regular file." };
    const char* leading_spaces_data[] = {
        TEST_DIR "/data/crash/invalid_key_leading_spaces",
        "Report may not start with a value." };
    const char* leading_spaces_data2[] = {
        TEST_DIR "/data/crash/invalid_key_leading_spaces2",
        "Report may not start with a value." };
    const char* no_spaces_data[] = {
        TEST_DIR "/data/crash/invalid_no_spaces",
        "Report key must have a value." };
    const char* empty_line_data[] = {
        TEST_DIR "/data/crash/invalid_value",
        "Report key must have a value." };

    g_test_add_data_func ("/whoopsie/invalid_symlink",
                          symlink_data, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/invalid_directory",
                          dir_data, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/key-no-value",
                          key_no_value_data, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/key-no-value2",
                          key_no_value_data2, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/key-leading-spaces",
                          leading_spaces_data, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/key-leading-spaces2",
                          leading_spaces_data2, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/no-spaces",
                          no_spaces_data, test_report_expect_error);
    g_test_add_data_func ("/whoopsie/empty-line",
                          empty_line_data, test_report_expect_error);

    /* Run this last, so as to not mess with other tests. */
    g_test_add_func ("/whoopsie/drop-privileges", test_drop_privileges);

    /* Do not consider warnings to be fatal. */
    g_log_set_always_fatal (G_LOG_FATAL_MASK);

	return g_test_run ();
}
