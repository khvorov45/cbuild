set -e # stop on error

echo Main tests
rm -f tests/*gcno
rm -f tests/*gcda
gcc -g -Wall -Wextra -O0 -Dprb_IMPLEMENTATION \
    --coverage -fno-inline -fno-inline-small-functions -fno-default-inline -dumpbase '' \
    tests/tests.c -o tests/tests.bin  
tests/tests.bin 

echo
echo C++ compatability
g++ -g -Wall -Wextra -O0 -Dprb_IMPLEMENTATION \
    tests/tests.c -o tests/tests-cpp.bin
tests/tests-cpp.bin 

echo
echo Two translation units
gcc -g -Wall -Wextra -O0 -c tests/precompile-2tu.c -o tests/precompile-2tu.obj
gcc -g -Wall -Wextra -O0 tests/tests.c tests/precompile-2tu.obj -o tests/tests-2tu.bin
tests/tests-2tu.bin 

echo
echo All tests passed
