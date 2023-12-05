#!/bin/sh
qemu-system-x86_64 \
    -m 256M \
    -kernel ./bzImage \
    -initrd ./rootfs_exp.cpio \
    -monitor /dev/null \
    -append "root=/dev/ram rdinit=/sbin/init console=ttyS0 quiet kaslr" \
    -smp cores=2,threads=1 \
    -nographic \
    -s \
    -cpu kvm64,+smep,+smap \

    #double_fetch-kaslr
    # -cpu kvm64,+smep \

    #bypass_kpti-nokaslr; userfaultfd-kaslr; re2dir-nokaslr
    #tty-kaslr; pt_regs-nokaslr
    # -cpu kvm64,+smep,+smap \

    #bypass_smep+smap-nokaslr
    # -cpu qemu64-v1,+smep,+smap \

    #ret2usr-nokaslr; seq_ops-kaslr; pipe-kaslr
    # -cpu qemu64-v1 \