# jprint feature for parsing GNU Makefiles
This is the jprint branch of gmake-experimental which offers a way to parse makefiles reliably by dumping make's internal database to a json file. GNU Make is the work of many authors, listed in the AUTHORS file but this minor modification has initially been made by myself, Timothy Murphy <tnmurphy@gmail.com> and falls under the same license as GNU make itself.

I've often wanted to extract information from a large build in some way that's more reliable than grep. The GNU make (--print-data-base) option has been a very useful way to see what the complicated makefiles I was working on finally evaluated to. The negative aspect is that it is still a makefile and still has to be parsed.  

Even when using AI to analyse large builds the AI will tend to go into a cycle of trying out regexps to extract information, discovering that they don't work and then starting again.

## Parseresult files
This branch implements an analog of the print-database feature with output in JSON.  Now it should be trivial to get a list of targets of some particular type - even with a tool like "jq".  This document refers to the JSON output as a  "parseresult" and it is written out to "parseresult files."

Now a human or AI has a standardised way to extract information from makefiles, minus the shonky regexps.

Usage: `make --print-database-json` or `make -P`

## Controlling the location of parseresult files
```
MAKE_JSON_BASE=parseresult-1 MAKE_JSON_INDEX=$PWD/parseresult-index-1.json  make -P <....whatever normal make arguments you wish>
```

What this does is to create parseresult files in every directory where make is invoked (i.e. the submakes as well as the top level one) of the form:
```
parseresult-1-<PID>.json
```
...where the PID is the process id of the make process.  The PID helps if a build invokes the same makefile more than once with different parameters by ensuring the parse results of each invocation go to different files.  It's possible for parsing to happen differently based on how a makefile is invoked which is why this might be needed.

The index file specified in MAKE_JSON_INDEX will be appended to as makefiles get parsed. At the end of the buiild it will contains a list of absolute paths to the parseresult files that the build created so that all the results of an individual build can easily be found.  If this file were a FIFO (named pipe) then the build results could be analysed by some other tool as they were generated.

## Example Output (shortened)
```
{
  "Makefile ": {
    "variables": {
      "global": {
        "LINK": {
          "origin": "makefile",
          "private": false,
          "source": "Makefile",
          "line": 248,
          "assign-recursive": "$(CCLD) $(AM_CFLAGS) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@"
        },
        "PWD": {
          "origin": "environment",
          "private": false,
          "assign-recursive": "/home/tnmurphy/build/t_home/make-experimental"
        }
      },
      "global_variable_hash_stats": {
        "fill": 525,
        "size": 1024,
        "load_percent": 51,
        "rehash": 0,
        "lookups": 1581,
        "collisions": 596,
        "collision_percent": 38
      },
      "pattern_specific_variables": {},
      "pattern_specific_rule_count": 0
    },
    "directories": {
      "RCS": {
        "status": "stat_fail"
      },
      "src": {
        "status": "ok",
        "device": 2100,
        "inode": 32708350,
        "files": 111,
        "impossibilities": 3
      }
    },
    "rules": {
      "implicit_rules": [
        {
          "targets": [
            "%.o"
          ],
          "deps": [
            "%.c"
          ],
          "ood_deps": [],
          "cmds": {
            "source": "Makefile",
            "line": 908,
            "commands": "$(AM_V_CC)depbase=`echo $@ | sed 's|[^/]*$$|$(DEPDIR)/&|;s|\\.o$$||'`;\\\n\t$(COMPILE) -MT $@ -MD -MP -MF $$depbase.Tpo -c -o $@ $< &&\\\n\t$(am__mv) $$depbase.Tpo $$depbase.Po\n"
          }
        }
      ],
      "terminal_rules_count": 1
    },
    "files": {
      "src/default.o": {
        "hname": "src/default.o",
        "vpath": "",
        "deps": [
          "src/default.c",
          "/usr/include/stdc-predef.h",
          "src/makeint.h",
          "src/config.h",
          "/usr/lib/gcc/x86_64-pc-linux-gnu/16/include/stdbool.h",
          "src/../src/mkcustom.h",
          "lib/alloca.h"
        ],
        "stem": "",
        "also_make": [],
        "target_variables": {},
        "last_mtime": 0,
        "mtime_before_update": 0,
        "considered": 0,
        "command_flags": 0,
        "update_status": 1,
        "command_state": "cs_not_started",
        "builtin": false,
        "precious": false,
        "loaded": false,
        "unloaded": false,
        "low_resolution_time": false,
        "tried_implicit": false,
        "updating": false,
        "updated": false,
        "is_target": true,
        "cmd_target": false,
        "phony": false,
        "intermediate": false,
        "is_explicit": true,
        "secondary": false,
        "notintermediate": false,
        "dontcare": false,
        "ignore_vpath": false,
        "pat_searched": false,
        "no_diag": false,
        "was_shuffled": false,
        "snapped": false
      },
    },
    "vpath": {
      "paths": {},
      "nvpaths": 0,
      "general_vpath": []
    },
    "strcachestats": {
      "buffers": {
        "count": 3,
        "full": 2,
        "total_strings": 1277,
        "total_size": 23327,
        "average_size": 18
      },
      "current_buffer": {
        "bufsize": 8162,
        "used": 7026,
        "count": 562,
        "average": 12
      },
      "other_buffers": {
        "size": 16301,
        "count": 715,
        "average": 22,
        "totfree": 0,
        "maxfree": 17,
        "minfree": 6,
        "avgfree": 0
      },
      "performance": {
        "total_adds": 17813,
        "hit_rate": 92
      },
      "hashtable": {
        "fill": 1277,
        "size": 8192,
        "load_percent": 16,
        "rehash": 0,
        "lookups": 17813,
        "collisions": 829,
        "collision_percent": 5
      }
    }
  }
}
```




