#!/bin/sh

find . -name "*.h" -o -name "*.c"-o -name "*.cc" -o -name "*.cpp" >> cscope.files
cscope -bRkq -i cscope.files
ctags -R --c++-kinds=+cdefgmnpstuv --fields=+iaS --extra=+q

exit 0
