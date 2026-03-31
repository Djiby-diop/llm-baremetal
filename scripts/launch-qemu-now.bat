@echo off
"C:\Program Files\qemu\qemu-system-x86_64.exe" ^
  -accel tcg ^
  -drive "if=pflash,format=raw,readonly=on,file=C:\Program Files\qemu\share\edk2-x86_64-code.fd" ^
  -drive "format=raw,file=C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\llm-baremetal-boot.img" ^
  -machine pc ^
  -m 2048M ^
  -cpu qemu64 ^
  -smp 2 ^
  -display sdl ^
  -serial stdio ^
  -monitor none
