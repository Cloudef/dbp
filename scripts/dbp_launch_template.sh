#!/bin/bash
#dbp-template

# The "dbp-template" line must not be altered nor be moved, it must be the second
# line in the file for proper executable nuking when dbpd starts.


# Wrapper script that simulates a binary of the desired program inside $PATH
# For use with the dbp system

# anything surrounded by an ! followed by % is replaced with
# actual values by dbpd when the special folder is populated

pyra-hspkg-run !%package_id! !%package_binary! --args "$@"
