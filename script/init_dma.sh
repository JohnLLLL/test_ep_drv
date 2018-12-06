#!/bin/sh

mknod -m 0666 /dev/vep_bar0 c `cat /proc/devices | grep pmc | sed 's/[a-z].*//'` 0

