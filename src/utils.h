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

#ifndef UTILS_H
#define UTILS_H

struct response_string {
    char* p;
    size_t length;
};

char* change_file_extension (const char* path, const char* extension);
gboolean already_handled_report (const char* crash_file);
gboolean mark_handled (const char* crash_file);
void init_response_string (struct response_string* resp);
void grow_response_string (struct response_string* resp, char* str, size_t length);
void destroy_response_string (struct response_string* resp);

#endif /* UTILS_H */
