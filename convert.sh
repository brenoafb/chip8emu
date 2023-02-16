#!/bin/sh

sed 's/0x//g' | tr -d ' ' | xxd -r -p
