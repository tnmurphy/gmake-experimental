/* database printout in json for GNU Make.
Copyright (C) 1988-2023 Free Software Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <https://www.gnu.org/licenses/>.  */
#ifndef _JPRINT_H_
#define _JPRINT_H_

struct file;
struct dep;
struct commands;

void print_escaped_string(const char *input);

void jprint_bool(const char *key, int value, int is_last);
void jprint_pointer(const char *key, const void *value, int is_last);
void jprint_unsigned_int(const char *key, unsigned int value, int is_last);
void jprint_string(const char *key, const char *value, int is_last);
void jprint_enum(const char *key, unsigned int value, int is_last);

void jprint_cmds(const char *key, struct commands *cmds, int is_last);


void jprint_file_variables (const char *key, const struct file *file, int is_last);
void jprint_target_variables (const char *key, const struct file *file, int is_last);

void jprint_command_state(const char *key, unsigned int command_state, int is_last);
void jprint_deps(const char *key, struct dep *dependencies, int is_last);
void jprint_file (const void *item, void *arg);



void jprint_variable_data_base (int is_last);
void jprint_file_data_base (int is_last);
void jprint_dir_data_base(int is_last);
void jprint_rule_data_base(int is_last);
void jprint_vpath_data_base(int is_last);
void jstrcache_print_stats(const char *);

extern FILE *json_file;

#endif /* _JPRINT_H_ */
