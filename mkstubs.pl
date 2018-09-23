#!/usr/bin/perl

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
