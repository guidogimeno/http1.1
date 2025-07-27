#!/bin/bash

usage() {
    echo "Usage: $0 [run|build|debug|clean]"
    echo "  run    : Builds the project and then executes it."
    echo "  build  : Compiles the project."
    echo "  debug  : Compiles the project with debugging symbols."
    echo "  clean  : Removes the compiled executable."
    exit 1
}

case "$1" in
    "run")
        if [ -f "main" ]; then
            rm -f "main"
        fi
        clang -O2 -std=c11 -Wall -Werror main.c -o main
        ./main
        ;;
    "build")
        clang -O2 -std=c11 -Wall -Werror main.c -o main
        ;;
    "debug")
        clang -g -std=c11 -Wall -Werror main.c -o main
        gdb -ex "tui enable" -ex "layout src" -ex "break main" ./main
        ;;
    "clean")
        if [ -f "main" ]; then
            rm "main"
        fi
        ;;
    *)
        usage
        ;;
esac

