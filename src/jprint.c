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
				  
#include <stdlib.h>
#include "makeint.h"

#include "filedef.h"
#include "dep.h"
#include "commands.h"
#include "variable.h"
#include "hash.h"

#include "jprint.h"

FILE *json_file;

typedef struct {
	int is_first;
	int indent;
	char *specific_target;
} variable_print_state;

typedef struct {
	int is_first;
	int indent;
} file_print_state;


void print_escaped_string(const char *input)
{
    const char *inchar = input;
    if (!input) {
        return;
    }
        
    while (*inchar != '\0')
    {
      switch (*inchar) {
        case '\0':
          break;               
        case '\b':
          fprintf (json_file, "\\n");
          break; 
        case '\f':
          fprintf (json_file, "\\f");
          break; 
        case '\n':
          fprintf (json_file, "\\n");
          break; 
        case '\r':
          fprintf (json_file, "\\r");
          break;
        case '\t':
          fprintf (json_file, "\\t");
          break; 
        case '\v': 
          fprintf (json_file, "\\v");
          break;
        case '\\':
          fprintf (json_file, "\\\\");
          break;
        case '/':
          fprintf (json_file, "\\/");
          break; 
        case '"': 
          fprintf (json_file, "\\\"");
          break;
        default:
          if ((*inchar >= '\x01' && *inchar <= '\x1f') || *inchar < 0) {
              fprintf (json_file, "\\u00%2x", (unsigned char)*inchar);
          } else {
            fputc(*inchar, json_file);
          }
      }
      inchar++;
    } 
}


void jprint_bool(const char *key, int value, int is_last) {
    fprintf (json_file, "  \"%s\": %s%s\n", key, value ? "true" : "false", is_last ? "" : ",");
}


void jprint_pointer(const char *key, const void *value, int is_last) {
    if (value) {
      fprintf (json_file, "  \"%s\": %p%s\n", key, value, is_last ? "" : ",");
    }
}


void jprint_unsigned_int(const char *key, unsigned int value, int is_last) {
    fprintf (json_file, "  \"%s\": %u%s\n", key, value, is_last ? "" : ",");
}


void jprint_string(const char *key, const char *value, int is_last) {
    fprintf (json_file, "  \"%s\": \"", key);
    print_escaped_string(value);
    fprintf (json_file, "\"%s\n", is_last ? "" : ",");
}


void jprint_enum(const char *key, unsigned int value, int is_last) {
    fprintf (json_file, "  \"%s\": %u%s\n", key, value, is_last ? "" : ",");
}


void
hash_jprint_stats (const char *key, struct hash_table *ht, int is_last)
{
  fprintf (json_file, "\"%s\": {\n", key);
  fprintf (json_file, "  \"load\": \"%lu/%lu=%.0f%%\",\n", ht->ht_fill, ht->ht_size,
           100.0 * (double) ht->ht_fill / (double) ht->ht_size);
  fprintf (json_file, "  \"rehash\": %u,\n", ht->ht_rehashes);
  fprintf (json_file, "  \"collisions\": \"%lu/%lu=%.0f%%\"\n", ht->ht_collisions, ht->ht_lookups,
           (ht->ht_lookups
            ? (100.0 * (double) ht->ht_collisions / (double) ht->ht_lookups)
            : 0));
  fprintf (json_file, "}%s\n", is_last ? "" : ",");
}

/* ============================ JSON PRINT ================================================================= */


/* Print information for variable V, prefixing it with PREFIX.  */

static void jprint_variable (const void *item, void *arg)
{
  const struct variable *v = item;
  const char *origin;
  variable_print_state *state = (variable_print_state *)arg;

  switch (v->origin)
    {
    case o_automatic:
      origin = _("automatic");
      break;
    case o_default:
      origin = _("default");
      break;
    case o_env:
      origin = _("environment");
      break;
    case o_file:
      origin = _("makefile");
      break;
    case o_env_override:
      origin = _("environment under -e");
      break;
    case o_command:
      origin = _("command line");
      break;
    case o_override:
      origin = _("'override' directive");
      break;
    case o_invalid:
      abort ();
    }
    
  if (state) {
     /* is first variable in a sequence so don't print a preceeding comma */
    if (state->is_first) {
      state->is_first = 0;
    } else {
      fprintf (json_file, ",\n");
    }
  }
  fprintf (json_file, "\"%s\" : {\n", v->name);
  fprintf (json_file, "  \"origin\": \"%s\",\n", origin);
  fprintf (json_file, "  \"private\": %s,\n", v->private_var ? "true" : "false");
  if (v->fileinfo.filenm)
    fprintf (json_file, "  \"source\": \"%s\",\n  \"line\": %lu,\n",
            v->fileinfo.filenm, v->fileinfo.lineno + v->fileinfo.offset);

    /* Is this a 'define'?  */
  if (v->recursive && strchr (v->value, '\n') != 0)
    fprintf (json_file, "  \"define\": \"%s\",\n", v->value);
  else
    {
      fprintf (json_file, "  \"%s%s\": \"",      
        v->append ? "append" : "assign", 
        v->recursive ? "-recursive" : "");
      print_escaped_string(v->value);
      fprintf (json_file, "\"");
    }
     fprintf (json_file, "\n}");
}


static void jprint_auto_variable (const void *item, void *arg)
{
  const struct variable *v = item;

  if (v->origin == o_automatic)
    jprint_variable (item, arg);
}


static void jprint_noauto_variable (const void *item, void *arg)
{
  const struct variable *v = item;

  if (v->origin != o_automatic)
    jprint_variable (item, arg);
}


/* Print all the variables in SET.  PREFIX is printed before
   the actual variable definitions (everything else is comments).  */

void jprint_variable_set (const char *key, struct variable_set *set, int pauto, int is_last)
{
  variable_print_state vstate;
  vstate.is_first = 1;
  vstate.indent=2;

  if (!set) {
      return;
  }
  fprintf (json_file, "  \"%s\": {\n", key);
  hash_map_arg (&set->table, (pauto ? jprint_auto_variable : jprint_variable),
                (void *)&vstate);
  /* hash_jprint_stats ("hash-table-stats", &set->table, 1); */
  fprintf (json_file, "}%s\n", is_last ? "" : ",");
}

/* Print the data base of variables.  */

void jprint_variable_data_base (int is_last)
{
  fprintf (json_file, "\"variables\": {\n");

  jprint_variable_set ("global", &global_variable_set, 0, 0);
  

  fprintf (json_file, "\"pattern-specific-variables\" : {\n");

  {
    struct pattern_var *p;
    unsigned int rules = 0;
    variable_print_state vstate;
    vstate.is_first = 1;
    vstate.indent=2;

    for (p = pattern_vars; p != 0; p = p->next)
      {
        ++rules;
        fprintf (json_file, "\n\"%s\" :\n", p->target);
        jprint_variable (&p->variable, (void *)&vstate);
      }

    fprintf (json_file, "\n},\n");
    
    fprintf (json_file, "  \"pattern-specific-rule-count\": %u\n", rules);
    fprintf (json_file, "}%s", is_last ? "" : ",");
  }

}

/* Print all the local variables of FILE.  */

void jprint_file_variables (const char *key, const struct file *file, int is_last)
{
  if (file->variables != 0)
    jprint_variable_set (key, file->variables->set, 1, is_last);
}

void jprint_target_variables (const char *key, const struct file *file, int is_last)
{
  fprintf (json_file, "  \"%s\": {\n", key);
  if (file->variables != 0)
    {
      size_t l = strlen (file->name);
      char *t = alloca (l + 3);

      memcpy (t, file->name, l);
      t[l] = ':';
      t[l+1] = ' ';
      t[l+2] = '\0';

      hash_map_arg (&file->variables->set->table, jprint_noauto_variable, t);
    }
 fprintf (json_file, "  }%s\n", is_last ? "" : ",");
}


void jprint_command_state(const char *key, unsigned int command_state, int is_last) {
fprintf (json_file, "  \"%s\": ", key);
switch (command_state)
    {
    case cs_running:
      fprintf (json_file, "\"cs_running\"");
      break;
    case cs_deps_running:
      fprintf (json_file, "\"cs_deps_running\"");
      break;
    case cs_not_started:
      fprintf (json_file, "\"cs_not_started\"");
      break;    
    case cs_finished:
      fprintf (json_file, "\"cs_not_finished\"");
      /*
      switch (f->update_status)
        {
        case us_none:
          break;
        case us_success:
          puts (_("#  Successfully updated."));
          break;
        case us_question:
          assert (question_flag);
          puts (_("#  Needs to be updated (-q is set)."));
          break;
        case us_failed:
          puts (_("#  Failed to be updated."));
          break;
        }*/
      break;
    default:
      puts (_("#  Invalid value in 'command_state' member!"));
      fflush (stdout);
      fflush (stderr);
      abort ();
    }
    fprintf (json_file, "%s\n", is_last ? "" : ",");
}

void jprint_deps(const char *key, struct dep *dependencies, int is_last) {
    fprintf (json_file, "  \"%s\": ", key);
    if (dependencies)
    {
      const struct dep *d;
      fprintf (json_file, "[\n");
      for (d = dependencies; d != 0; d = d->next) {
            fprintf (json_file, "     \"%s\"%s\n", dep_name (d), !d->next ? "" : ",");
            
      }
      fprintf (json_file, "]%s\n", is_last ? "" : ",");
    } else {
      fprintf (json_file, "  []%s\n", is_last ? "" : ",");
    }
}


void jprint_cmds(const char *key, struct commands *cmds, int is_last)
{

  if (!cmds) {
      return;
  }

  fprintf (json_file, "\"%s\" : {\n\"source\": ", key);

  if (cmds->fileinfo.filenm == 0)
    fprintf (json_file, "\"builtin\", ");
  else
    fprintf (json_file, "\"%s\",\n \"line\": %lu,\n",
            cmds->fileinfo.filenm, cmds->fileinfo.lineno);

  fprintf (json_file, "\"commands\": \"");
  print_escaped_string(cmds->commands);
  fprintf (json_file, "\"\n}%s\n", is_last ? "" : ",");
}


void jprint_file (const void *item, void *arg)
{
  const struct file *f = item;
  file_print_state *state = (file_print_state *)arg;
    
  if (no_builtin_rules_flag && f->builtin)
    return;

  if (state) {
    /* is first variable in a sequence so don't print a preceeding comma */
    if (state->is_first) {
      state->is_first = 0;
    } else {
      fprintf (json_file, ",\n");
    }
  }

  fprintf (json_file, "\"%s\" : {\n",f->name);
  jprint_string("hname", f->hname, 0);
  jprint_string("vpath", f->vpath, 0);
  jprint_deps("deps", f->deps, 0);
  jprint_cmds("cmds", f->cmds, 0);
  
  jprint_string("stem", f->stem, 0);
  jprint_deps("also_make", f->also_make, 0);
  
  /* print_pointer("prev", (const void *)f->prev, 0);
  print_pointer("last", (const void *)f->last, 0);
   */
   
  if (f->renamed) {
    jprint_string("renamed", f->renamed->name, 0);
  }
  jprint_file_variables("variables", f, 0);
  jprint_target_variables("target-variables", f, 0);
  if (f->pat_variables) {
    jprint_variable_set("pattern_specific_variables", f->pat_variables->set, 0, 0);
  }
  if (f->parent) {
    jprint_string("parent", f->parent->name, 0);
  }
  jprint_pointer("double_colon", (const void *)f->double_colon, 0);
  jprint_unsigned_int("last_mtime", f->last_mtime, 0);
  jprint_unsigned_int("mtime_before_update", f->mtime_before_update, 0);
  jprint_unsigned_int("considered", f->considered, 0);
  fprintf (json_file, "  \"command_flags\": %d,\n", f->command_flags);
  jprint_enum("update_status", f->update_status, 0);
  jprint_command_state("command_state", f->command_state, 0);
  jprint_bool("builtin", f->builtin, 0);
  jprint_bool("precious", f->precious, 0);
  jprint_bool("loaded", f->loaded, 0);
  jprint_bool("unloaded", f->unloaded, 0);
  jprint_bool("low_resolution_time", f->low_resolution_time, 0);
  jprint_bool("tried_implicit", f->tried_implicit, 0);
  jprint_bool("updating", f->updating, 0);
  jprint_bool("updated", f->updated, 0);
  jprint_bool("is_target", f->is_target, 0);
  jprint_bool("cmd_target", f->cmd_target, 0);
  jprint_bool("phony", f->phony, 0);
  jprint_bool("intermediate", f->intermediate, 0);
  jprint_bool("is_explicit", f->is_explicit, 0);
  jprint_bool("secondary", f->secondary, 0);
  jprint_bool("notintermediate", f->notintermediate, 0);
  jprint_bool("dontcare", f->dontcare, 0);
  jprint_bool("ignore_vpath", f->ignore_vpath, 0);
  jprint_bool("pat_searched", f->pat_searched, 0);
  jprint_bool("no_diag", f->no_diag, 0);
  jprint_bool("was_shuffled", f->was_shuffled, 0);
  jprint_bool("snapped", f->snapped, 1);
  fprintf (json_file, "}\n");
}


void jprint_file_data_base (int is_last)
{
  file_print_state fstate;
  fstate.is_first = 1;
  fstate.indent = 2;


  fprintf (json_file, "\n\"files\": {\n");

  hash_map_arg (get_files(), jprint_file, (void *)&fstate);

  fprintf (json_file, "\n}%s\n", is_last ? "" : ",");
  /* hash_jprint_stats ("hash-table-stats", get_files(), 0); */
}

void jprint_dir_data_base(int is_last)
{
  /* not implemented yet */
  fprintf (json_file, "\n\"dirs\": []%s\n", is_last ? "" : ",");
}

void jprint_rule_data_base(int is_last)
{
  /* not implemented yet */
  fprintf (json_file, "\n\"rules\": []%s\n", is_last ? "" : ",");
}

void jprint_vpath_data_base(int is_last)
{
  /* not implemented yet */
  fprintf (json_file, "\n\"vpath\": []%s\n", is_last ? "" : ",");
}

void jstrcache_print_stats(const char *p)
{
  /* not implemented yet */
  fprintf(json_file, "%s", p ? "" : ""); /* prevent unused parameter wanrnings */
}

/* EOF */
