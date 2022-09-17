rm -f tests/*gcno
rm -f tests/*gcda
gcc -g -Wall -Wextra -O0 \
    -Wno-missing-field-initializers -Wno-unused-parameter \
    --coverage -fno-inline -fno-inline-small-functions -fno-default-inline -dumpbase '' \
    tests/tests.c -o tests/tests.bin && tests/tests.bin && gcov tests/tests.c
