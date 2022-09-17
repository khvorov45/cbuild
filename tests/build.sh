gcc -g -Wall -Wextra \
    -Wno-missing-field-initializers -Wno-unused-parameter \
    tests/tests.c -o tests/tests.bin && tests/tests.bin
