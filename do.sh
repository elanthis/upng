#!/bin/sh
make || exit 1
./test foo.png foo.tga || exit 1
cmp -s foo.tga out.tga || exit 1 
echo ok
