NumOS third_party bundle

Repo version: v0.0.0

This directory keeps source bundles that support the NumOS build and research
workflow without changing the current freestanding runtime implementation.

Contents

1. c_standard_lib
   Extracted from the user supplied archive:
   C:\Users\quite\Downloads\c_standard_lib-master.zip

   This is the P.J. Plauger Standard C Library source tree mirrored by the
   referenced GitHub project. NumOS does not link this tree into the current
   kernel or user runtime by default. It is included here as source material
   for future libc expansion work.

   Read third_party/c_standard_lib/README.TXT before redistributing or linking
   the code into other deliverables. That bundled notice carries the upstream
   rights and limits text.

2. toolchains/source
   Downloaded with:
   bash tools/build-cross-compiler.sh --fetch-only --source-dir third_party/toolchains/source

   The directory contains the GCC 13.2.0 and binutils 2.41 source archives
   used by the cross compiler helper.
