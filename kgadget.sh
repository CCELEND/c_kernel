#!/bin/bash

if [ -f 'vmlinux' ];then

	echo -e "\033[32m\033[1mkernel======================================================\033[0m"
	strings vmlinux | grep "Linux version"

	echo -e "\033[32m\033[1minit_cred===================================================\033[0m"
	objdump -t vmlinux | grep " init_cred"

	echo -e "\033[32m\033[1mcommit_creds================================================\033[0m"
	objdump -t vmlinux | grep " commit_creds"

	echo -e "\033[32m\033[1mprepare_kernel_cred=========================================\033[0m"
	objdump -t vmlinux | grep " prepare_kernel_cred"

	echo -e "\033[32m\033[1mswapgs_restore_regs_and_return_to_usermode==================\033[0m"
	objdump -t vmlinux | grep "swapgs_restore_regs_and_return_to_usermode"
	echo -e "\033[36m\033[1m============================================================\033[0m"
	objdump -M intel -d vmlinux | grep "<swapgs_restore_regs_and_return_to_usermode>:" -A 20
	objdump -M intel -d vmlinux | grep "<__irqentry_text_end>:" -A 20

	echo -e "\033[32m\033[1mnative_write_cr4============================================\033[0m"
	objdump -t vmlinux | grep " native_write_cr4"

	echo -e "\033[32m\033[1mwork_for_cpu_fn=============================================\033[0m"
	objdump -t vmlinux | grep " work_for_cpu_fn"

	echo -e "\033[32m\033[1muser_free_payload_rcu=======================================\033[0m"
	objdump -t vmlinux | grep " user_free_payload_rcu"

	echo -e "\033[32m\033[1mptm_unix98_ops==============================================\033[0m"
	objdump -t vmlinux | grep "ptm_unix98_ops"

	echo -e "\033[32m\033[1mpty_unix98_ops==============================================\033[0m"
	objdump -t vmlinux | grep "pty_unix98_ops"

	echo -e "\033[32m\033[1msingle_start================================================\033[0m"
	objdump -t vmlinux | grep " single_start"

	echo -e "\033[32m\033[1mpop register ret============================================\033[0m"
	ROPgadget --binary vmlinux --only 'ret|pop' | grep ': pop rdi ; ret$' &
	ROPgadget --binary vmlinux --only 'ret|pop' | grep ': pop rsi ; ret$' &
	ROPgadget --binary vmlinux --only 'ret|pop' | grep ': pop rdx ; ret$' &
	ROPgadget --binary vmlinux --only 'ret|pop' | grep ': pop rcx ; ret$' &
	ROPgadget --binary vmlinux --only 'ret|pop' | grep ': pop rax ; ret$' &
	ROPgadget --binary vmlinux --only 'ret|pop' | grep ': pop rsp ; ret$' &
	wait

	echo -e "\033[32m\033[1mmov rsp rax=================================================\033[0m"
	ROPgadget --binary vmlinux --only 'pop|mov|jmp|dec' | grep 'mov rsp, rax'

	echo -e "\033[32m\033[1mmov rdi rax=================================================\033[0m"
	ROPgadget --binary vmlinux --only 'pop|mov|call' | grep ': mov rdi, rax' | grep -v 'call 0xffffffff'

	echo -e "\033[32m\033[1mswapgs======================================================\033[0m"
	ROPgadget --binary vmlinux --only 'pop|swapgs|ret|popfq' | grep 'swapgs ;'

	echo -e "\033[32m\033[1miretq=======================================================\033[0m"
	# ropper -f vmlinux --search "iretq;"

else
	echo -e "\033[31m\033[1m[-] $libcname: No such file.\033[0m"
fi
