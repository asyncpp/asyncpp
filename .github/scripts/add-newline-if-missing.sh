#!/bin/bash
if [[ -f "$1" && -s "$1" ]]; then
    if [[ -n "$(tail -c 1 "$1")" ]]; then
        echo "Fixed missing newline in file $1"
        sed -i -e '$a\' $1
    fi
fi