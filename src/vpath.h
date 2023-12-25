/* Implementation of pattern-matching file search paths for GNU Make.
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

#ifndef __VPATH_H__
#define __VPATH_H__

#include "filedef.h"
#include "variable.h"
#if MK_OS_W32
#include "pathstuff.h"
#endif


/* Structure used to represent a selective VPATH searchpath.  */

struct vpath
  {
    struct vpath *next;      /* Pointer to next struct in the linked list.  */
    const char *pattern;     /* The pattern to match.  */
    const char *percent;     /* Pointer into 'pattern' where the '%' is.  */
    size_t patlen;           /* Length of the pattern.  */
    const char **searchpath; /* Null-terminated list of directories.  */
    size_t maxlen;           /* Maximum length of any entry in the list.  */
  };

/* Linked-list of all selective VPATHs.  */

extern  struct vpath *vpaths;

/* Structure for the general VPATH given in the variable.  */

extern struct vpath *general_vpath;

/* Structure for GPATH given in the variable.  */

extern struct vpath *gpaths;

#endif

