#!/bin/bash

# note: some of these tests require German locale

DIR=parsertest

for v in $DIR/*.in ; do
   name=`echo $v | sed -e 's#\.in$##g'|sed -e "s#$DIR/##"`
   if [ -e $DIR/$name.out ] ; then
      ./ttxacq.pl -verify $v > out.verify
      cmp -s $DIR/$name.out out.verify
      if [ $? == 0 ] ; then
         echo "OK:   $v"
      else
         echo "FAIL: $v"
      fi
      rm -f out.verify
   else
      echo "SKIP: $v"
   fi
done
