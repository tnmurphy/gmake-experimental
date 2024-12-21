/*
database printout in json for GNU Make. Copyright (C) 1988-2023 Free
Software Foundation, Inc. This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "makeint.h"
#include <stdarg.h>
#include <stdlib.h>

#include "commands.h"
#include "dep.h"
#include "filedef.h"
#include "hash.h"
#include "rule.h"
#include "variable.h"

#include "jprint.h"

typedef struct
{
  int is_first;
  int indent;
  char *specific_target;
  FILE *json_file;
} jprint_state;

jprint_state global_jstate;
jprint_state *jstate = &global_jstate;

int
jprintf (jprint_state *jstate_, const char *fmt, ...)
{
  // initializing
  // list pointer
  va_list args;
  va_start (args, fmt);
  fprintf (jstate_->json_file, fmt, args);

  va_end (args);
  return 0;
}

int
jputc (jprint_state *jstate_, const char c)
{
  return fputc (c, jstate_->json_file);
}

void
print_escaped_string (const char *input)
{
  const char *inchar = input;
  if (!input)
    {
      return;
    }

  while (*inchar != '\0')
    {
      switch (*inchar)
        {
        case '\0':
          break;
        case '\b':
          jprintf (jstate, "\\n");
          break;
        case '\f':
          jprintf (jstate, "\\f");
          break;
        case '\n':
          jprintf (jstate, "\\n");
          break;
        case '\r':
          jprintf (jstate, "\\r");
          break;
        case '\t':
          jprintf (jstate, "\\t");
          break;
        case '\v':
          jprintf (jstate, "\\v");
          break;
        case '\\':
          jprintf (jstate, "\\\\");
          break;
        case '/':
          jprintf (jstate, "\\/");
          break;
        case '"':
          jprintf (jstate, "\\\"");
          break;
        default:
          if ((*inchar >= '\x01' && *inchar <= '\x1f') || *inchar < 0)
            {
              jprintf (jstate, "\\u00%2x", (unsigned char)*inchar);
            }
          else
            {
              jputc (jstate, *inchar);
            }
        }
      inchar++;
    }
}

void
jprint_bool (const char *key, int value, int is_last)
{
  jprintf (jstate,
           "  \"%s\": %s%s\n",
           key, value ? "true" : "false", is_last ? "" : ",");
}

void
jprint_pointer (const char *key, const void *value, int is_last)
{
  if (value)
    {
      jprintf (jstate,
               "  \"%s\": %p%s\n",
               key, value, is_last ? "" : ",");
    }
}

void
jprint_unsigned_int (const char *key, unsigned int value, int is_last)
{
  jprintf (jstate,
           "  \"%s\": %u%s\n",
           key, value, is_last ? "" : ",");
}

void
jprint_string (const char *key, const char *value, int is_last)
{
  jprintf (jstate,
           "  \"%s\": \"",
           key);
  print_escaped_string (value);
  jprintf (jstate, "\"%s\n", is_last ? "" : ",");
}

void
jprint_enum (const char *key, unsigned int value, int is_last)
{
  jprintf (jstate,
           "  \"%s\": %u%s\n",
           key, value, is_last ? "" : ",");
}

void
hash_jprint_stats (const char *key, struct hash_table *ht, int is_last)
{
  jprintf (jstate, "\"%s\": {\n", key);
  jprintf (jstate,
           "  \"load\": \"%lu/%lu=%.0f%%\",\n",
           ht->ht_fill, ht->ht_size,
           100.0 * (double)ht->ht_fill / (double)ht->ht_size);
  jprintf (jstate,
           "  \"rehash\": %u,\n",
           ht->ht_rehashes);
  jprintf (jstate,
           "  \"collisions\": \"%lu/%lu=%.0f%%\"\n",
           ht->ht_collisions, ht->ht_lookups,
           (ht->ht_lookups
                ? (100.0 * (double)ht->ht_collisions / (double)ht->ht_lookups)
                : 0));
  jprintf (jstate, "}%s\n", is_last ? "" : ",");
}

/* ============================
 * JSON PRINT
 * =================================================================
 */

/* Print information
 * for variable V,
 * prefixing it with
 * PREFIX.  */

static void
jprint_variable (const void *item, void *arg)
{
  const struct variable *v = item;
  const char *origin;
  jprint_state *state = (jprint_state *)arg;

  switch (v->origin)
    {
    case o_automatic:
      origin = _ ("automatic");
      break;
    case o_default:
      origin = _ ("default");
      break;
    case o_env:
      origin = _ ("environment");
      break;
    case o_file:
      origin = _ ("makefile");
      break;
    case o_env_override:
      origin = _ ("environment under -e");
      break;
    case o_command:
      origin = _ ("command line");
      break;
    case o_override:
      origin = _ ("'override' directive");
      break;
    case o_invalid:
      abort ();
    }

  if (state)
    {
      /* is first
       * variable in
       * a sequence
       * so don't
       * print a
       * preceeding
       * comma */
      if (state->is_first)
        {
          state->is_first = 0;
        }
      else
        {
          jprintf (jstate, ",\n");
        }
    }
  jprintf (jstate,
           "\"%s\" : {\n",
           v->name);
  jprintf (jstate,
           "  \"origin\": \"%s\",\n",
           origin);
  jprintf (jstate,
           "  \"private\": %s,\n",
           v->private_var ? "true"
                          : "false");
  if (v->fileinfo.filenm)
    jprintf (jstate,
             "  \"source\": \"%s\",\n  \"line\": %lu,\n",
             v->fileinfo.filenm, v->fileinfo.lineno + v->fileinfo.offset);

  /* Is this a
   * 'define'?  */
  if (v->recursive && strchr (v->value, '\n') != 0)
    {
      jprintf (jstate, "  \"define\": \"");
      print_escaped_string (v->value);
      jprintf (jstate, "\"\n");
    }
  else
    {
      jprintf (jstate,
               "  \"%s%s\": \"",
               v->append ? "append"
                         : "assign",
               v->recursive ? "-recursive"
                            : "");
      print_escaped_string (v->value);
      jprintf (jstate, "\"");
    }
  jprintf (jstate, "\n}");
}

static void
jprint_auto_variable (const void *item, void *arg)
{
  const struct variable *v = item;

  if (v->origin == o_automatic)
    jprint_variable (item, arg);
}

static void
jprint_noauto_variable (const void *item, void *arg)
{
  const struct variable *v = item;

  if (v->origin != o_automatic)
    jprint_variable (item, arg);
}

/* Print all the
   variables in SET.
   PREFIX is printed
   before the actual
   variable
   definitions
   (everything else
   is comments).  */

void
jprint_variable_set (const char *key, struct variable_set *set, int pauto,
                     int is_last)
{
  jprint_state vstate;
  vstate.is_first = 1;
  vstate.indent = global_jprint_state.indent;

  if (!set)
    {
      return;
    }
  jprintf (jstate,
           "  \"%s\": {\n",
           key);
  hash_map_arg (&set->table, (pauto ? jprint_auto_variable : jprint_variable),
                (void *)&vstate);
  /* hash_jprint_stats
   * ("hash-table-stats",
   * &set->table,
   * 1); */
  jprintf (jstate, "}%s\n", is_last ? "" : ",");
}

/* Print the data
 * base of
 * variables.  */

void
jprint_variable_data_base (int is_last)
{
  jprintf (jstate, "\"variables\": {\n");

  jprint_variable_set ("global", &global_variable_set, 0, 0);

  jprintf (jstate, "\"pattern-specific-variables\" : {\n");

  {
    struct pattern_var *p;
    unsigned int rules = 0;
    variable_print_state vstate;
    vstate.is_first = 1;
    vstate.indent = 2;

    for (p = pattern_vars; p != 0; p = p->next)
      {
        ++rules;
        jprintf (jstate,
                 "\n\"%s\" :\n",
                 p->target);
        jprint_variable (&p->variable, (void *)&vstate);
      }

    jprintf (jstate, "\n},\n");

    jprintf (jstate,
             "  \"pattern-specific-rule-count\": %u\n",
             rules);
    jprintf (jstate, "}%s", is_last ? "" : ",");
  }
}

/* Print all the
 * local variables
 * of FILE.  */

void
jprint_file_variables (const char *key, const struct file *file, int is_last)
{
  if (file->variables != 0)
    jprint_variable_set (key, file->variables->set, 1, is_last);
}

void
jprint_target_variables (const char *key, const struct file *file, int is_last)
{
  jprintf (jstate,
           "  \"%s\": {\n",
           key);
  if (file->variables != 0)
    {
      size_t l = strlen (file->name);
      char *t = alloca (l + 3);

      memcpy (t, file->name, l);
      t[l] = ':';
      t[l + 1] = ' ';
      t[l + 2] = '\0';

      hash_map_arg (&file->variables->set->table, jprint_noauto_variable, t);
    }
  jprintf (jstate, "  }%s\n", is_last ? "" : ",");
}

void
jprint_command_state (const char *key, unsigned int command_state, int is_last)
{
  jprintf (jstate, "  \"%s\": ", key);
  switch (command_state)
    {
    case cs_running:
      jprintf (jstate, "\"cs_running\"");
      break;
    case cs_deps_running:
      jprintf (jstate, "\"cs_deps_running\"");
      break;
    case cs_not_started:
      jprintf (jstate, "\"cs_not_started\"");
      break;
    case cs_finished:
      jprintf (jstate, "\"cs_not_finished\"");
      /*
      switch
      (f->update_status)
        {
        case us_none:
          break;
        case us_success:
          puts (_("# Successfully updated."));
          break;
        case us_question:
          assert(question_flag);
          puts (_("# Needs to be updated (-q is set)."));
          break;
        case
      us_failed:
          puts (_("# Failed to be updated."));
          break;
        }*/
      break;
    default:
      puts (_ ("#  Invalid value in 'command_state' member!"));
      fflush (stdout);
      fflush (stderr);
      abort ();
    }
  jprintf (jstate, "%s\n", is_last ? "" : ",");
}

void
jprint_deps (const char *key, struct dep *dependencies, int is_last)
{
  jprintf (jstate, "  \"%s\": ", key);
  if (dependencies)
    {
      const struct dep *d;
      jprintf (jstate, "[\n");
      for (d = dependencies; d != 0; d = d->next)
        {
          jprintf (jstate, "     \"%s\"%s\n",
                   dep_name (d), !d->next ? "" : ",");
        }
      jprintf (jstate, "]%s\n", is_last ? "" : ",");
    }
  else
    {
      jprintf (jstate, "  []%s\n", is_last ? "" : ",");
    }
}

void
jprint_cmds (const char *key, struct commands *cmds, int is_last)
{

  if (!cmds)
    {
      return;
    }

  jprintf (jstate,
           "\"%s\" : {\n\"source\": ", key);

  if (cmds->fileinfo.filenm == 0)
    jprintf (jstate, "\"builtin\", ");
  else
    jprintf (jstate,
             "\"%s\",\n \"line\": %lu,\n",
             cmds->fileinfo.filenm, cmds->fileinfo.lineno);

  jprintf (jstate, "\"commands\": \"");
  print_escaped_string (cmds->commands);
  jprintf (jstate, "\"\n}%s\n", is_last ? "" : ",");
}

void
jprint_file (const void *item, void *arg)
{
  const struct file *f = item;
  file_print_state *state = (file_print_state *)arg;

  if (no_builtin_rules_flag && f->builtin)
    return;

  if (state)
    {
      /* is first
       * variable in
       * a sequence
       * so don't
       * print a
       * preceeding
       * comma */
      if (state->is_first)
        {
          state->is_first = 0;
        }
      else
        {
          jprintf (jstate, ",\n");
        }
    }

  jprintf (jstate,
           "\"%s\" : {\n",
           f->name);
  jprint_string ("hname", f->hname, 0);
  jprint_string ("vpath", f->vpath, 0);
  jprint_deps ("deps", f->deps, 0);
  jprint_cmds ("cmds", f->cmds, 0);

  jprint_string ("stem", f->stem, 0);
  jprint_deps ("also_make", f->also_make, 0);

  /* print_pointer("prev",
  (const void
  *)f->prev, 0);
  print_pointer("last",
  (const void
  *)f->last, 0);
   */

  if (f->renamed)
    {
      jprint_string ("renamed", f->renamed->name, 0);
    }
  jprint_file_variables ("variables", f, 0);
  jprint_target_variables ("target-variables",
                           f, 0);
  if (f->pat_variables)
    {
      jprint_variable_set ("pattern_specific_variables",
                           f->pat_variables->set, 0, 0);
    }
  if (f->parent)
    {
      jprint_string ("parent", f->parent->name, 0);
    }
  jprint_pointer ("double_colon", (const void *)f->double_colon, 0);
  jprint_unsigned_int ("last_mtime", f->last_mtime, 0);
  jprint_unsigned_int ("mtime_before_update", f->mtime_before_update, 0);
  jprint_unsigned_int ("considered", f->considered, 0);
  jprintf (jstate, "  \"command_flags\": %d,\n", f->command_flags);
  jprint_enum ("update_status", f->update_status, 0);
  jprint_command_state ("command_state", f->command_state, 0);
  jprint_bool ("builtin", f->builtin, 0);
  jprint_bool ("precious", f->precious, 0);
  jprint_bool ("loaded", f->loaded, 0);
  jprint_bool ("unloaded", f->unloaded, 0);
  jprint_bool ("low_resolution_time", f->low_resolution_time, 0);
  jprint_bool ("tried_implicit", f->tried_implicit, 0);
  jprint_bool ("updating", f->updating, 0);
  jprint_bool ("updated", f->updated, 0);
  jprint_bool ("is_target", f->is_target, 0);
  jprint_bool ("cmd_target", f->cmd_target, 0);
  jprint_bool ("phony", f->phony, 0);
  jprint_bool ("intermediate", f->intermediate, 0);
  jprint_bool ("is_explicit", f->is_explicit, 0);
  jprint_bool ("secondary", f->secondary, 0);
  jprint_bool ("notintermediate", f->notintermediate, 0);
  jprint_bool ("dontcare", f->dontcare, 0);
  jprint_bool ("ignore_vpath", f->ignore_vpath, 0);
  jprint_bool ("pat_searched", f->pat_searched, 0);
  jprint_bool ("no_diag", f->no_diag, 0);
  jprint_bool ("was_shuffled", f->was_shuffled, 0);
  jprint_bool ("snapped", f->snapped, 1);
  jprintf (jstate, "}\n");
}

void
jprint_file_data_base (int is_last)
{
  file_print_state fstate;
  fstate.is_first = 1;
  fstate.indent = 2;

  jprintf (jstate, "\n\"files\": {\n");

  hash_map_arg (get_files (), jprint_file, (void *)&fstate);

  jprintf (jstate, "\n}%s\n", is_last ? "" : ",");
  /* hash_jprint_stats("hash-table-stats", * get_files(), * 0); */
}

void
jprint_dir_data_base (int is_last)
{
  /* not implemented
   * yet */
  /* jprintf(jstate,
   * "\n\"dirs\":
   * []%s\n",
   * is_last ? "" :
   * ","); */

  void print_dir_data_base (void)
  {
    unsigned int files;
    unsigned int impossible;
    struct directory **dir_slot;
    struct directory **dir_end;
#if MK_OS_W32
    char buf[INTSTR_LENGTH + 1];
#endif

    jprintf (jstate, "\n\"directories\" : [\n");

    files = impossible = 0;

    dir_slot = (struct directory **)directories.ht_vec;
    dir_end = dir_slot + directories.ht_size;
    for (; dir_slot < dir_end; dir_slot++)
      {
        struct directory *dir = *dir_slot;
        if (!HASH_VACANT (dir))
          {
            if (dir->contents == NULL)
              printf (_ ("# %s: could not be stat'd.\n"), dir->name);
            else if (dir->contents->dirfiles.ht_vec == NULL)
#if MK_OS_W32
              printf (
                  _ ("# %s (key %s, mtime %s): could not be opened.\n"),
                  dir->name, dir->contents->path_key,
                  make_ulltoa ((unsigned long long)dir->contents->mtime, buf));
#elif defined(VMS_INO_T)
              printf (_ ("# %s (device %d, inode [%d,%d,%d]): could not be opened.\n"),
                      dir->name, dir->contents->dev, dir->contents->ino[0],
                      dir->contents->ino[1], dir->contents->ino[2]);
#else
              printf (
                  _ ("# %s (device %ld, inode %ld): could not be opened.\n"),
                  dir->name, (long)dir->contents->dev,
                  (long)dir->contents->ino);
#endif
            else
              {
                unsigned int f = 0;
                unsigned int im = 0;
                struct dirfile **files_slot;
                struct dirfile **files_end;

                files_slot = (struct dirfile **)dir->contents->dirfiles.ht_vec;
                files_end = files_slot + dir->contents->dirfiles.ht_size;
                for (; files_slot < files_end; files_slot++)
                  {
                    struct dirfile *df = *files_slot;
                    if (!HASH_VACANT (df))
                      {
                        if (df->impossible)
                          ++im;
                        else
                          ++f;
                      }
                  }
#if MK_OS_W32
                printf (_ ("# %s (key %s, mtime %s): "), dir->name,
                        dir->contents->path_key,
                        make_ulltoa ((unsigned long long)dir->contents->mtime,
                                     buf));
#elif defined(VMS_INO_T)
                printf (_ ("# %s (device %d, inode [%d,%d,%d]): "), dir->name,
                        dir->contents->dev, dir->contents->ino[0],
                        dir->contents->ino[1], dir->contents->ino[2]);
#else
                printf (_ ("# %s (device %ld, inode %ld): "), dir->name,
                        (long)dir->contents->dev, (long)dir->contents->ino);
#endif
                if (f == 0)
                  fputs (_ ("No"), stdout);
                else
                  printf ("%u", f);
                fputs (_ (" files, "), stdout);
                if (im == 0)
                  fputs (_ ("no"), stdout);
                else
                  printf ("%u", im);
                fputs (_ (" impossibilities"), stdout);
                if (dir->contents->dirstream == NULL)
                  puts (".");
                else
                  puts (_ (" so far."));
                files += f;
                impossible += im;
              }
          }
      }

    fputs ("\n# ", stdout);
    if (files == 0)
      fputs (_ ("No"), stdout);
    else
      printf ("%u", files);
    fputs (_ (" files, "),
           stdout);
    if (impossible == 0)
      fputs (_ ("no"), stdout);
    else
      printf ("%u", impossible);
    printf (_ (" impossibilities in %lu directories.\n"),
            directories.ht_fill);
  }

  void jprint_rule (struct rule * r)
  {
    jprintf (jstate, "    { \n");
    if (r->_defn == NULL)
      {
        unsigned int k;
        const struct dep *dep, *ood = 0;
        int is_first_dep = 1;

        jprintf (jstate, "    \"targets\" : [\n");
        for (k = 0; k < r->num; ++k)
          {
            jprintf (jstate,
                     "%s      \"%s\"",
                     k == 0 ? "" : ",\n", r->targets[k]);
          }
        jprintf (jstate, "\n    ],\n");

        if (r->terminal)
          jprintf (jstate, "      \"terminal\" : true, \n");

        /* print all
         * normal
         * dependencies;
         * find
         * first
         * order-only
         * dep.  */
        jprintf (jstate, "      \"deps\" : [\n");

        for (dep = r->deps; dep; dep = dep->next)
          {
            if (dep->ignore_mtime == 0)
              { /* not
                   an order only dependency */

                if (!is_first_dep)
                  {
                    jprintf (jstate, ",\n");
                  }
                else
                  {
                    is_first_dep = 0;
                  }
                if (dep->wait_here)
                  {
                    jprintf (jstate, "        \".WAIT\"");
                  }
                else
                  {
                    jprintf (jstate, "        \"%s\"", dep_name (dep));
                  }
              }
            else if (ood == 0)
              {
                ood = dep; /* find the first OOD so we can process them next */
              }
          }
        jprintf (jstate, "\n       ],\n");

        jprintf (jstate, "\n      \"ood-deps\" : [\n");
        /* print
         * order-only
         * deps, if
         * we have
         * any.  */
        is_first_dep = 1;
        for (; ood; ood = ood->next)
          {
            if (ood->ignore_mtime)
              {
                if (!is_first_dep)
                  {
                    jprintf (jstate, ",\n");
                  }
                else
                  {
                    is_first_dep = 0;
                  }
                if (ood->wait_here)
                  {
                    jprintf (jstate, "        \".WAIT\"");
                  }
                else
                  {
                    jprintf (jstate, "        \"%s\"", dep_name (ood));
                  }
              }
          }
        jprintf (jstate, "      ]");
      }

    if (r->cmds != 0)
      {
        jprintf (jstate, ",\n");
        jprint_cmds ("cmds", r->cmds, 1);
      }
    else
      {
        jprintf (jstate, "\n");
      }
    jprintf (jstate, "    } \n");
  }

  void jprint_rule_data_base (int is_last)
  {
    unsigned int rules, terminal;
    struct rule *r;
    /* unsigned int
     * num_pattern_rules
     * =
     * get_num_pattern_rules();
     */

    jprintf (jstate, "\n\"rules\": {");
    jprintf (jstate, "\n  \"implicit-rules\": [\n");

    rules = terminal = 0;
    for (r = pattern_rules; r != 0; r = r->next)
      {
        if (rules != 0)
          {
            jprintf (jstate, ",\n");
          }
        ++rules;

        jprint_rule (r);

        if (r->terminal)
          ++terminal;
      }

    jprintf (jstate,
             "\n],\n \"terminal-rules-count\" : %u\n",
             terminal);
    jprintf (jstate, "}%s\n", is_last ? "" : ",");

    if (num_pattern_rules != rules)
      {
        /* 
	 This can happen if a fatal error was detected while reading the
	 makefiles and thus count_implicit_rule_limits wasn't called
	 yet.
	*/
        if (num_pattern_rules != 0)
          ONN (fatal, NILF,
               "INTERNAL: num_pattern_rules is wrong!  %u != %u",
               num_pattern_rules, rules);
      }
  }

  void jprint_vpath_data_base (int is_last)
  {
    /* not  implemented  yet */
    jprintf (jstate,
             "\n\"vpath\": []%s\n",
             is_last ? "" : ",");
  }

  void jstrcache_print_stats (const char *p)
  {
    /* not implemented yet */
    jprintf (jstate, "%s",
             p ? "" : ""); /* prevent unused parameter wanrnings */
  }

  /* EOF */
