SCRIPT_DIR=$(dirname "$0")
RUN_BIN=$SCRIPT_DIR/run.bin
gcc -Wall -Wextra -Werror -Wfatal-errors $SCRIPT_DIR/run.c -o $RUN_BIN -lpthread && $RUN_BIN