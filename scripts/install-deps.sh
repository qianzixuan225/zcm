#!/bin/bash

STRICT=true
SINGLE_MODE=false
while getopts "is" opt; do
    case $opt in
        i) STRICT=false ;;
        s) SINGLE_MODE=true ;;
        \?) exit 1 ;;
    esac
done

PKGS=''
PIP_PKGS=''

## Waf dependencies
PKGS+='pkg-config '

## Basic C compiler dependency
PKGS+='build-essential '

## Lib ZMQ
PKGS+='libzmq3-dev '

## Java
PKGS+='default-jdk default-jre '

## Node
PKGS+='nodejs nodejs-legacy npm '

## Python
PKGS+='python python-pip '
PIP_PKGS+='Cython '

## LibElf
PKGS+='libelf-dev libelf1 '

## CxxTest
PKGS+='cxxtest '

## Clang tools for code sanitizers, style checkers, etc.
PKGS+='clang '

## Julia
PKGS+='julia '

echo "Updating apt repos"
sudo apt-get update
ret=$?
if [[ $ret -ne 0 && "$STRICT" == "true" ]]; then
    echo "Failed to update"
    exit $ret
fi

echo "Installing from apt"
if $SINGLE_MODE; then
    for pkg in $PKGS; do
        echo "Installing $pkg"
        sudo apt-get install -yq $pkg
        ret=$?
        if [[ $ret -ne 0 && "$STRICT" == "true" ]]; then
            echo "Failed to install packages"
            exit $ret
        fi
    done
else
    sudo apt-get install -yq $PKGS
fi

sudo pip install --upgrade pip
ret=$?
if [[ $ret -ne 0 && "$STRICT" == "true" ]]; then
    echo "Failed to upgrade pip"
    exit $ret
fi

sudo pip install $PIP_PKGS
ret=$?
if [[ $ret -ne 0 && "$STRICT" == "true" ]]; then
    echo "Failed to install pip packages"
    exit $ret
fi

echo "Updating db"
sudo updatedb
ret=$?
if [[ $ret -ne 0 && "$STRICT" == "true" ]]; then
    echo "Failed to updatedb"
    exit $ret
fi

exit 0
