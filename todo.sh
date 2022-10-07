#! /usr/bin/bash

grep -Er 'TODO|\bBUG\b|FIXME|HACK' --color -n --exclude=todo.sh --exclude=shaders/.*/bin/.* ./shaders ./src ./app/src/main
