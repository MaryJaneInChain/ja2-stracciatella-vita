/* POSIX functions Rust std references but Vita newlib lacks.
 * None are used by the game itself; fail with ENOSYS. */
#include <errno.h>
#include <sys/types.h>

#define NOSYS { errno = ENOSYS; return -1; }

int symlink(const char *a, const char *b) NOSYS
long readlink(const char *p, char *buf, unsigned long n) NOSYS
long readlinkat(int fd, const char *p, char *buf, unsigned long n) NOSYS
int dup2(int a, int b) NOSYS
int mkfifo(const char *p, mode_t m) NOSYS
int lchown(const char *p, uid_t u, gid_t g) NOSYS
int fchown(int fd, uid_t u, gid_t g) NOSYS
pid_t getppid(void) { return 0; }

/* default newlib heap is too small for JA2's resources.
 * 128MB ran out playing the intro videos (whole .smk + decoded audio held in memory).
 * 256MB only fits the app budget with extended memory mode (ATTRIBUTE2=12 in param.sfo, see CMakeLists.txt).
 * ponytail: fixed size, tune if the game runs out or startup fails */
int _newlib_heap_size_user = 256 * 1024 * 1024;
/* vita defaults: main thread 256KB, SDL threads 64KB; old JA2 code has big stack arrays */
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

/* no user db: dirs::home_dir() falls back to $HOME, set before main */
#include <stdlib.h>
#include <sys/stat.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#define VITA_HOME "ux0:data/ja2-stracciatella"

/* long loads pump no input, so the system idle timer would auto-suspend the game mid-load */
static int vita_keep_awake(SceSize args, void *argp)
{
	for (;;) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		sceKernelDelayThread(10 * 1000 * 1000);
	}
	return 0;
}

__attribute__((constructor)) static void vita_set_home(void)
{
	SceUID t = sceKernelCreateThread("keep_awake", vita_keep_awake, 0x10000100, 0x1000, 0, 0, NULL);
	if (t >= 0) sceKernelStartThread(t, 0, NULL);

	/* opt-in overclock to max (default is 333MHz): drop an empty "overclock" file.
	 * ponytail: file flag until the launcher menu exists */
	struct stat st;
	if (stat(VITA_HOME "/overclock", &st) == 0) {
		scePowerSetArmClockFrequency(444);
		scePowerSetBusClockFrequency(222);
		scePowerSetGpuClockFrequency(222);
		scePowerSetGpuXbarClockFrequency(166);
	}

	mkdir(VITA_HOME, 0777);
	setenv("HOME", VITA_HOME, 1);
	setenv("TMPDIR", VITA_HOME, 1); /* rust temp_dir() -> log file lands here instead of /tmp */
	setenv("SDL_THREAD_STACK_SIZE", "1048576", 0); /* SDL hints fall back to env vars */
}

struct passwd;
int getpwuid_r(uid_t u, struct passwd *pw, char *buf, unsigned long n, struct passwd **res)
{ *res = 0; return ENOENT; }
