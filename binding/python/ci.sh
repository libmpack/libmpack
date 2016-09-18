#!/bin/sh

if [ "$CONFIG" = "python3-binding" ]; then
	PIP=pip3
	PYTHON=python3
elif [ "$CONFIG" = "python2-binding" ]; then
	PIP=pip2
	PYTHON=python2
else
	echo "invalid CI config" 2>&1
	exit 1
fi

$PIP install --upgrade pip
$PIP install cython
$PIP install autopxd
$PYTHON setup.py clean
$PYTHON setup.py build_ext --inplace
$PYTHON test.py
