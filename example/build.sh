SCRIPT_DIR=$(dirname "$0")
RUN_BIN=$SCRIPT_DIR/build.bin
gcc -g -Wall -Wextra -Werror -Wfatal-errors $SCRIPT_DIR/build.c -o $RUN_BIN -lpthread && $RUN_BIN $@
