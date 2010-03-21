#!/bin/bash
#
# This script is used to verify the grabber by running it on a number of
# input files (*.in) and comparing the output with files (*.out) which are
# also part of the distribution. This is used to check for regressions in
# new releases.
#
# Some *.in files document yet unimplemented parser features (see the
# comments at the top of all input files.)  For these no output files
# exist yet and verdict "SKIP" is reported by the script.
#
# Note: some of these tests require German locale for case conversion.
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#  Copyright 2006-2008 by Tom Zoerner (tomzo at users.sf.net)
#
# $Id: verify.sh,v 1.5 2010/03/21 20:51:13 tom Exp $
#

DIR=parsertest
EXE=./tv_grab_ttx.pl

for v in $DIR/*.in ; do
   name=`echo $v | sed -e 's#\.in$##g'|sed -e "s#$DIR/##"`
   if [ -e $DIR/$name.out ] ; then
      $EXE -verify $v > out.verify
      if [ $? == 0 ] ; then
         if [ -e $DIR/$name.in2 ] ; then
            mv out.verify out.verify1
            $EXE -merge out.verify1 -verify $DIR/$name.in2 > out.verify
            rm -f out.verify1
         fi
         cmp -s $DIR/$name.out out.verify
         if [ $? == 0 ] ; then
            echo "OK:   $v"
         else
            echo "FAIL: $v"
         fi
      else
         echo "INCONC: $v (exit status $?)"
      fi
      rm -f out.verify
   else
      echo "SKIP: $v"
   fi
done
