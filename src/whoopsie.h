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

#ifndef WHOOPSIE_H
#define WHOOPSIE_H

GHashTable* parse_report (const char* report_path, gboolean full_report, GError** error);
void hex_to_char (char* buf, const unsigned char *str, int len);
void get_system_uuid (char* res, GError** error);
char* get_crash_db_url (void);
void destroy_key_and_value (gpointer key, gpointer value, gpointer user_data);
void drop_privileges (GError** error);

#endif /* WHOOPSIE_H */
