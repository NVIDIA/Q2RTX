#!/bin/sh

if [ -f VERSION ]
then
    ver=`cat VERSION`
    rev=`tr -d -c [:digit:] < VERSION`
elif [ -x `which git` -a -d ".git" ]
then
    ver=`git describe --match "r[0-9]*" --abbrev=4 HEAD`
    rev=`git describe --match "r[0-9]*" --abbrev=0 HEAD | tr -d -c [:digit:]`
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

