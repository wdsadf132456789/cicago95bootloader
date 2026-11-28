# Chicago-95 UEFI FAT32 image assembly (invoked by CMake as `cmake -P`).
#
# Usage:
#   cmake -DUEFI_IMG=<path> -DUEFI_STAGE_DIR=<dir> -DUEFI_EFI=<path>
#         -DKERNEL_ELF=<path> -DMODULE_PAYLOAD_DIR=<dir> -DMODULE_COUNT=<n>
#         -DTRUNCATE=<path> -DMKFS_VFAT=<path> -DMCOPY=<path>
#         -P make_uefi_image.cmake
#
# The resulting image is a plain FAT32 filesystem containing:
#   EFI/BOOT/BOOTX64.EFI   UEFI application
#   KERNEL.ELF             ELF64 kernel image loaded by the UEFI application
#   MODULEn.BIN            optional boot modules (MOD1 header validated)

if(NOT DEFINED UEFI_IMG OR NOT DEFINED UEFI_STAGE_DIR OR NOT DEFINED UEFI_EFI
   OR NOT DEFINED KERNEL_ELF OR NOT DEFINED MODULE_PAYLOAD_DIR
   OR NOT DEFINED MODULE_COUNT OR NOT DEFINED TRUNCATE
   OR NOT DEFINED MKFS_VFAT OR NOT DEFINED MCOPY)
    message(FATAL_ERROR
        "Usage: cmake -DUEFI_IMG=<path> -DUEFI_STAGE_DIR=<dir> "
        "-DUEFI_EFI=<path> -DKERNEL_ELF=<path> -DMODULE_PAYLOAD_DIR=<dir> "
        "-DMODULE_COUNT=<n> -DTRUNCATE=<path> "
        "-DMKFS_VFAT=<path> -DMCOPY=<path> -P make_uefi_image.cmake")
endif()

if(NOT EXISTS "${KERNEL_ELF}")
    message(FATAL_ERROR "KERNEL.ELF not found: ${KERNEL_ELF}")
endif()

set(UEFI_IMG_SIZE 67108864)  # 64 MiB

file(REMOVE "${UEFI_IMG}")
file(REMOVE_RECURSE "${UEFI_STAGE_DIR}")
file(MAKE_DIRECTORY "${UEFI_STAGE_DIR}/EFI/BOOT")

configure_file("${UEFI_EFI}" "${UEFI_STAGE_DIR}/EFI/BOOT/BOOTX64.EFI" COPYONLY)
configure_file("${KERNEL_ELF}" "${UEFI_STAGE_DIR}/KERNEL.ELF" COPYONLY)

math(EXPR _module_last "${MODULE_COUNT} - 1")
foreach(i RANGE ${_module_last})
    set(payload "${MODULE_PAYLOAD_DIR}/module_${i}.bin")
    if(EXISTS "${payload}")
        configure_file("${payload}" "${UEFI_STAGE_DIR}/MODULE${i}.BIN" COPYONLY)
    endif()
endforeach()

execute_process(
    COMMAND "${TRUNCATE}" -s ${UEFI_IMG_SIZE} "${UEFI_IMG}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "truncate failed: ${_rc}")
endif()

execute_process(
    COMMAND "${MKFS_VFAT}" -F 32 -s 1 -n CHICAGO95 "${UEFI_IMG}"
    RESULT_VARIABLE _rc
    OUTPUT_QUIET
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "mkfs.vfat failed: ${_rc}: ${_err}")
endif()

file(GLOB _entries "${UEFI_STAGE_DIR}/*")
execute_process(
    COMMAND "${MCOPY}" -i "${UEFI_IMG}" -s ${_entries} "::"
    RESULT_VARIABLE _rc
    OUTPUT_QUIET
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "mcopy failed: ${_rc}: ${_err}")
endif()

message(STATUS "UEFI FAT32 image assembled: ${UEFI_IMG}")
