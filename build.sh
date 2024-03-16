#!/bin/sh
GCC="gcc -Wall -Werror -Wextra -Wpedantic -Wstrict-prototypes -std=gnu11 -o "
BIN="./bin/"

mkdir ${BIN}
clear

rm ${BIN}manager

$GCC ${BIN}manager manager.c -lreadline

${BIN}manager