// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/trap.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "trace", "Display stack back trace info", mon_backtrace },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}

/*
 * Implementation notes:
 * 1. The ebp values denote frame top of the function calls in the stack.
 *    Since stack frames essentially feature a single linked list, with each
 *    link pointing to the next one (or zero when the list is exhausted), we
 *    traverse the list by repeatedly dereferencing ebp.
 * 2. The eip values are meant to denote the function call context address
 *    *roughly*. It is actually determined as return address (or address to
 *    the next instruction) of the caller, which is always at the top of
 *    function call frames. For mon_backtrace() itself, this will be the next
 *    instruction after call to read_eip(). For other function call stack
 *    frames, this will be next instruction of the caller when the function
 *    call returns.
 * 3. The args, function call argument list, is immediately below eips. Each
 *    args[i] will be the ith argument as listed in function definition from
 *    left to right. Since we have no way (yet) to determine the argument
 *    number, we temporarily blindly print 5 arguments, some or all of which
 *    may be garbage if there ain't that many, and some may be missing if
 *    there are more than that.
 */
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t *ebp = (uint32_t *) read_ebp();
	uint32_t *eip = (uint32_t *) read_eip();
	uint32_t *args;
	int i, nargs;
	struct Eipdebuginfo info;
	char buf[64];

	cprintf("Stack backtrace:\n");
	while (1) {
		args = ebp + 2;
		nargs = 5; // #args default value
		if (debuginfo_eip((uintptr_t) eip, &info) == 0) {
			nargs = info.eip_fn_narg;
			strlcpy(buf, info.eip_fn_name, info.eip_fn_namelen+1);
			cprintf("%s:%d: %s+%d\n", info.eip_file, info.eip_line,
				buf, (uint32_t)eip - info.eip_fn_addr);
		}
		cprintf("  ebp %08x eip %08x  args", ebp, eip);
		// TODO: blindly printing 5 arguments since we have no way of
		// knowing the exact number.
		for (i = 0; i < nargs; i++)
			cprintf(" %08x", args[i]);
		cprintf("\n");
		if (!ebp) // function call frame list not exhausted?
			break;
		eip = (uint32_t *)*(ebp + 1);
		ebp = (uint32_t *)*ebp;
	};
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
