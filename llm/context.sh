#!/bin/bash

clear

echo "> I am working on a hardware accelerator project. Here are my goals:"
echo ""
cat docs/goals.md
echo ""
echo "> Here is the project structure (note that the QEMU device is in the"
echo "> QEMU source tree, not here):"
tree --gitignore --noreport .
echo ""
echo "> Here is an overview of the development environment/workflow:"
cat docs/workflow.md
echo ""
cat llm/rules.md
echo ""
