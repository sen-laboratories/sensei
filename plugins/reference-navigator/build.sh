#!/bin/bash
#
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.

# a simple wrapper for make, since I found no way to add this to the Makefile template
# adds the attributes from the resource: the server finds a plugin by its signature and reads its type and features from the attributes.
mkdir -p bin && make && \
rc Resources.rdef && \
resattr -o bin/SenReferenceNavigator Resources.rsrc && ../../tools/set-version-attr.sh Resources.rdef bin/SenReferenceNavigator

# in any case, clean up, but don't fail if file does not exist
rm -f Resources.rsrc
