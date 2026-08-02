
VAR1_IMMEDIATE:=$@
VAR2_DEFERRED=$@

target1:
	echo arg1 arg2 -o $@
	echo VAR1=$(VAR1_IMMEDIATE)
	echo VAR2=$(VAR2_DEFERRED) 

target2:
	echo arg1 arg2 -o $@


