#!/bin/bash

NM=riscv64-unknown-elf-nm

KERNEL=kernel/kernel
OUT=kernel/symbols.c

$NM $KERNEL | grep " T " | sort -k1 |awk '
BEGIN {
    print "#include \"types.h\""
    print ""
    print "struct symbol {"
    print "  uint64 addr;"
    print "  char *name;"
    print "};"
    print ""
    print "struct symbol symbols[] = {"
}
{
    printf("  {0x%s, \"%s\"},\n", $1, $3)
}
END {
    print "};"
    print ""
    print "int nsymbols = sizeof(symbols)/sizeof(symbols[0]);"
}
' > $OUT