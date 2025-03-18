/* Directory hashing structures for GNU Make.
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

#include "hash.h"
#include "debug.h"


#ifdef HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
# if MK_OS_VMS
/* its prototype is in vmsdir.h, which is not needed for HAVE_DIRENT_H */
const char *vmsify (const char *name, int type);
# endif
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
# ifdef HAVE_VMSDIR_H
#  include "vmsdir.h"
# endif /* HAVE_VMSDIR_H */
#endif


struct directory_contents
  {
    dev_t dev;                  /* Device and inode numbers of this dir.  */
#if MK_OS_W32
    /* Inode means nothing on Windows32. Even file key information is
     * unreliable because it is random per file open and undefined for remote
     * filesystems. The most unique attribute I can come up with is the fully
     * qualified name of the directory. Beware though, this is also
     * unreliable. I'm open to suggestion on a better way to emulate inode.  */
    char *path_key;
    time_t ctime;
    time_t mtime;        /* controls check for stale directory cache */
    int fs_flags;     /* FS_FAT, FS_NTFS, ... */
# define FS_FAT      0x1
# define FS_NTFS     0x2
# define FS_UNKNOWN  0x4
#else
# if MK_OS_VMS_INO_T
    ino_t ino[3];
# else
    ino_t ino;
# endif
#endif /* MK_OS_W32 */
    struct hash_table dirfiles; /* Files in this directory.  */
    unsigned long counter;      /* command_count value when last read. */
    DIR *dirstream;             /* Stream reading this directory.  */
  };


struct directory
  {
    const char *name;           /* Name of the directory.  */
    unsigned long counter;      /* command_count value when last read.
                                   Used for non-existent directories.  */

    /* The directory's contents.  This data may be shared by several
       entries in the hash table, which refer to the same directory
       (identified uniquely by 'dev' and 'ino') under different names.  */
    struct directory_contents *contents;
  };

/* Hash table of files in each directory.  */

struct dirfile
  {
    const char *name;           /* Name of the file.  */
    size_t length;
    short impossible;           /* This file is impossible.  */
    unsigned char type;
  };

/* Table of directories hashed by name.  */
extern struct hash_table directories;

/* Table of directory contents hashed by device and inode number.  */
extern struct hash_table directory_contents;
