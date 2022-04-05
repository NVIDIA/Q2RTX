#!/bin/sh

if [ -f VERSION ]
then
    ver="`cat VERSION`"
    rev="`sed -e 's/^r\([0-9]\+\).*$/\1/' VERSION`"
elif [ -x "`which git`" -a "`git rev-parse --is-inside-work-tree 2>/dev/null`" = "true" ]
then
    rev="`git rev-list --count HEAD`"
    ver="r$rev~`git rev-parse --short HEAD`"
else
    echo "WARNING: Couldn't detect Q2PRO version." >&2
    ver="r666"
    rev="666"
fi

case $1 in
--version|-v)
    echo $ver;;
--revision|-r)
    echo $rev;;
*)
    echo $ver
    echo $rev;;
esac
