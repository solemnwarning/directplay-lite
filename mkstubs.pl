#!/usr/bin/perl
# DirectPlay Lite - Hook DLL stub function generator
# Copyright (C) 2018 Daniel Collins <solemnwarning@solemnwarning.net>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

use strict;
use warnings;

if((scalar @ARGV) != 1)
{
	print STDERR "Usage: $0 <dll-name.dll> < func-names.txt > dll-name.asm\n";
	exit(42); # EX_USAGE
}

my ($dllname) = @ARGV;

print <<EOF;
.586
.MODEL FLAT, C

EXTERN find_sym\@8: PROC

.DATA

dll_name db "$dllname", 0

EOF

while(defined(my $func = <STDIN>))
{
	$func =~ s/[\r\n]//g;
	next if($func eq "");
	
	print <<EOF;
.DATA

${func}_sym  db "${func}", 0
${func}_addr dd 0

.CODE

PUBLIC ${func}
${func} PROC
	cmp   ${func}_addr, 0
	jne   ${func}_jmp
	push  offset ${func}_sym
	push  offset dll_name
	call  find_sym\@8
	mov   ${func}_addr, eax
	${func}_jmp:
	jmp [${func}_addr]
${func} ENDP

EOF
}

print "END\n";
