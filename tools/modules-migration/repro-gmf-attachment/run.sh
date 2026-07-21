#!/bin/bash
# Minimal reproducer for the "declaration attached to named module X cannot be attached to other
# modules" failure that parked the srctrl.messaging slice (clang 22.1.8, Homebrew LLVM, macOS).
# See README.md for the analysis. Expected: steps 1-6 succeed, step 7 (m:f) FAILS.
set -u
CXX=${CXX:-/opt/homebrew/opt/llvm/bin/clang++}
F="-std=c++23"
run() { echo "+ $*"; "$@"; }
run $CXX $F --precompile -x c++-module a_s.cppm -o a-s.pcm || exit 1
run $CXX $F --precompile -x c++-module a.cppm -fmodule-file=a:s=a-s.pcm -o a.pcm || exit 1
run $CXX $F --precompile -x c++-module b2.cppm -o b2.pcm || exit 1
echo "== contrast 1: plain consumer of a+b2, USES trim -- accepted (ill-formed NDR, no diagnostic)"
run $CXX $F --precompile -x c++-module c4.cppm -fmodule-file=a=a.pcm -fmodule-file=a:s=a-s.pcm -fmodule-file=b2=b2.pcm -o c4.pcm
echo "== contrast 2: carrier partition WITHOUT 'import a' -- accepted"
run $CXX $F --precompile -x c++-module m_t2.cppm -o m-t2.pcm
run $CXX $F --precompile -x c++-module m_f.cppm -fmodule-file=a=a.pcm -fmodule-file=a:s=a-s.pcm -fmodule-file=m:t=m-t2.pcm -o m-f2.pcm
echo "== THE FAILING SHAPE: carrier partition m:t has GMF textual parse of s.h AND imports a;"
echo "== sibling partition m:f imports :t and a -> attachment clash diagnosed here"
run $CXX $F --precompile -x c++-module m_t.cppm -fmodule-file=a=a.pcm -fmodule-file=a:s=a-s.pcm -o m-t.pcm
run $CXX $F --precompile -x c++-module m_f.cppm -fmodule-file=a=a.pcm -fmodule-file=a:s=a-s.pcm -fmodule-file=m:t=m-t.pcm -o m-f.pcm
echo "exit=$? (expected: 1 with 'declaration ''trim'' attached to named module ''a:s'' cannot be attached to other modules')"
