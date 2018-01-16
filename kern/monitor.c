// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line
extern int16_t CGA_COLOR_MASK;

struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Trace the stack and call hierarchy", mon_backtrace },
	{ "chcolor", "Change the default display color", mon_chcolor },
	{ "continue", "Continue from a breakpoint", mon_continue },
	{ "si", "Continue from a breakpoint with single step", mon_si }
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

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
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");
	unsigned ebp = read_ebp();
	unsigned eip;
	char funn[100];
	struct Eipdebuginfo info;
	int i;
	while(ebp != 0) // Small trick found in entry.S
	{
        eip = *(unsigned*)(ebp+4);
        debuginfo_eip(eip, &info);
        for(i = 0; i < info.eip_fn_namelen; i++)
            funn[i] = info.eip_fn_name[i];
        funn[info.eip_fn_namelen] = 0;
        cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
        ebp, *(unsigned*)(ebp+4), *(unsigned*)(ebp+8), *(unsigned*)(ebp+12), *(unsigned*)(ebp+16), *(unsigned*)(ebp+20), *(unsigned*)(ebp+24));
        cprintf("         %s:%d: %s+%d\n",
        info.eip_file, info.eip_line, funn, eip - info.eip_fn_addr);
        ebp = *(unsigned*)ebp;
    }
	return 0;
}

int
mon_chcolor(int argc, char **argv, struct Trapframe *tf)
{
    int16_t bg, ft;
    if(argc != 2)
    {
        cprintf("Argument number error\n");
    }
    else if(strlen(argv[1]) != 2)
    {
        cprintf("Argument error");
    }
    else
    {

        switch(argv[1][0])
        {
            case 'r': bg = 1<<6; break;
            case 'g': bg = 1<<5; break;
            case 'b': bg = 1<<4; break;
            case 'w': bg = 0x70; break;
            default: bg = 0x00;
        }
        switch(argv[1][1])
        {
            case 'r': ft = 1<<2; break;
            case 'g': ft = 1<<1; break;
            case 'b': ft = 1; break;
            case 'w': ft = 0x07; break;
            default: ft = 0x00;
        }
        CGA_COLOR_MASK = (bg|ft)<<8;
    }
    cprintf("Color changed\n");
    return 0;
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
    if(tf == NULL) {
        cprintf("Not a breakpoint\n");
        return 0;
    }
    tf->tf_eflags &= ~FL_TF;
    env_pop_tf(tf);
    return 0;
}

int
mon_si(int argc, char **argv, struct Trapframe *tf)
{
    if(tf == NULL) {
        cprintf("Not a breakpoint\n");
        return 0;
    }
    cprintf("Single Step\n");
    tf->tf_eflags |= FL_TF;
    env_pop_tf(tf);
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
