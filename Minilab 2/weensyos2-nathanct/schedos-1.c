#include "schedos-app.h"
#include "x86sync.h"

/*****************************************************************************
 * schedos-1
 *
 *   This tiny application prints red "1"s to the console.
 *   It yields the CPU to the kernel after each "1" using the sys_yield()
 *   system call.  This lets the kernel (schedos-kern.c) pick another
 *   application to run, if it wants.
 *
 *   The other schedos-* processes simply #include this file after defining
 *   PRINTCHAR appropriately.
 *
 *****************************************************************************/

#ifndef PRINTCHAR
#define PRINTCHAR	('1' | 0x0C00)
#endif

void
start(void)
{
	int i;

	for (i = 0; i < RUNCOUNT; i++) {
		//Use our atomic system call to print chars at cursor without interrupts
			//*cursorpos++ = PRINTCHAR; // Write characters to the console, yielding after each one.
		sys_atomic_char(PRINTCHAR);
		
		//This yield needs to be removed to prevent out-of-orderness
		//sys_yield();
	}

	// Yield forever.
	//while (1)
		//sys_yield();
	
	//Instead of yielding forever, we want to exit once process 1 finishes
	//Exit code 0 means a succesful exit
	sys_exit(0);
		
}
