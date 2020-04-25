 clang \
   --target=wasm32 \
   -std=c++14 \
   -O3 \
   -flto \
   -Ofast \
   -nostdlib \
   -Wl,--no-entry \
   -Wl,--allow-undefined \
   -Wl,--initial-memory=$[65536*10] \
   -Wl,--max-memory=$[65536*10] \
   -Wl,--strip-all \
   -Wl,--export-dynamic \
   -Wl,--export=__heap_base \
   -Wl,-z,stack-size=$[1024] \
   -Wl,--stack-first \
   -Wl,--no-merge-data-segments \
   -Wl,--lto-O3 \
   -o "$2" \
   "$1"
