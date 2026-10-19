# Only LLVM and Node are needed; no proprietary SDK or console.
$ErrorActionPreference = 'Stop'
$clang = (Get-Command clang -ErrorAction SilentlyContinue).Source
if (!$clang) { $clang = 'C:/Program Files/LLVM/bin/clang.exe' }
$lld = Join-Path (Split-Path $clang) 'lld.exe'
$repo = Split-Path $PSScriptRoot
$out = Join-Path $repo 'src/obj/bt-native-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $clang --target=wasm32 -nostdlib -ffreestanding -fno-builtin -std=c99 -Wall -Wextra -Werror -I (Join-Path $PSScriptRoot 'stubs') -c (Join-Path $PSScriptRoot 'test_bt_native.c') -o (Join-Path $out 'test.o')
if ($LASTEXITCODE) { throw 'Test compilation failed' }
& $lld -flavor wasm --no-entry --export=run_tests (Join-Path $out 'test.o') -o (Join-Path $out 'test.wasm')
if ($LASTEXITCODE) { throw 'Test link failed' }
node -e 'const fs=require("fs"); WebAssembly.instantiate(fs.readFileSync(process.argv[1])).then(({instance})=>{const line=instance.exports.run_tests(); if(line){console.error("FAIL test_bt_native.c:"+line); process.exit(1)} console.log("PASS Bluetooth routing, PS edges, menu state, disconnects and failure paths; no LDD dependencies");}).catch(e=>{console.error(e);process.exit(1)})' (Join-Path $out 'test.wasm')
if ($LASTEXITCODE) { throw 'Bluetooth regression tests failed' }
