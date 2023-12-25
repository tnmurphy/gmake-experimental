#!/usr/bin/env python3

"""
A module to do some rough validation on the json output
from gmake -P - for use in tests as a sanity check more than
for testing specific features

Returns 0 if all files are ok
OUTPUTS the number of OK files and the total number of files
"""

import sys
import json


if __name__ == "__main__":
    errors = 0
    files_ok = 0
    for f in sys.argv[1:]:
        try:
            with open(f, "r", encoding="utf-8") as fh:
                j = json.load(fh)
            files_ok += 1
        except json.decoder.JSONDecodeError as e:
            print(f"parse error {f}: {e}",file=sys.stderr)
            errors += 1
        except FileNotFoundError as e:
            print(f"cannot open {f}: {e}",file=sys.stderr)
            errors += 1
    print(f"{files_ok} {files_ok+errors}")
    sys.exit(errors)
