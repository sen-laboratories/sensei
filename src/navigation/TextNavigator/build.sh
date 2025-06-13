#!/bin/bash

# a simple wrapper for make, since I found no way to add this to the Makefile template
# adds some needed attributes from the resource, since we need to query for the SEN:TYPE.
make && \
rc Resources.rdef && \
resattr -o bin/SenTextNavigator Resources.rsrc && \

# in any case, clean up
rm Resources.rsrc
