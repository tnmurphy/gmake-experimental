/* Internals of variables for GNU Make.
Copyright (C) 1988-2024 Free Software Foundation, Inc.
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

#include "makeint.h"

#include <assert.h>

#include "filedef.h"
#include "debug.h"
#include "dep.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "os.h"
#include "rule.h"
#if MK_OS_W32
#include "pathstuff.h"
#endif
#include "hash.h"
#include "warning.h"

/* Incremented every time we enter target_environment().  */
unsigned long long env_recursion = 0;

/* Incremented every time we add or remove a global variable.  */
static unsigned long variable_changenum = 0;

/* Chain of all pattern-specific variables.  */

struct pattern_var *pattern_vars = NULL;

/* Pointer to the last struct in the pack of a specific size, from 1 to 255.*/

static struct pattern_var *last_pattern_vars[256];

/* Create a new pattern-specific variable struct. The new variable is
   inserted into the PATTERN_VARS list in the shortest patterns first
   order to support the shortest stem matching (the variables are
   matched in the reverse order so the ones with the longest pattern
   will be considered first). Variables with the same pattern length
   are inserted in the definition order. */

struct pattern_var *
create_pattern_var (const char *target, const char *suffix)
{
  size_t len = strlen (target);
  struct pattern_var *p = xcalloc (sizeof (struct pattern_var));

  if (pattern_vars != 0)
    {
      if (len < 256 && last_pattern_vars[len] != 0)
        {
          p->next = last_pattern_vars[len]->next;
          last_pattern_vars[len]->next = p;
        }
      else
        {
          /* Find the position where we can insert this variable. */
          struct pattern_var **v;

          for (v = &pattern_vars; ; v = &(*v)->next)
            {
              /* Insert at the end of the pack so that patterns with the
                 same length appear in the order they were defined .*/

              if (*v == 0 || (*v)->len > len)
                {
                  p->next = *v;
                  *v = p;
                  break;
                }
            }
        }
    }
  else
    {
      pattern_vars = p;
      p->next = 0;
    }

  p->target = target;
  p->len = len;
  p->suffix = suffix + 1;

  if (len < 256)
    last_pattern_vars[len] = p;

  return p;
}

/* Look up a target in the pattern-specific variable list.  */

static struct pattern_var *
lookup_pattern_var (struct pattern_var *start, const char *target,
                    size_t targlen)
{
  struct pattern_var *p;

  for (p = start ? start->next : pattern_vars; p != 0; p = p->next)
    {
      const char *stem;
      size_t stemlen;

      if (p->len > targlen)
        /* It can't possibly match.  */
        continue;

      /* From the lengths of the filename and the pattern parts,
         find the stem: the part of the filename that matches the %.  */
      stem = target + (p->suffix - p->target - 1);
      stemlen = targlen - p->len + 1;

      /* Compare the text in the pattern before the stem, if any.  */
      if (stem > target && !strneq (p->target, target, stem - target))
        continue;

      /* Compare the text in the pattern after the stem, if any.
         We could test simply using streq, but this way we compare the
         first two characters immediately.  This saves time in the very
         common case where the first character matches because it is a
         period.  */
      if (*p->suffix == stem[stemlen]
          && (*p->suffix == '\0' || streq (&p->suffix[1], &stem[stemlen+1])))
        break;
    }

  return p;
}

/* Hash table of all global variable definitions.  */

static unsigned long
variable_hash_1 (const void *keyv)
{
  struct variable const *key = (struct variable const *) keyv;
  return_STRING_N_HASH_1 (key->name, key->length);
}

static unsigned long
variable_hash_2 (const void *keyv)
{
  struct variable const *key = (struct variable const *) keyv;
  return_STRING_N_HASH_2 (key->name, key->length);
}

static int
variable_hash_cmp (const void *xv, const void *yv)
{
  struct variable const *x = (struct variable const *) xv;
  struct variable const *y = (struct variable const *) yv;
  int result = x->length - y->length;
  if (result)
    return result;
  return_STRING_N_COMPARE (x->name, y->name, x->length);
}

#ifndef VARIABLE_BUCKETS
#define VARIABLE_BUCKETS                523
#endif
#ifndef PERFILE_VARIABLE_BUCKETS
#define PERFILE_VARIABLE_BUCKETS        23
#endif
#ifndef SMALL_SCOPE_VARIABLE_BUCKETS
#define SMALL_SCOPE_VARIABLE_BUCKETS    13
#endif

struct variable_set global_variable_set;
struct variable_set_list global_setlist
  = { 0, &global_variable_set, 0 };
struct variable_set_list *current_variable_set_list = &global_setlist;

/* Implement variables.  */

static void
check_valid_name (const floc* flocp, const char *name, size_t length)
{
  const char *cp, *end;

  if (!warn_check (wt_invalid_var))
    return;

  for (cp = name, end = name + length; cp < end; ++cp)
    if (ISSPACE (*cp))
      break;
  if (cp == end)
    return;

  warning (wt_invalid_var, flocp,
           ONS (format, 0, _("invalid variable name '%.*s'"), (int)length, name));
}

void
init_hash_global_variable_set (void)
{
  hash_init (&global_variable_set.table, VARIABLE_BUCKETS,
             variable_hash_1, variable_hash_2, variable_hash_cmp);
}

/* Define variable named NAME with value VALUE in SET.  VALUE is copied.
   LENGTH is the length of NAME, which does not need to be null-terminated.
   ORIGIN specifies the origin of the variable (makefile, command line
   or environment).
   If RECURSIVE is nonzero a flag is set in the variable saying
   that it should be recursively re-expanded.  */

struct variable *
define_variable_in_set (const char *name, size_t length,
                        const char *value, enum variable_origin origin,
                        int recursive, struct variable_set *set,
                        const floc *flocp)
{
  struct variable *v;
  struct variable **var_slot;
  struct variable var_key;

  check_valid_name (flocp, name, length);

  if (set == NULL)
    set = &global_variable_set;

  var_key.name = (char *) name;
  var_key.length = (unsigned int) length;
  var_slot = (struct variable **) hash_find_slot (&set->table, &var_key);
  v = *var_slot;

#if MK_OS_VMS
  /* VMS does not populate envp[] with DCL symbols and logical names which
     historically are mapped to environment variables.
     If the variable is not yet defined, then we need to check if getenv()
     can find it.  Do not do this for origin == o_env to avoid infinite
     recursion */
  if (HASH_VACANT (v) && (origin != o_env))
    {
      struct variable * vms_variable;
      char * vname = alloca (length + 1);
      char * vvalue;

      strncpy (vname, name, length);
      vvalue = getenv(vname);

      /* Values starting with '$' are probably foreign commands.
         We want to treat them as Shell aliases and not look them up here */
      if ((vvalue != NULL) && (vvalue[0] != '$'))
        {
          vms_variable =  lookup_variable(name, length);
          /* Refresh the slot */
          var_slot = (struct variable **) hash_find_slot (&set->table,
                                                          &var_key);
          v = *var_slot;
        }
    }
#endif

  if (env_overrides && origin == o_env)
    origin = o_env_override;

  if (! HASH_VACANT (v))
    {
      if (env_overrides && v->origin == o_env)
        /* V came from in the environment.  Since it was defined
           before the switches were parsed, it wasn't affected by -e.  */
        v->origin = o_env_override;

      /* A variable of this name is already defined.
         If the old definition is from a stronger source
         than this one, don't redefine it.  */
      if ((int) origin >= (int) v->origin)
        {
          free (v->value);
          v->value = xstrdup (value);
          if (flocp != 0)
            v->fileinfo = *flocp;
          else
            v->fileinfo.filenm = 0;
          v->origin = origin;
          v->recursive = recursive;
        }
      return v;
    }

  /* Create a new variable definition and add it to the hash table.  */

  v = xcalloc (sizeof (struct variable));
  v->name = xstrndup (name, length);
  v->length = (unsigned int) length;
  hash_insert_at (&set->table, v, var_slot);
  if (set == &global_variable_set)
    ++variable_changenum;

  v->value = xstrdup (value);
  if (flocp != 0)
    v->fileinfo = *flocp;
  v->origin = origin;
  v->recursive = recursive;

  v->export = v_default;
  v->exportable = 1;
  /* Check the nul-terminated variable name.  */
  name = v->name;
  if (*name != '_' && (*name < 'A' || *name > 'Z')
      && (*name < 'a' || *name > 'z'))
    v->exportable = 0;
  else
    {
      for (++name; *name != '\0'; ++name)
        if (*name != '_' && (*name < 'a' || *name > 'z')
            && (*name < 'A' || *name > 'Z') && !ISDIGIT(*name))
          break;

      if (*name != '\0')
        v->exportable = 0;
    }

  return v;
}


/* Undefine variable named NAME in SET. LENGTH is the length of NAME, which
   does not need to be null-terminated. ORIGIN specifies the origin of the
   variable (makefile, command line or environment). */

static void
free_variable_name_and_value (const void *item)
{
  struct variable *v = (struct variable *) item;
  free (v->name);
  free (v->value);
}

void
free_variable_set (struct variable_set_list *list)
{
  hash_map (&list->set->table, free_variable_name_and_value);
  hash_free (&list->set->table, 1);
  free (list->set);
  free (list);
}

void
undefine_variable_in_set (const floc *flocp, const char *name, size_t length,
                          enum variable_origin origin,
                          struct variable_set *set)
{
  struct variable *v;
  struct variable **var_slot;
  struct variable var_key;

  check_valid_name (flocp, name, length);

  if (set == NULL)
    set = &global_variable_set;

  var_key.name = (char *) name;
  var_key.length = (unsigned int) length;
  var_slot = (struct variable **) hash_find_slot (&set->table, &var_key);

  if (env_overrides && origin == o_env)
    origin = o_env_override;

  v = *var_slot;
  if (! HASH_VACANT (v))
    {
      if (env_overrides && v->origin == o_env)
        /* V came from in the environment.  Since it was defined
           before the switches were parsed, it wasn't affected by -e.  */
        v->origin = o_env_override;

      /* Undefine only if this undefinition is from an equal or stronger
         source than the variable definition.  */
      if ((int) origin >= (int) v->origin)
        {
          hash_delete_at (&set->table, var_slot);
          free_variable_name_and_value (v);
          free (v);
          if (set == &global_variable_set)
            ++variable_changenum;
        }
    }
}

/* If the variable passed in is "special", handle its special nature.
   Currently there are two such variables, both used for introspection:
   .VARIABLES expands to a list of all the variables defined in this instance
   of make.
   .TARGETS expands to a list of all the targets defined in this
   instance of make.
   Returns the variable reference passed in.  */

#define EXPANSION_INCREMENT(_l)  ((((_l) / 500) + 1) * 500)

static struct variable *
lookup_special_var (struct variable *var)
{
  static unsigned long last_changenum = 0;

  /* This one actually turns out to be very hard, due to the way the parser
     records targets.  The way it works is that target information is collected
     internally until make knows the target is completely specified.  Only when
     it sees that some new construct (a new target or variable) is defined does
     make know that the previous one is done.  In short, this means that if
     you do this:

       all:

       TARGS := $(.TARGETS)

     then $(TARGS) won't contain "all", because it's not until after the
     variable is created that the previous target is completed.

     Changing this would be a major pain.  I think a less complex way to do it
     would be to pre-define the target files as soon as the first line is
     parsed, then come back and do the rest of the definition as now.  That
     would allow $(.TARGETS) to be correct without a major change to the way
     the parser works.

  if (streq (var->name, ".TARGETS"))
    var->value = build_target_list (var->value);
  else
  */

  if (variable_changenum != last_changenum && streq (var->name, ".VARIABLES"))
    {
      size_t max = EXPANSION_INCREMENT (strlen (var->value));
      size_t len;
      char *p;
      struct variable **vp = (struct variable **) global_variable_set.table.ht_vec;
      struct variable **end = &vp[global_variable_set.table.ht_size];

      /* Make sure we have at least MAX bytes in the allocated buffer.  */
      var->value = xrealloc (var->value, max);

      /* Walk through the hash of variables, constructing a list of names.  */
      p = var->value;
      len = 0;
      for (; vp < end; ++vp)
        if (!HASH_VACANT (*vp))
          {
            struct variable *v = *vp;
            int l = v->length;

            len += l + 1;
            if (len > max)
              {
                size_t off = p - var->value;

                max += EXPANSION_INCREMENT (l + 1);
                var->value = xrealloc (var->value, max);
                p = &var->value[off];
              }

            p = mempcpy (p, v->name, l);
            *(p++) = ' ';
          }
      *(p-1) = '\0';

      /* Remember the current variable change number.  */
      last_changenum = variable_changenum;
    }

  return var;
}


/* Check the variable name for validity.  */
static void
check_variable_reference (const char *name, size_t length)
{
  const char *cp, *end;

  if (!warn_check (wt_invalid_ref))
    return;

  for (cp = name, end = name + length; cp < end; ++cp)
    if (ISSPACE (*cp))
      break;
  if (cp == end)
    return;

  warning (wt_invalid_ref, *expanding_var,
           ONS (format, 0, _("invalid variable reference '%.*s'"), (int)length, name));
}

/* Lookup a variable whose name is a string starting at NAME
   and with LENGTH chars.  NAME need not be null-terminated.
   Returns address of the 'struct variable' containing all info
   on the variable, or nil if no such variable is defined.  */

struct variable *
lookup_variable (const char *name, size_t length)
{
  const struct variable_set_list *setlist;
  struct variable var_key;
  int is_parent = 0;

  check_variable_reference (name, length);

  var_key.name = (char *) name;
  var_key.length = (unsigned int) length;

  for (setlist = current_variable_set_list;
       setlist != 0; setlist = setlist->next)
    {
      const struct variable_set *set = setlist->set;
      struct variable *v;

      v = hash_find_item ((struct hash_table *) &set->table, &var_key);
      if (v && (!is_parent || !v->private_var))
        return v->special ? lookup_special_var (v) : v;

      is_parent |= setlist->next_is_parent;
    }

#if MK_OS_VMS
  /* VMS doesn't populate envp[] with DCL symbols and logical names, which
     historically are mapped to environment variables and returned by
     getenv().  */
  {
    char *vname = alloca (length + 1);
    char *value;
    strncpy (vname, name, length);
    vname[length] = 0;
    value = getenv (vname);
    if (value != 0)
      {
        char *sptr;
        int scnt;

        sptr = value;
        scnt = 0;

        while ((sptr = strchr (sptr, '$')))
          {
            scnt++;
            sptr++;
          }

        if (scnt > 0)
          {
            char *nvalue;
            char *nptr;

            nvalue = alloca (strlen (value) + scnt + 1);
            sptr = value;
            nptr = nvalue;

            while (*sptr)
              {
                if (*sptr == '$')
                  {
                    *nptr++ = '$';
                    *nptr++ = '$';
                  }
                else
                  *nptr++ = *sptr;
                sptr++;
              }

            *nptr = '\0';
            return define_variable (vname, length, nvalue, o_env, 1);

          }

        return define_variable (vname, length, value, o_env, 1);
      }
  }
#endif /* MK_OS_VMS */

  return 0;
}
/* Lookup a variable whose name is a string starting at NAME
   and with LENGTH chars.  NAME need not be null-terminated.
   Returns address of the 'struct variable' containing all info
   on the variable, or nil if no such variable is defined.  */

struct variable *
lookup_variable_for_file (const char *name, size_t length, struct file *file)
{
  struct variable *var;
  struct variable_set_list *savev;

  if (file == NULL)
    return lookup_variable (name, length);

  install_file_context (file, &savev, NULL);

  var = lookup_variable (name, length);

  restore_file_context (savev, NULL);

  return var;
}

/* Lookup a variable whose name is a string starting at NAME
   and with LENGTH chars in set SET.  NAME need not be null-terminated.
   Returns address of the 'struct variable' containing all info
   on the variable, or nil if no such variable is defined.  */

struct variable *
lookup_variable_in_set (const char *name, size_t length,
                        const struct variable_set *set)
{
  struct variable var_key;

  check_variable_reference (name, length);

  var_key.name = (char *) name;
  var_key.length = (unsigned int) length;

  return hash_find_item ((struct hash_table *) &set->table, &var_key);
}

/* Initialize FILE's variable set list.  If FILE already has a variable set
   list, the topmost variable set is left intact, but the the rest of the
   chain is replaced with FILE->parent's setlist.  If FILE is a double-colon
   rule, then we will use the "root" double-colon target's variable set as the
   parent of FILE's variable set.

   If we're READING a makefile, don't do the pattern variable search now,
   since the pattern variable might not have been defined yet.  */

void
initialize_file_variables (struct file *file, int reading)
{
  struct variable_set_list *l = file->variables;

  if (l == 0)
    {
      l = (struct variable_set_list *)
        xmalloc (sizeof (struct variable_set_list));
      l->set = xmalloc (sizeof (struct variable_set));
      hash_init (&l->set->table, PERFILE_VARIABLE_BUCKETS,
                 variable_hash_1, variable_hash_2, variable_hash_cmp);
      file->variables = l;
    }

  /* If this is a double-colon, then our "parent" is the "root" target for
     this double-colon rule.  Since that rule has the same name, parent,
     etc. we can just use its variables as the "next" for ours.  */

  if (file->double_colon && file->double_colon != file)
    {
      initialize_file_variables (file->double_colon, reading);
      l->next = file->double_colon->variables;
      l->next_is_parent = 0;
      return;
    }

  if (file->parent == 0)
    l->next = &global_setlist;
  else
    {
      initialize_file_variables (file->parent, reading);
      l->next = file->parent->variables;
    }
  l->next_is_parent = 1;

  /* If we're not reading makefiles and we haven't looked yet, see if
     we can find pattern variables for this target.  */

  if (!reading && !file->pat_searched)
    {
      struct pattern_var *p;
      const size_t targlen = strlen (file->name);

      p = lookup_pattern_var (0, file->name, targlen);
      if (p != 0)
        {
          struct variable_set_list *global = current_variable_set_list;

          /* We found at least one.  Set up a new variable set to accumulate
             all the pattern variables that match this target.  */

          file->pat_variables = create_new_variable_set ();
          current_variable_set_list = file->pat_variables;

          do
            {
              /* We found one, so insert it into the set.  */

              struct variable *v;

              if (p->variable.flavor == f_simple)
                {
                  v = define_variable_loc (
                    p->variable.name, strlen (p->variable.name),
                    p->variable.value, p->variable.origin,
                    0, &p->variable.fileinfo);

                  v->flavor = f_simple;
                }
              else
                v = do_variable_definition (
                  &p->variable.fileinfo, p->variable.name, p->variable.value,
                  p->variable.origin, p->variable.flavor,
                  p->variable.conditional, s_pattern);

              /* Also mark it as a per-target and copy export status. */
              v->per_target = p->variable.per_target;
              v->export = p->variable.export;
              v->private_var = p->variable.private_var;
            }
          while ((p = lookup_pattern_var (p, file->name, targlen)) != 0);

          current_variable_set_list = global;
        }
      file->pat_searched = 1;
    }

  /* If we have a pattern variable match, set it up.  */

  if (file->pat_variables != 0)
    {
      file->pat_variables->next = l->next;
      file->pat_variables->next_is_parent = l->next_is_parent;
      l->next = file->pat_variables;
      l->next_is_parent = 0;
    }
}

/* Pop the top set off the current variable set list,
   and free all its storage.  */

struct variable_set_list *
create_new_variable_set (void)
{
  struct variable_set_list *setlist;
  struct variable_set *set;

  set = xmalloc (sizeof (struct variable_set));
  hash_init (&set->table, SMALL_SCOPE_VARIABLE_BUCKETS,
             variable_hash_1, variable_hash_2, variable_hash_cmp);

  setlist = (struct variable_set_list *)
    xmalloc (sizeof (struct variable_set_list));
  setlist->set = set;
  setlist->next = current_variable_set_list;
  setlist->next_is_parent = 0;

  return setlist;
}

/* Create a new variable set and push it on the current setlist.
   If we're pushing a global scope (that is, the current scope is the global
   scope) then we need to "push" it the other way: file variable sets point
   directly to the global_setlist so we need to replace that with the new one.
 */

struct variable_set_list *
push_new_variable_scope (void)
{
  current_variable_set_list = create_new_variable_set ();
  if (current_variable_set_list->next == &global_setlist)
    {
      /* It was the global, so instead of new -> &global we want to replace
         &global with the new one and have &global -> new, with current still
         pointing to &global  */
      struct variable_set *set = current_variable_set_list->set;
      current_variable_set_list->set = global_setlist.set;
      global_setlist.set = set;
      current_variable_set_list->next = global_setlist.next;
      global_setlist.next = current_variable_set_list;
      current_variable_set_list = &global_setlist;
    }
  return current_variable_set_list;
}

void
pop_variable_scope (void)
{
  struct variable_set_list *setlist;
  struct variable_set *set;

  /* Can't call this if there's no scope to pop!  */
  assert (current_variable_set_list->next != NULL);

  if (current_variable_set_list != &global_setlist)
    {
      /* We're not pointing to the global setlist, so pop this one.  */
      setlist = current_variable_set_list;
      set = setlist->set;
      current_variable_set_list = setlist->next;
    }
  else
    {
      /* This set is the one in the global_setlist, but there is another global
         set beyond that.  We want to copy that set to global_setlist, then
         delete what used to be in global_setlist.  */
      setlist = global_setlist.next;
      set = global_setlist.set;
      global_setlist.set = setlist->set;
      global_setlist.next = setlist->next;
      global_setlist.next_is_parent = setlist->next_is_parent;
    }

  /* Free the one we no longer need.  */
  free (setlist);
  hash_map (&set->table, free_variable_name_and_value);
  hash_free (&set->table, 1);
  free (set);
}

/* Install a new global context for FILE so that errors/warnings are shown
   in that context.  Sets OLDLIST to the previous list, and if not NULL sets
   OLDFLOC to reading_file and changes reading_file to the current FILE.
   Use restore_file_context() to undo this.  */

void
install_file_context (struct file *file, struct variable_set_list **oldlist, const floc **oldfloc)
{
  *oldlist = current_variable_set_list;
  current_variable_set_list = file->variables;

  if (oldfloc)
    {
      *oldfloc = reading_file;
      if (file->cmds && file->cmds->fileinfo.filenm)
        reading_file = &file->cmds->fileinfo;
      else
        reading_file = NULL;
    }
}

/* Restore a saved global context from OLDLIST.  If OLDFLOC is not NULL,
   set reading_file back to that value.  */

void
restore_file_context (struct variable_set_list *oldlist, const floc *oldfloc)
{
  current_variable_set_list = oldlist;
  if (oldfloc)
    reading_file = oldfloc;
}


/* Merge FROM_SET into TO_SET, freeing unused storage in FROM_SET.  */

static void
merge_variable_sets (struct variable_set *to_set,
                     struct variable_set *from_set)
{
  struct variable **from_var_slot = (struct variable **) from_set->table.ht_vec;
  struct variable **from_var_end = from_var_slot + from_set->table.ht_size;

  int inc = to_set == &global_variable_set ? 1 : 0;

  for ( ; from_var_slot < from_var_end; from_var_slot++)
    if (! HASH_VACANT (*from_var_slot))
      {
        struct variable *from_var = *from_var_slot;
        struct variable **to_var_slot
          = (struct variable **) hash_find_slot (&to_set->table, *from_var_slot);
        if (HASH_VACANT (*to_var_slot))
          {
            hash_insert_at (&to_set->table, from_var, to_var_slot);
            variable_changenum += inc;
          }
        else
          {
            /* GKM FIXME: delete in from_set->table */
            free (from_var->value);
            free (from_var);
          }
      }
}

/* Merge SETLIST1 into SETLIST0, freeing unused storage in SETLIST1.  */

void
merge_variable_set_lists (struct variable_set_list **setlist0,
                          struct variable_set_list *setlist1)
{
  struct variable_set_list *to = *setlist0;
  struct variable_set_list *last0 = 0;

  /* If there's nothing to merge, stop now.  */
  if (!setlist1 || setlist1 == &global_setlist)
    return;

  if (to)
    {
      /* These loops rely on the fact that all setlists terminate with the
         global setlist (before NULL).  If not, arguably we SHOULD die.  */

      /* Make sure that setlist1 is not already a subset of setlist0.  */
      while (to != &global_setlist)
        {
          if (to == setlist1)
            return;
          to = to->next;
        }

      to = *setlist0;
      while (setlist1 != &global_setlist && to != &global_setlist)
        {
          struct variable_set_list *from = setlist1;
          setlist1 = setlist1->next;

          merge_variable_sets (to->set, from->set);

          last0 = to;
          to = to->next;
        }
    }

  if (setlist1 != &global_setlist)
    {
      if (last0 == 0)
        *setlist0 = setlist1;
      else
        last0->next = setlist1;
    }
}

/* Define the automatic variables, and record the addresses
   of their structures so we can change their values quickly.  */

void
define_automatic_variables (void)
{
  struct variable *v;
  char buf[200];

  sprintf (buf, "%u", makelevel);
  define_variable_cname (MAKELEVEL_NAME, buf, o_env, 0);

  sprintf (buf, "%s%s%s",
           version_string,
           (remote_description == 0 || remote_description[0] == '\0')
           ? "" : "-",
           (remote_description == 0 || remote_description[0] == '\0')
           ? "" : remote_description);
  define_variable_cname ("MAKE_VERSION", buf, o_default, 0);
  define_variable_cname ("MAKE_HOST", make_host, o_default, 0);

#if MK_OS_DOS
  /* Allow to specify a special shell just for Make,
     and use $COMSPEC as the default $SHELL when appropriate.  */
  {
    static char shell_str[] = "SHELL";
    const int shlen = sizeof (shell_str) - 1;
    struct variable *mshp = lookup_variable ("MAKESHELL", 9);
    struct variable *comp = lookup_variable ("COMSPEC", 7);

    /* $(MAKESHELL) overrides $(SHELL) even if -e is in effect.  */
    if (mshp)
      (void) define_variable (shell_str, shlen,
                              mshp->value, o_env_override, 0);
    else if (comp)
      {
        /* $(COMSPEC) shouldn't override $(SHELL).  */
        struct variable *shp = lookup_variable (shell_str, shlen);

        if (!shp)
          (void) define_variable (shell_str, shlen, comp->value, o_env, 0);
      }
  }
#elif MK_OS_OS2
  {
    static char shell_str[] = "SHELL";
    const int shlen = sizeof (shell_str) - 1;
    struct variable *shell = lookup_variable (shell_str, shlen);
    struct variable *replace = lookup_variable ("MAKESHELL", 9);

    /* if $MAKESHELL is defined in the environment assume o_env_override */
    if (replace && *replace->value && replace->origin == o_env)
      replace->origin = o_env_override;

    /* if $MAKESHELL is not defined use $SHELL but only if the variable
       did not come from the environment */
    if (!replace || !*replace->value)
      if (shell && *shell->value && (shell->origin == o_env
          || shell->origin == o_env_override))
        {
          /* overwrite whatever we got from the environment */
          free (shell->value);
          shell->value = xstrdup (default_shell);
          shell->origin = o_default;
        }

    /* Some people do not like cmd to be used as the default
       if $SHELL is not defined in the Makefile.
       With -DNO_CMD_DEFAULT you can turn off this behaviour */
# ifndef NO_CMD_DEFAULT
    /* otherwise use $COMSPEC */
    if (!replace || !*replace->value)
      replace = lookup_variable ("COMSPEC", 7);

    /* otherwise use $OS2_SHELL */
    if (!replace || !*replace->value)
      replace = lookup_variable ("OS2_SHELL", 9);
# else
#   warning NO_CMD_DEFAULT: GNU Make will not use CMD.EXE as default shell
# endif

    if (replace && *replace->value)
      /* overwrite $SHELL */
      (void) define_variable (shell_str, shlen, replace->value,
                              replace->origin, 0);
    else
      /* provide a definition if there is none */
      (void) define_variable (shell_str, shlen, default_shell,
                              o_default, 0);
  }

#endif

  /* This won't override any definition, but it will provide one if there
     isn't one there.  */
  v = define_variable_cname ("SHELL", default_shell, o_default, 0);
#if MK_OS_DOS
  v->export = v_export;  /*  Export always SHELL.  */
#endif

  /* On MSDOS we do use SHELL from environment, since it isn't a standard
     environment variable on MSDOS, so whoever sets it, does that on purpose.
     On OS/2 we do not use SHELL from environment but we have already handled
     that problem above. */
#if !MK_OS_DOS && !MK_OS_OS2
  /* Don't let SHELL come from the environment.  */
  if (*v->value == '\0' || v->origin == o_env || v->origin == o_env_override)
    {
      free (v->value);
      v->origin = o_file;
      v->value = xstrdup (default_shell);
    }
#endif

  /* Make sure MAKEFILES gets exported if it is set.  */
  v = define_variable_cname ("MAKEFILES", "", o_default, 0);
  v->export = v_ifset;

  /* Define the magic D and F variables in terms of
     the automatic variables they are variations of.  */

#if MK_OS_DOS || MK_OS_W32
  /* For consistency, remove the trailing backslash as well as slash.  */
  define_variable_cname ("@D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $@)))",
                         o_automatic, 1);
  define_variable_cname ("%D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $%)))",
                         o_automatic, 1);
  define_variable_cname ("*D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $*)))",
                         o_automatic, 1);
  define_variable_cname ("<D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $<)))",
                         o_automatic, 1);
  define_variable_cname ("?D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $?)))",
                         o_automatic, 1);
  define_variable_cname ("^D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $^)))",
                         o_automatic, 1);
  define_variable_cname ("+D", "$(patsubst %/,%,$(patsubst %\\,%,$(dir $+)))",
                         o_automatic, 1);
#else  /* not MK_OS_DOS, not MK_OS_W32 */
  define_variable_cname ("@D", "$(patsubst %/,%,$(dir $@))", o_automatic, 1);
  define_variable_cname ("%D", "$(patsubst %/,%,$(dir $%))", o_automatic, 1);
  define_variable_cname ("*D", "$(patsubst %/,%,$(dir $*))", o_automatic, 1);
  define_variable_cname ("<D", "$(patsubst %/,%,$(dir $<))", o_automatic, 1);
  define_variable_cname ("?D", "$(patsubst %/,%,$(dir $?))", o_automatic, 1);
  define_variable_cname ("^D", "$(patsubst %/,%,$(dir $^))", o_automatic, 1);
  define_variable_cname ("+D", "$(patsubst %/,%,$(dir $+))", o_automatic, 1);
#endif
  define_variable_cname ("@F", "$(notdir $@)", o_automatic, 1);
  define_variable_cname ("%F", "$(notdir $%)", o_automatic, 1);
  define_variable_cname ("*F", "$(notdir $*)", o_automatic, 1);
  define_variable_cname ("<F", "$(notdir $<)", o_automatic, 1);
  define_variable_cname ("?F", "$(notdir $?)", o_automatic, 1);
  define_variable_cname ("^F", "$(notdir $^)", o_automatic, 1);
  define_variable_cname ("+F", "$(notdir $+)", o_automatic, 1);
}


static int
should_export (const struct variable *v)
{
  switch (v->export)
    {
    case v_export:
      break;

    case v_noexport:
      return 0;

    case v_ifset:
      if (v->origin == o_default)
        return 0;
      break;

    case v_default:
      if (v->origin == o_default || v->origin == o_automatic)
        /* Only export default variables by explicit request.  */
        return 0;

      /* The variable doesn't have a name that can be exported.  */
      if (! v->exportable)
        return 0;

      if (! export_all_variables
          && v->origin != o_command
          && v->origin != o_env && v->origin != o_env_override)
        return 0;
      break;
    }

  return 1;
}

/* Create a new environment for FILE's commands.
   If FILE is nil, this is for the 'shell' function.
   The child's MAKELEVEL variable is incremented.
   If recursive is true then we're running a recursive make, else not.  */

char **
target_environment (struct file *file, int recursive)
{
  struct variable_set_list *set_list;
  struct variable_set_list *s;
  struct hash_table table;
  struct variable **v_slot;
  struct variable **v_end;
  char **result_0;
  char **result;
  const char *invalid = NULL;
  /* If we got no value from the environment then never add the default.  */
  int added_SHELL = shell_var.value == 0;
  int found_makelevel = 0;
  int found_mflags = 0;
  int found_makeflags = 0;

  /* If file is NULL we're creating the target environment for $(shell ...)
     Remember this so we can just ignore recursion.  */
  if (!file)
    ++env_recursion;

  /* We need to update makeflags if (a) we're not recurive, (b) jobserver_auth
     is enabled, and (c) we need to add invalidation.  */
  if (!recursive && jobserver_auth)
    invalid = jobserver_get_invalid_auth ();

  if (file)
    set_list = file->variables;
  else
    set_list = current_variable_set_list;

  hash_init (&table, VARIABLE_BUCKETS,
             variable_hash_1, variable_hash_2, variable_hash_cmp);

  /* Run through all the variable sets in the list, accumulating variables
     in TABLE.  We go from most specific to least, so the first variable we
     encounter is the keeper.  */
  for (s = set_list; s != 0; s = s->next)
    {
      struct variable_set *set = s->set;
      const int islocal = s == set_list;
      const int isglobal = set == &global_variable_set;

      v_slot = (struct variable **) set->table.ht_vec;
      v_end = v_slot + set->table.ht_size;
      for ( ; v_slot < v_end; v_slot++)
        if (! HASH_VACANT (*v_slot))
          {
            struct variable **evslot;
            struct variable *v = *v_slot;

            if (!islocal && v->private_var)
              continue;

            evslot = (struct variable **) hash_find_slot (&table, v);

            if (HASH_VACANT (*evslot))
              {
                /* We'll always add target-specific variables, since we may
                   discover that they should be exported later: we'll check
                   again below.  For global variables only add them if they're
                   exportable.  */
                if (!isglobal || should_export (v))
                  hash_insert_at (&table, v, evslot);
              }
            else if ((*evslot)->export == v_default)
              /* We already have a variable but we don't know its status.  */
              (*evslot)->export = v->export;
          }
    }

  result = result_0 = xmalloc ((table.ht_fill + 3) * sizeof (char *));

  v_slot = (struct variable **) table.ht_vec;
  v_end = v_slot + table.ht_size;
  for ( ; v_slot < v_end; v_slot++)
    if (! HASH_VACANT (*v_slot))
      {
        struct variable *v = *v_slot;
        char *value = v->value;
        char *cp = NULL;

        /* This might be here because it was a target-specific variable that
           we didn't know the status of when we added it.  */
        if (! should_export (v))
          continue;

        /* If V is recursively expanded and didn't come from the environment,
           expand its value.  If it came from the environment, it should
           go back into the environment unchanged... except MAKEFLAGS.  */
        if (v->recursive && ((v->origin != o_env && v->origin != o_env_override)
                             || streq (v->name, MAKEFLAGS_NAME)))
          value = cp = recursively_expand_for_file (v, file);

        /* If this is the SHELL variable remember we already added it.  */
        if (!added_SHELL && streq (v->name, "SHELL"))
          {
            added_SHELL = 1;
            goto setit;
          }

        /* If this is MAKELEVEL, update it.  */
        if (!found_makelevel && streq (v->name, MAKELEVEL_NAME))
          {
            char val[INTSTR_LENGTH + 1];
            sprintf (val, "%u", makelevel + 1);
            free (cp);
            value = cp = xstrdup (val);
            found_makelevel = 1;
            goto setit;
          }

        /* If we need to reset jobserver, check for MAKEFLAGS / MFLAGS.  */
        if (invalid)
          {
            if (!found_makeflags && streq (v->name, MAKEFLAGS_NAME))
              {
                char *mf;
                char *vars;
                found_makeflags = 1;

                if (!strstr (value, " --" JOBSERVER_AUTH_OPT "="))
                  goto setit;

                /* The invalid option must come before variable overrides.  */
                vars = strstr (value, " -- ");
                if (!vars)
                  mf = xstrdup (concat (2, value, invalid));
                else
                  {
                    size_t lf = vars - value;
                    size_t li = strlen (invalid);
                    mf = xmalloc (strlen (value) + li + 1);
                    strcpy (mempcpy (mempcpy (mf, value, lf), invalid, li),
                            vars);
                  }
                free (cp);
                value = cp = mf;
                if (found_mflags)
                  invalid = NULL;
                goto setit;
              }

            if (!found_mflags && streq (v->name, "MFLAGS"))
              {
                const char *mf;
                found_mflags = 1;

                if (!strstr (value, " --" JOBSERVER_AUTH_OPT "="))
                  goto setit;

                if (v->origin != o_env)
                  goto setit;
                mf = concat (2, value, invalid);
                free (cp);
                value = cp = xstrdup (mf);
                if (found_makeflags)
                  invalid = NULL;
                goto setit;
              }
          }

#if MK_OS_W32
        if (streq (v->name, "Path") || streq (v->name, "PATH"))
          {
            if (!cp)
              cp = xstrdup (value);
            value = convert_Path_to_windows32 (cp, ';');
            goto setit;
          }
#endif

      setit:
        *result++ = xstrdup (concat (3, v->name, "=", value));
        free (cp);
      }

  if (!added_SHELL)
    *result++ = xstrdup (concat (3, shell_var.name, "=", shell_var.value));

  if (!found_makelevel)
    {
      char val[MAKELEVEL_LENGTH + 1 + INTSTR_LENGTH + 1];
      sprintf (val, "%s=%u", MAKELEVEL_NAME, makelevel + 1);
      *result++ = xstrdup (val);
    }

  *result = NULL;

  hash_free (&table, 0);

  if (!file)
    --env_recursion;

  return result_0;
}

static struct variable *
set_special_var (struct variable *var, enum variable_origin origin)
{
  if (streq (var->name, MAKEFLAGS_NAME))
    reset_makeflags (origin);

  else if (streq (var->name, RECIPEPREFIX_NAME))
    /* The user is resetting the command introduction prefix.  This has to
       happen immediately, so that subsequent rules are interpreted
       properly.  */
    cmd_prefix = var->value[0]=='\0' ? RECIPEPREFIX_DEFAULT : var->value[0];

  else if (streq (var->name, WARNINGS_NAME))
    {
      /* It's weird but for .WARNINGS to make sense we need to expand them
         when they are set, even if it's a recursive variable.  */
      char *actions = allocated_expand_variable (STRING_SIZE_TUPLE (WARNINGS_NAME));
      decode_warn_actions (actions, &var->fileinfo);
      free (actions);
    }

  return var;
}

/* Given a string, shell-execute it and return a malloc'ed string of the
 * result. This removes only ONE newline (if any) at the end, for maximum
 * compatibility with the *BSD makes.  If it fails, returns NULL. */

static char *
shell_result (const char *p)
{
  char *buf;
  size_t len;
  char *args[2];

  install_variable_buffer (&buf, &len);

  args[0] = (char *) p;
  args[1] = NULL;
  func_shell_base (variable_buffer, args, 0);

  return swap_variable_buffer (buf, len);
}

/* Given a variable, a value, and a flavor, define the variable.
   See the try_variable_definition() function for details on the parameters. */

struct variable *
do_variable_definition (const floc *flocp, const char *varname, const char *value,
                        enum variable_origin origin, enum variable_flavor flavor,
                        int conditional, enum variable_scope scope)
{
  const char *newval;
  char *alloc_value = NULL;
  struct variable *v;
  int append = 0;

  /* Conditional variable definition: only set if the var is not defined. */
  if (conditional)
    {
      v = lookup_variable (varname, strlen (varname));
      if (v)
        return v;
    }

  /* Calculate the variable's new value in VALUE.  */

  switch (flavor)
    {
    case f_simple:
      /* A simple variable definition "var := value".  Expand the value.
         We have to allocate memory since otherwise it'll clobber the
         variable buffer, and we may still need that if we're looking at a
         target-specific variable.  */
      newval = alloc_value = allocated_expand_string (value);
      break;
    case f_expand:
      {
        /* A POSIX "var :::= value" assignment.  Expand the value, then it
           becomes a recursive variable.  After expansion convert all '$'
           tokens to '$$' to resolve to '$' when recursively expanded.  */
        char *t = allocated_expand_string (value);
        char *np = alloc_value = xmalloc (strlen (t) * 2 + 1);
        char *op = t;
        while (op[0] != '\0')
          {
            if (op[0] == '$')
              *(np++) = '$';
            *(np++) = *(op++);
          }
        *np = '\0';
        free (t);
        newval = alloc_value;
        break;
      }
    case f_shell:
      {
        /* A shell definition "var != value".  Expand value, pass it to
           the shell, and store the result in recursively-expanded var. */
        char *q = allocated_expand_string (value);
        alloc_value = shell_result (q);
        free (q);
        flavor = f_recursive;
        newval = alloc_value;
        break;
      }
    case f_recursive:
      /* A recursive variable definition "var = value".
         The value is used verbatim.  */
      newval = value;
      break;
    case f_append:
    case f_append_value:
      {
        int override = 0;
        if (scope == s_global)
          v = lookup_variable (varname, strlen (varname));
        else
          {
            /* When appending in a target/pattern variable context, we want to
               append only with other variables in the context of this
               target/pattern.  */
            append = 1;
            v = lookup_variable_in_set (varname, strlen (varname),
                                        current_variable_set_list->set);
            if (v)
              {
                /* Don't append from the global set if a previous non-appending
                   target/pattern-specific variable definition exists. */
                if (!v->append)
                  append = 0;

                if (scope == s_pattern &&
                    (v->origin == o_env_override || v->origin == o_command))
                  {
                    /* This is the case of multiple target/pattern specific
                       definitions/appends, e.g.
                         al%: hello := first
                         al%: hello += second
                       in the presence of a command line definition or an
                       env override.  Do not merge x->value and value here.
                       For pattern-specific variables the values are merged in
                       recursively_expand_for_file.  */
                    override = 1;
                    append = 1;
                  }
              }
          }

        if (!v)
          {
            /* There was no old value: make this a recursive definition.  */
            newval = value;
            flavor = f_recursive;
          }
        else if (override)
          {
            /* Command line definition / env override takes precedence over
               a pattern/target-specific append.  */
            newval = value;
            /* Set flavor to f_recursive to recursively expand this variable
               at build time in recursively_expand_for_file.  */
            flavor = f_recursive;
          }
        else
          {
            /* Paste the old and new values together in VALUE.  */

            size_t oldlen, vallen, alloclen;
            const char *val;
            char *cp;
            char *tp = NULL;

            val = value;
            if (v->recursive)
              /* The previous definition of the variable was recursive.
                 The new value is the unexpanded old and new values.  */
              flavor = f_recursive;
            else if (flavor != f_append_value)
              /* The previous definition of the variable was simple.
                 The new value comes from the old value, which was expanded
                 when it was set; and from the expanded new value.  Allocate
                 memory for the expansion as we may still need the rest of the
                 buffer if we're looking at a target-specific variable.  */
              val = tp = allocated_expand_string (val);

            /* If the new value is empty, nothing to do.  */
            vallen = strlen (val);
            if (!vallen)
              {
                alloc_value = tp;
                goto done;
              }

            oldlen = strlen (v->value);
            alloclen = oldlen + 1 + vallen + 1;
            cp = alloc_value = xmalloc (alloclen);

            if (oldlen)
              {
                char *s;
                if (streq (varname, MAKEFLAGS_NAME)
                    && (s = strstr (v->value, " -- ")) != NULL)
                  /* We found a separator in MAKEFLAGS.  Ignore variable
                     assignments: set_special_var() will reconstruct things.  */
                  cp = mempcpy (cp, v->value, s - v->value);
                else
                  cp = mempcpy (cp, v->value, oldlen);
                *(cp++) = ' ';
              }

            memcpy (cp, val, vallen + 1);
            free (tp);
            newval = alloc_value;
          }
      }
      break;
    case f_bogus:
    default:
      /* Should not be possible.  */
      abort ();
    }

  assert (newval);

#if MK_OS_DOS
  /* Many Unix Makefiles include a line saying "SHELL=/bin/sh", but
     non-Unix systems don't conform to this default configuration (in
     fact, most of them don't even have '/bin').  On the other hand,
     $SHELL in the environment, if set, points to the real pathname of
     the shell.
     Therefore, we generally won't let lines like "SHELL=/bin/sh" from
     the Makefile override $SHELL from the environment.  But first, we
     look for the basename of the shell in the directory where SHELL=
     points, and along the $PATH; if it is found in any of these places,
     we define $SHELL to be the actual pathname of the shell.  Thus, if
     you have bash.exe installed as d:/unix/bash.exe, and d:/unix is on
     your $PATH, then SHELL=/usr/local/bin/bash will have the effect of
     defining SHELL to be "d:/unix/bash.exe".  */
  if ((origin == o_file || origin == o_override)
      && strcmp (varname, "SHELL") == 0)
    {
      PATH_VAR (shellpath);
      extern char * __dosexec_find_on_path (const char *, char *[], char *);

      /* See if we can find "/bin/sh.exe", "/bin/sh.com", etc.  */
      if (__dosexec_find_on_path (p, NULL, shellpath))
        {
          char *tp;

          for (tp = shellpath; *tp; tp++)
            if (*tp == '\\')
              *tp = '/';

          v = define_variable_loc (varname, strlen (varname),
                                   shellpath, origin, flavor == f_recursive,
                                   flocp);
        }
      else
        {
          const char *shellbase, *bslash;
          struct variable *pathv = lookup_variable ("PATH", 4);
          char *path_string;
          char *fake_env[2];
          size_t pathlen = 0;

          shellbase = strrchr (newval, '/');
          bslash = strrchr (newval, '\\');
          if (!shellbase || bslash > shellbase)
            shellbase = bslash;
          if (!shellbase && newval[1] == ':')
            shellbase = newval + 1;
          if (shellbase)
            shellbase++;
          else
            shellbase = newval;

          /* Search for the basename of the shell (with standard
             executable extensions) along the $PATH.  */
          if (pathv)
            pathlen = strlen (pathv->value);
          path_string = xmalloc (5 + pathlen + 2 + 1);
          /* On MSDOS, current directory is considered as part of $PATH.  */
          sprintf (path_string, "PATH=.;%s", pathv ? pathv->value : "");
          fake_env[0] = path_string;
          fake_env[1] = 0;
          if (__dosexec_find_on_path (shellbase, fake_env, shellpath))
            {
              char *tp;

              for (tp = shellpath; *tp; tp++)
                if (*tp == '\\')
                  *tp = '/';

              v = define_variable_loc (varname, strlen (varname),
                                       shellpath, origin,
                                       flavor == f_recursive, flocp);
            }
          else
            v = lookup_variable (varname, strlen (varname));

          free (path_string);
        }
    }
  else
#endif /* MK_OS_DOS */
#if MK_OS_W32
  if ((origin == o_file || origin == o_override || origin == o_command)
      && streq (varname, "SHELL"))
    {
      extern const char *default_shell;

      /* Call shell locator function. If it returns TRUE, then
         set no_default_sh_exe to indicate sh was found and
         set new value for SHELL variable.  */

      if (find_and_set_default_shell (newval))
        {
          v = define_variable_in_set (varname, strlen (varname), default_shell,
                                      origin, flavor == f_recursive,
                                      (scope == s_global ? NULL
                                       : current_variable_set_list->set),
                                      flocp);
          no_default_sh_exe = 0;
        }
      else
        {
          char *tp = alloc_value;

          alloc_value = allocated_expand_string (newval);

          if (find_and_set_default_shell (alloc_value))
            {
              v = define_variable_in_set (varname, strlen (varname), newval,
                                          origin, flavor == f_recursive,
                                          (scope == s_global ? NULL
                                           : current_variable_set_list->set),
                                          flocp);
              no_default_sh_exe = 0;
            }
          else
            v = lookup_variable (varname, strlen (varname));

          free (tp);
        }
    }
  else
    v = NULL;

  /* If not $SHELL, or if $SHELL points to a program we didn't find,
     just process this variable "as usual".  */
  if (!v)
#endif

  /* If we are defining variables inside an $(eval ...), we might have a
     different variable context pushed, not the global context (maybe we're
     inside a $(call ...) or something.  Since this function is only ever
     invoked in places where we want to define globally visible variables,
     make sure we define this variable in the global set.  */

  v = define_variable_in_set (varname, strlen (varname), newval, origin,
                              flavor == f_recursive || flavor == f_expand,
                              (scope == s_global
                               ? NULL : current_variable_set_list->set),
                              flocp);
  v->append = append;
  v->conditional = conditional;

 done:
  free (alloc_value);
  return v->special ? set_special_var (v, origin) : v;
}

/* Parse P (a null-terminated string) as a variable definition.

   If it is not a variable definition, return NULL and the contents of *VAR
   are undefined, except NAME points to the first non-space character or EOS.

   If it is a variable definition, return a pointer to the char after the
   assignment token and set the following fields (only) of *VAR:
    name        : name of the variable (ALWAYS SET) (NOT NUL-TERMINATED!)
    length      : length of the variable name
    value       : value of the variable (nul-terminated)
    flavor      : flavor of the variable
    conditional : whether it's a conditional assignment
   Other values in *VAR are unchanged.
  */

char *
parse_variable_definition (const char *str, struct variable *var)
{
  const char *p = str;
  const char *end = NULL;

  NEXT_TOKEN (p);
  var->name = (char *)p;
  var->length = 0;
  var->conditional = 0;

  /* Walk through STR until we find a valid assignment operator.  Each time
     through this loop P points to the next character to consider.  */
  while (1)
    {
      const char *start;
      int c = *p++;

      /* If we find a comment or EOS, it's not a variable definition.  */
      if (STOP_SET (c, MAP_COMMENT|MAP_NUL))
        return NULL;

      if (ISBLANK (c))
        {
          /* Variable names can't contain spaces so if this is the second set
             of spaces we know it's not a variable assignment.  */
          if (end)
            return NULL;
          end = p - 1;
          NEXT_TOKEN (p);
          continue;
        }

      /* This is the start of a token.  */
      start = p - 1;

      /* If we see a ? then it could be a conditional assignment. */
      if (c == '?')
        {
          var->conditional = 1;
          c = *p++;
        }

      /* If we found = we're done!  */
      if (c == '=')
        {
          if (!end)
            end = start;
          var->flavor = f_recursive; /* = */
          break;
        }

      if (c == ':')
        {
          if (!end)
            end = start;

          /* We need to distinguish :=, ::=, and :::=, versus : outside of an
             assignment (which means this is not a variable definition).  */
          c = *p++;
          if (c == '=')
            {
              var->flavor = f_simple; /* := */
              break;
            }
          if (c == ':')
            {
              c = *p++;
              if (c == '=')
                {
                  var->flavor = f_simple; /* ::= */
                  break;
                }
              if (c == ':' && *p++ == '=')
                {
                  var->flavor = f_expand; /* :::= */
                  break;
                }
            }
          return NULL;
        }

      /* See if it's one of the other two-byte operators.  */
      if (*p == '=')
        {
          switch (c)
            {
            case '+':
              var->flavor = f_append; /* += */
              break;
            case '!':
              var->flavor = f_shell; /* != */
              break;
            default:
              goto other;
            }

          if (!end)
            end = start;
          ++p;
          break;
        }

    other:
      /* We found a char which is not part of an assignment operator.
         If we've seen whitespace, then we know this is not a variable
         assignment since variable names cannot contain whitespace.  */
      if (end)
        return NULL;

      if (c == '$')
        p = skip_reference (p);

      var->conditional = 0;
    }

  /* We found a valid variable assignment: END points to the char after the
     end of the variable name and P points to the char after the =.  */
  var->length = (unsigned int) (end - var->name);
  var->value = next_token (p);

  return (char *)p;
}

/* Try to interpret LINE (a null-terminated string) as a variable definition.

   If LINE was recognized as a variable definition, a pointer to its 'struct
   variable' is returned.  If LINE is not a variable definition, NULL is
   returned.  */

struct variable *
assign_variable_definition (struct variable *v, const char *line)
{
  char *name;

  if (!parse_variable_definition (line, v))
    return NULL;

  /* Expand the name, so "$(foo)bar = baz" works.  */
  name = alloca (v->length + 1);
  memcpy (name, v->name, v->length);
  name[v->length] = '\0';
  v->name = allocated_expand_string (name);

  if (v->name[0] == '\0')
    O (fatal, &v->fileinfo, _("empty variable name"));

  return v;
}

/* Try to interpret LINE (a null-terminated string) as a variable definition.

   ORIGIN may be o_file, o_override, o_env, o_env_override,
   or o_command specifying that the variable definition comes
   from a makefile, an override directive, the environment with
   or without the -e switch, or the command line.

   See the comments for assign_variable_definition().

   If LINE was recognized as a variable definition, a pointer to its 'struct
   variable' is returned.  If LINE is not a variable definition, NULL is
   returned.  */

struct variable *
try_variable_definition (const floc *flocp, const char *line,
                         enum variable_origin origin, enum variable_scope scope)
{
  struct variable v;
  struct variable *vp;

  if (flocp != 0)
    v.fileinfo = *flocp;
  else
    v.fileinfo.filenm = 0;

  if (!assign_variable_definition (&v, line))
    return 0;

  vp = do_variable_definition (flocp, v.name, v.value, origin, v.flavor,
                               v.conditional, scope);

  free (v.name);

  return vp;
}

/* These variables are internal to make, and so considered "defined" for the
   purposes of warn_undefined even if they are not really defined.  */

struct defined_vars
  {
    const char *name;
    size_t len;
  };

static const struct defined_vars defined_vars[] = {
  { STRING_SIZE_TUPLE ("MAKECMDGOALS") },
  { STRING_SIZE_TUPLE ("MAKE_RESTARTS") },
  { STRING_SIZE_TUPLE ("MAKE_TERMOUT") },
  { STRING_SIZE_TUPLE ("MAKE_TERMERR") },
  { STRING_SIZE_TUPLE ("MAKEOVERRIDES") },
  { STRING_SIZE_TUPLE (".DEFAULT") },
  { STRING_SIZE_TUPLE ("-*-command-variables-*-") },
  { STRING_SIZE_TUPLE ("-*-eval-flags-*-") },
  { STRING_SIZE_TUPLE ("VPATH") },
  { STRING_SIZE_TUPLE ("GPATH") },
  { STRING_SIZE_TUPLE (WARNINGS_NAME) },
  { STRING_SIZE_TUPLE (GNUMAKEFLAGS_NAME) },
  { NULL, 0 }
};

void
warn_undefined (const char *name, size_t len)
{
  if (warn_check (wt_undefined_var))
    {
      const struct defined_vars *dp;
      for (dp = defined_vars; dp->name != NULL; ++dp)
        if (dp->len == len && memcmp (dp->name, name, len) == 0)
          return;

      warning (wt_undefined_var, reading_file,
               ONS (format, 0, _("reference to undefined variable '%.*s'"),
                    (int)len, name));
    }
}

static void
set_env_override (const void *item, void *arg UNUSED)
{
  struct variable *v = (struct variable *)item;
  enum variable_origin old = env_overrides ? o_env : o_env_override;
  enum variable_origin new = env_overrides ? o_env_override : o_env;

  if (v->origin == old)
    v->origin = new;
}

void
reset_env_override ()
{
  hash_map_arg (&global_variable_set.table, set_env_override, NULL);
}

/* Print information for variable V, prefixing it with PREFIX.  */

static void
print_variable (const void *item, void *arg)
{
  const struct variable *v = item;
  const char *prefix = arg;
  const char *origin;

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
  fputs ("# ", stdout);
  fputs (origin, stdout);
  if (v->private_var)
    fputs (" private", stdout);
  if (v->fileinfo.filenm)
    printf (_(" (from '%s', line %lu)"),
            v->fileinfo.filenm, v->fileinfo.lineno + v->fileinfo.offset);
  putchar ('\n');
  fputs (prefix, stdout);

  /* Is this a 'define'?  */
  if (v->recursive && strchr (v->value, '\n') != 0)
    printf ("define %s\n%s\nendef\n", v->name, v->value);
  else
    {
      char *p;

      printf ("%s %s= ", v->name, v->recursive ? v->append ? "+" : "" : ":");

      /* Check if the value is just whitespace.  */
      p = next_token (v->value);
      if (p != v->value && *p == '\0')
        /* All whitespace.  */
        printf ("$(subst ,,%s)", v->value);
      else if (v->recursive)
        fputs (v->value, stdout);
      else
        /* Double up dollar signs.  */
        for (p = v->value; *p != '\0'; ++p)
          {
            if (*p == '$')
              putchar ('$');
            putchar (*p);
          }
      putchar ('\n');
    }
}

static void
print_auto_variable (const void *item, void *arg)
{
  const struct variable *v = item;

  if (v->origin == o_automatic)
    print_variable (item, arg);
}

static void
print_noauto_variable (const void *item, void *arg)
{
  const struct variable *v = item;

  if (v->origin != o_automatic)
    print_variable (item, arg);
}

/* Print all the variables in SET.  PREFIX is printed before
   the actual variable definitions (everything else is comments).  */

static void
print_variable_set (struct variable_set *set, const char *prefix, int pauto)
{
  hash_map_arg (&set->table, (pauto ? print_auto_variable : print_variable),
                (void *)prefix);

  fputs (_("# variable set hash-table stats:\n"), stdout);
  fputs ("# ", stdout);
  hash_print_stats (&set->table, stdout);
  putc ('\n', stdout);
}

/* Print the data base of variables.  */

void
print_variable_data_base (void)
{
  puts (_("\n# Variables\n"));

  print_variable_set (&global_variable_set, "", 0);

  puts (_("\n# Pattern-specific Variable Values"));

  {
    struct pattern_var *p;
    unsigned int rules = 0;

    for (p = pattern_vars; p != 0; p = p->next)
      {
        ++rules;
        printf ("\n%s :\n", p->target);
        print_variable (&p->variable, (void *)"# ");
      }

    if (rules == 0)
      puts (_("\n# No pattern-specific variable values."));
    else
      printf (_("\n# %u pattern-specific variable values"), rules);
  }
}


/* Print all the local variables of FILE.  */

void
print_file_variables (const struct file *file)
{
  if (file->variables != 0)
    print_variable_set (file->variables->set, "# ", 1);
}

void
print_target_variables (const struct file *file)
{
  if (file->variables != 0)
    {
      size_t l = strlen (file->name);
      char *t = alloca (l + 3);

      memcpy (t, file->name, l);
      t[l] = ':';
      t[l+1] = ' ';
      t[l+2] = '\0';

      hash_map_arg (&file->variables->set->table, print_noauto_variable, t);
    }
}


#if MK_OS_W32
void
sync_Path_environment ()
{
  static char *environ_path = NULL;
  char *oldpath = environ_path;
  char *path = allocated_expand_string ("PATH=$(PATH)");

  if (!path)
    return;

  /* Convert the value of PATH into something Windows32 world can grok.
    Note: convert_Path_to_windows32 must see only the value of PATH,
    and see it from its first character, to do its tricky job.  */
  convert_Path_to_windows32 (path + CSTRLEN ("PATH="), ';');

  environ_path = path;
  putenv (environ_path);
  free (oldpath);
}
#endif
