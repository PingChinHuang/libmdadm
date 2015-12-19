#!/bin/sh

find . -name "*.h" -o -name "*.c"-o -name "*.cc" -o -name "*.cpp" > cscope.files
cscope -bRkq -i cscope.files
ctags -R

exit 0
