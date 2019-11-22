#!/bin/sh

echo "Call ReBuildForModuleBuild"

HOSTCFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -fomit-frame-pointer"

if [ ! -e scripts/basic/fixdep ]
then
	echo "Build scripts/basic/fixdep"
	gcc -Wp,-MD,scripts/basic/.fixdep.d $HOSTCFLAGS -o scripts/basic/fixdep scripts/basic/fixdep.c
fi

if [ ! -e scripts/recordmcount ]
then
	echo "Build scripts/recordmcount"
	gcc -Wp,-MD,scripts/.recordmcount.d $HOSTCFLAGS -o scripts/recordmcount scripts/recordmcount.c
fi

if [ ! -e scripts/mod/modpost ]
then
	echo "Build scripts/mod/modpost"
	gcc -Wp,-MD,scripts/mod/.file2alias.o.d $HOSTCFLAGS -c -o scripts/mod/file2alias.o scripts/mod/file2alias.c
	gcc -Wp,-MD,scripts/mod/.modpost.o.d $HOSTCFLAGS -c -o scripts/mod/modpost.o scripts/mod/modpost.c
	gcc -Wp,-MD,scripts/mod/.sumversion.o.d $HOSTCFLAGS -c -o scripts/mod/sumversion.o scripts/mod/sumversion.c
	gcc -o scripts/mod/modpost scripts/mod/modpost.o scripts/mod/file2alias.o scripts/mod/sumversion.o
fi
