#!/bin/sh
# Interactive prompt: after the stage build, ask whether to compile the kernel.
# Usage: prompt_kernel.sh <kernel-dir>
# Answer Y/yes to build the kernel now, anything else to skip it.

KERNEL_DIR="${1:?usage: prompt_kernel.sh <kernel-dir>}"

printf "Would you like to compile the kernel now? Y [Yes] N [No] "
if ! read -r ANS; then
    echo
    echo "Skipping kernel build."
    exit 0
fi

case "$ANS" in
    Y|y|Yes|YES|yes)
        printf "Are you sure you want to compile the kernel? Y [Yes] N [No] "
        if ! read -r CONFIRM; then
            echo
            echo "Skipping kernel build."
            exit 0
        fi
        case "$CONFIRM" in
            Y|y|Yes|YES|yes)
                echo "Compiling the kernel..."
                make -C "$KERNEL_DIR"
                ;;
            *)
                echo "Skipping kernel build."
                ;;
        esac
        ;;
    *)
        printf "Are you sure you want to not compile the kernel? Y [Yes] N [No] "
        if read -r CONFIRM; then
            case "$CONFIRM" in
                Y|y|Yes|YES|yes)
                    echo "Skipping kernel build."
                    ;;
                *)
                    echo "Compiling the kernel..."
                    make -C "$KERNEL_DIR"
                    ;;
            esac
        else
            echo
            echo "Skipping kernel build."
        fi
        ;;
esac
