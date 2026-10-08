#!/bin/bash
#
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.

# a simple wrapper for make, since I found no way to add this to the Makefile template
# adds some needed attributes from the resource, since we need to query for the META:TYPE.
make && \
rc Resources.rdef && \
resattr -o bin/SenMimeExtractor Resources.rsrc

# in any case, clean up, but don't fail if file does not exist
rm -f Resources.rsrc
