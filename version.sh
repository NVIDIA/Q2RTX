#!/bin/sh

if [ -f VERSION ]
then
    ver="`cat VERSION`"
    rev="`sed -e 's/^r\([0-9]\+\).*$/\1/' VERSION`"
elif [ -x "`which git`" -a -d ".git" ]
then
    rev="`git rev-list HEAD | wc -l | tr -d -c 0-9`"
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

