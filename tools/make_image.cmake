# Chicago-95 image assembly (invoked by CMake as `cmake -P`).
#
# Usage:
#   cmake -DIMG_OUT=<path> -DSTAGE_BIN_DIR=<dir> -DKERNEL_BIN=<path> -P make_image.cmake
#
# Layout on disk (512-byte sectors):
#   LBA 0x0000:      Stage1
#   LBA 0x0001+:     Stage2
#   LBA 0x0400:      Stage3
#   LBA 0x0430:      Stage4
#   LBA 0x0800-0x0DF0: Stages 5-100 (16 sectors each)
#   LBA 0x1000:      Kernel
#   All remaining space padded to 1 GB.

if(NOT DEFINED IMG_OUT OR NOT DEFINED STAGE_BIN_DIR OR NOT DEFINED KERNEL_BIN)
    message(FATAL_ERROR
        "Usage: cmake -DIMG_OUT=<path> -DSTAGE_BIN_DIR=<dir> "
        "-DKERNEL_BIN=<path> -P make_image.cmake")
endif()

set(IMG_SIZE 1073741824)   # 1 GB

function(emit_stage bin seek)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E rm -f "${IMG_OUT}")
    execute_process(
        COMMAND dd if=${bin} of=${IMG_OUT} bs=512 seek=${seek}
                count=1 conv=notrunc
        RESULT_VARIABLE _rc)
endfunction()

# Rebuild the image from scratch each time.
execute_process(COMMAND ${CMAKE_COMMAND} -E remove "${IMG_OUT}")

# Stage 1 at LBA 0.
execute_process(
    COMMAND dd if=${STAGE_BIN_DIR}/stage1.bin of=${IMG_OUT} bs=512
            seek=0 count=1 conv=notrunc
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "dd stage1 failed: ${_rc}")
endif()

# Stage 2 at LBA 1.
execute_process(
    COMMAND dd if=${STAGE_BIN_DIR}/stage2.bin of=${IMG_OUT} bs=512
            seek=1 conv=notrunc
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "dd stage2 failed: ${_rc}")
endif()

# Stage 3 at LBA 0x400 (1024).
execute_process(
    COMMAND dd if=${STAGE_BIN_DIR}/stage3.bin of=${IMG_OUT} bs=512
            seek=1024 conv=notrunc
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "dd stage3 failed: ${_rc}")
endif()

# Stage 4 at LBA 0x430 (1072).
execute_process(
    COMMAND dd if=${STAGE_BIN_DIR}/stage4.bin of=${IMG_OUT} bs=512
            seek=1072 conv=notrunc
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "dd stage4 failed: ${_rc}")
endif()

# Stages 5-100 at LBA 0x800 (2048) + (n - 5) * 16.
foreach(n RANGE 5 100)
    math(EXPR lba "2048 + (${n} - 5) * 16")
    set(bin "${STAGE_BIN_DIR}/stage${n}.bin")
    execute_process(
        COMMAND dd if=${bin} of=${IMG_OUT} bs=512 seek=${lba}
                conv=notrunc
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "dd stage${n} failed: ${_rc}")
    endif()
endforeach()

# Kernel at LBA 0x1000 (4096). Optional: skipped when the user declined to
# build the kernel in the interactive prompt.
if(EXISTS "${KERNEL_BIN}")
    execute_process(
        COMMAND dd if=${KERNEL_BIN} of=${IMG_OUT} bs=512 seek=4096
                conv=notrunc
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "dd kernel failed: ${_rc}")
    endif()
else()
    message(WARNING "kernel.bin not found (kernel build skipped) -- image has no kernel")
endif()

# Pad to exactly 1 GB.
execute_process(
    COMMAND truncate -s ${IMG_SIZE} "${IMG_OUT}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "truncate failed: ${_rc}")
endif()

message(STATUS "chicago95.bin assembled (padded to 1 GB)")
