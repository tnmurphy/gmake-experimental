#!/usr/bin/bash

# Run some makefiles using the json print (-P) option and check the
# json output in a fairly trivial way

CDIR=$(dirname $BASH_SOURCE)

OUT=$CDIR/out
if [[ ! -d $OUT ]]; then
  mkdir $CDIR/$OUT
fi

# Set the base name of the json output from Make
export MAKE_JSON_BASE=$OUT/test_json1
rm $OUT/*.json $OUT/*.idx

# Run make without builtin rules or variables to keep the files simpler
$CDIR/../../make -P -R -r -f json1.mk

./validate.py $MAKE_JSON_BASE*.json
