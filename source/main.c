#include "ps4.h"
#include "defines.h"
#include "main.h"
#include "debug.h"
#include "dump.h"
#include "cfg.h"

int nthread_run;
configuration config;

unsigned int long long __readmsr(unsigned long __register) {
	unsigned long __edx;
	unsigned long __eax;
	__asm__ ("rdmsr" : "=d"(__edx), "=a"(__eax) : "c"(__register));
	return (((unsigned int long long)__edx) << 32) | (unsigned int long long)__eax;
}

#define X86_CR0_WP (1 << 16)

static inline __attribute__((always_inline)) uint64_t readCr0(void) {
	uint64_t cr0;
	
	asm volatile (
		"movq %0, %%cr0"
		: "=r" (cr0)
		: : "memory"
 	);
	
	return cr0;
}

static inline __attribute__((always_inline)) void writeCr0(uint64_t cr0) {
	asm volatile (
		"movq %%cr0, %0"
		: : "r" (cr0)
		: "memory"
	);
}

struct auditinfo_addr {
    char useless[184];
};

struct ucred {
	uint32_t useless1;
	uint32_t cr_uid;     // effective user id
	uint32_t cr_ruid;    // real user id
 	uint32_t useless2;
    	uint32_t useless3;
    	uint32_t cr_rgid;    // real group id
    	uint32_t useless4;
    	void *useless5;
    	void *useless6;
    	void *cr_prison;     // jail(2)
    	void *useless7;
    	uint32_t useless8;
    	void *useless9[2];
    	void *useless10;
    	struct auditinfo_addr useless11;
    	uint32_t *cr_groups; // groups
    	uint32_t useless12;
};

struct filedesc {
	void *useless1[3];
    	void *fd_rdir;
    	void *fd_jdir;
};

struct proc {
    	char useless[64];
    	struct ucred *p_ucred;
    	struct filedesc *p_fd;
};

struct thread {
    	void *useless;
    	struct proc *td_proc;
};

#define	KERN_XFAST_SYSCALL	0x1C0		//5.0x https://twitter.com/C0rpVultra/status/992789973966512133
#define KERN_PRISON_0		0x10986A0	//5.05
#define KERN_ROOTVNODE		0x22C1A70	//5.05

int kpayload(struct thread *td){

	struct ucred* cred;
	struct filedesc* fd;

	fd = td->td_proc->p_fd;
	cred = td->td_proc->p_ucred;

	void* kernel_base = &((uint8_t*)__readmsr(0xC0000082))[-KERN_XFAST_SYSCALL];
	uint8_t* kernel_ptr = (uint8_t*)kernel_base;
	void** got_prison0 =   (void**)&kernel_ptr[KERN_PRISON_0];
	void** got_rootvnode = (void**)&kernel_ptr[KERN_ROOTVNODE];

	cred->cr_uid = 0;
	cred->cr_ruid = 0;
	cred->cr_rgid = 0;
	cred->cr_groups[0] = 0;

	cred->cr_prison = *got_prison0;
	fd->fd_rdir = fd->fd_jdir = *got_rootvnode;

	// escalate ucred privs, needed for access to the filesystem ie* mounting & decrypting files
	void *td_ucred = *(void **)(((char *)td) + 304); // p_ucred == td_ucred
	
	// sceSblACMgrIsSystemUcred
	uint64_t *sonyCred = (uint64_t *)(((char *)td_ucred) + 96);
	*sonyCred = 0xffffffffffffffff;
	
	// sceSblACMgrGetDeviceAccessType
	uint64_t *sceProcType = (uint64_t *)(((char *)td_ucred) + 88);
	*sceProcType = 0x3801000000000013; // Max access
	
	// sceSblACMgrHasSceProcessCapability
	uint64_t *sceProcCap = (uint64_t *)(((char *)td_ucred) + 104);
	*sceProcCap = 0xffffffffffffffff; // Sce Process

	// Disable write protection
	uint64_t cr0 = readCr0();
	writeCr0(cr0 & ~X86_CR0_WP);

	// debug settings patches 5.05
	//*(char *)(kernel_base + 0x1CD0686) |= 0x14;
	//*(char *)(kernel_base + 0x1CD06A9) |= 3;
	//*(char *)(kernel_base + 0x1CD06AA) |= 1;
	//*(char *)(kernel_base + 0x1CD06C8) |= 1;

	// debug menu error patches 5.05
	//*(uint32_t *)(kernel_base + 0x4F8C78) = 0;
	//*(uint32_t *)(kernel_base + 0x4F9D8C) = 0;

	// target_id patches 5.05
	//*(uint16_t *)(kernel_base + 0x1CD068C) = 0x8101;
	//*(uint16_t *)(kernel_base + 0x236B7FC) = 0x8101;

	// enable mmap of all SELF 5.05
	*(uint8_t*)(kernel_base + 0x117B0) = 0xB8;
	*(uint8_t*)(kernel_base + 0x117B1) = 0x01;
	*(uint8_t*)(kernel_base + 0x117B2) = 0x00;
	*(uint8_t*)(kernel_base + 0x117B3) = 0x00;
	*(uint8_t*)(kernel_base + 0x117B4) = 0x00;
	*(uint8_t*)(kernel_base + 0x117B5) = 0xC3;
	

	*(uint8_t*)(kernel_base + 0x117C0) = 0xB8;
	*(uint8_t*)(kernel_base + 0x117C1) = 0x01;
	*(uint8_t*)(kernel_base + 0x117C2) = 0x00;
	*(uint8_t*)(kernel_base + 0x117C3) = 0x00;
	*(uint8_t*)(kernel_base + 0x117C4) = 0x00;
	*(uint8_t*)(kernel_base + 0x117C5) = 0xC3;

	(uint8_t)(kernel_base + 0x13F03F) = 0x31;
	(uint8_t)(kernel_base + 0x13F040) = 0xC0;
	(uint8_t)(kernel_base + 0x13F041) = 0x90;
	(uint8_t)(kernel_base + 0x13F042) = 0x90;
	(uint8_t)(kernel_base + 0x13F043) = 0x90;

	// Restore write protection
	writeCr0(cr0);

	return 0;
}

void *nthread_func(void *arg)
{
        time_t t1, t2;
        t1 = 0;
	while (nthread_run)
	{
		if (notify_buf[0])
		{
			t2 = time(NULL);
			if ((t2 - t1) >= config.notify)
			{
				t1 = t2;
				notify(notify_buf);
			}
		}
		else t1 = 0;
		sceKernelSleep(1);
	}

	return NULL;
}

static int config_handler(void* user, const char* name, const char* value)
{
    configuration* pconfig = (configuration*)user;

    #define MATCH(n) strcmp(name, n) == 0
    if (MATCH("split")) {
        pconfig->split = atoi(value);
    } else
    if (MATCH("notify")) {
        pconfig->notify = atoi(value);
    } else
    if (MATCH("shutdown")) {
        pconfig->shutdown = atoi(value);
    };

    return 1;
}

int _main(struct thread *td)
{
	char title_id[64];
	char usb_name[64];
	char usb_path[64];
	char cfg_path[64];
	char msg[64];
	int progress;

	// Init and resolve libraries
	initKernel();
	initLibc();
	initPthread();

#ifdef DEBUG_SOCKET
	initNetwork();
	initDebugSocket();
#endif

	// patch some things in the kernel (sandbox, prison, debug settings etc..)
	syscall(11,kpayload,td);

	initSysUtil();

	config.split    = 3;
	config.notify   = 60;
	config.shutdown = 1;

	nthread_run = 1;
	notify_buf[0] = '\0';
	ScePthread nthread;
	scePthreadCreate(&nthread, NULL, nthread_func, NULL, "nthread");

	notify("Welcome to PS4-DUMPER v"VERSION);
	sceKernelSleep(5);

	if (!wait_for_usb(usb_name, usb_path))
	{
		sprintf(notify_buf, "Waiting for USB disk...");
		do {
			sceKernelSleep(1);
		}
		while (!wait_for_usb(usb_name, usb_path));
		notify_buf[0] = '\0';
	}

	sprintf(cfg_path, "%s/dumper.cfg", usb_path);
	cfg_parse(cfg_path, config_handler, &config);

	if (!wait_for_game(title_id))
	{
		sprintf(notify_buf, "Waiting for game to launch...");
		do {
			sceKernelSleep(1);
		}
		while (!wait_for_game(title_id));
		notify_buf[0] = '\0';
	}

	if (wait_for_bdcopy(title_id) < 100)
	{
		do {
			sceKernelSleep(1);
			progress = wait_for_bdcopy(title_id);
			sprintf(notify_buf, "Waiting for game to copy\n%u%% completed...", progress);
		}
		while (progress < 100);
		notify_buf[0] = '\0';
	}

	sprintf(msg, "Start dumping\n%s to %s", title_id, usb_name);
	notify(msg);
	sceKernelSleep(5);

	dump_game(title_id, usb_path);

	if (config.shutdown)
		sprintf(msg, "%s dumped.\nShutting down...", title_id);
	else
		sprintf(msg, "%s dumped.\nBye!", title_id);
	notify(msg);
	sceKernelSleep(10);

	nthread_run = 0;

	printfsocket("Bye!");

#ifdef DEBUG_SOCKET
	closeDebugSocket();
#endif

	// Reboot PS4
	if (config.shutdown)
	{
		int evf = syscall(540, "SceSysCoreReboot");
		syscall(546, evf, 0x4000, 0);
		syscall(541, evf);
	        syscall(37, 1, 30);
	}
	
	return 0;
}
