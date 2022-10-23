rm -f tests/*gcno
rm -f tests/*gcda
gcc -g -Wall -Wextra -O0 \
    --coverage -fno-inline -fno-inline-small-functions -fno-default-inline -dumpbase '' \
    tests/tests.c -o tests/tests.bin && tests/tests.bin 

g++ -g -Wall -Wextra -O0 \
    tests/tests.c -o tests/tests-cpp.bin && tests/tests-cpp.bin 
