set -e # stop on error

echo Main tests
rm -f tests/*gcno
rm -f tests/*gcda
gcc -g -Wall -Wextra -Werror -Wfatal-errors -O0 \
    --coverage -fno-inline -fno-inline-small-functions -fno-default-inline -dumpbase '' \
    tests/tests.c -o tests/tests.bin -lpthread
tests/tests.bin 

echo
echo C++ compatability
g++ -g -Wall -Wextra -Werror -O0 tests/tests.c -o tests/tests-cpp.bin -lpthread
tests/tests-cpp.bin 

echo
echo Two translation units
gcc -g -Wall -Wextra -Werror -O0 -c tests/precompile-2tu.c -o tests/precompile-2tu.obj
gcc -g -Wall -Wextra -Werror -O0 -Dprb_NO_IMPLEMENTATION tests/tests.c tests/precompile-2tu.obj -o tests/tests-2tu.bin -lpthread
tests/tests-2tu.bin 

echo
echo Clang C
clang -g -Wall -Wextra -Werror -O0 tests/tests.c -o tests/tests-clang-c.bin -lpthread
tests/tests-clang-c.bin 

echo
echo Clang C++
clang++ -x c++ -g -Wall -Wextra -Werror -O0 tests/tests.c -o tests/tests-clang-cpp.bin -lpthread
tests/tests-clang-cpp.bin 

echo
echo All tests passed
