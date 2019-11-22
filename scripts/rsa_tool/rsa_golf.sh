#!/bin/sh

cd scripts/rsa_tool/
make clean
make
cd ../../
echo "=============================================="
if [ -e ~/rsa_gen_golf ]
then
	echo "RSA generation"
	~/rsa_gen_golf $1 $2
	cat $3 >> $2
else
	echo "rsa generator is not found!"
	scripts/rsa_tool/rsa_tool $1 $2
	cat $3 >> $2
fi
echo "=============================================="

