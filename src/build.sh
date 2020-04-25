 clang \
   --target=wasm32 \
   -std=c++14 \
   -O3 \
   -flto \
   -Ofast \
   -nostdlib \
   -Wl,--no-entry \
   -Wl,--relocatable \
   -Wl,--strip-all \
   -Wl,--export-dynamic \
   -Wl,-z,stack-size=$[1024] \
   -Wl,--stack-first \
   -Wl,--no-merge-data-segments \
   -Wl,--lto-O3 \
   -o "$2" \
   "$1"
