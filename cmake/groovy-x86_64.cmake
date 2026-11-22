# CMake toolchain for the GroovyArcade (x86_64 Linux) build.
# Compiles against an old glibc (Debian 10 "buster", glibc 2.28) sysroot so the result runs on
# any x86_64 distro with glibc >= 2.28; X11/DRM/EGL/udev headers come from the host
# (symlinked into the sysroot by make_sysroot_glibc228.sh) because SDL only dlopen()s those libraries at runtime.
get_filename_component(_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GLIBC_SYSROOT "${_ROOT}/deps/sysroot-glibc228")
set(_F "--sysroot=${GLIBC_SYSROOT} -O2 -march=x86-64 -mtune=generic")
set(CMAKE_C_FLAGS_INIT   "${_F}")
set(CMAKE_CXX_FLAGS_INIT "${_F}")
# Debian's gcc puts the host lib dir before the sysroot ones: force crt*.o and libc from the sysroot
set(_L "--sysroot=${GLIBC_SYSROOT} -B${GLIBC_SYSROOT}/usr/lib/x86_64-linux-gnu/ -L${GLIBC_SYSROOT}/usr/lib/x86_64-linux-gnu -L${GLIBC_SYSROOT}/lib/x86_64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_L}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_L}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_L}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
