#include "schedos-kern.h"
#include "x86.h"
#include "lib.h"

/*****************************************************************************
 * schedos-kern
 *
 *   This is the schedos's kernel.
 *   It sets up process descriptors for the 4 applications, then runs
 *   them in some schedule.
 *
 *****************************************************************************/

// The program loader loads 4 processes, starting at PROC1_START, allocating
// 1 MB to each process.
// Each process's stack grows down from the top of its memory space.
// (But note that SchedOS processes, like MiniprocOS processes, are not fully
// isolated: any process could modify any part of memory.)

#define NPROCS		5
#define PROC1_START	0x200000
#define PROC_SIZE	0x100000

// +---------+-----------------------+--------+---------------------+---------/
// | Base    | Kernel         Kernel | Shared | App 0         App 0 | App 1
// | Memory  | Code + Data     Stack | Data   | Code + Data   Stack | Code ...
// +---------+-----------------------+--------+---------------------+---------/
// 0x0    0x100000               0x198000 0x200000              0x300000
//
// The program loader puts each application's starting instruction pointer
// at the very top of its stack.
//
// System-wide global variables shared among the kernel and the four
// applications are stored in memory from 0x198000 to 0x200000.  Currently
// there is just one variable there, 'cursorpos', which occupies the four
// bytes of memory 0x198000-0x198003.  You can add more variables by defining
// their addresses in schedos-symbols.ld; make sure they do not overlap!

// A process descriptor for each process.
// Note that proc_array[0] is never used.
// The first application process descriptor is proc_array[1].
static process_t proc_array[NPROCS];

// A pointer to the currently running process.
// This is kept up to date by the run() function, in mpos-x86.c.
process_t *current;

// The preferred scheduling algorithm.
int scheduling_algorithm;

//START: NATHAN'S IMPLEMENTATION

	// Declare seed variable use system call to find current time and store as seed
	unsigned tsc;

	// Create an array for assigning lottery "priority" tickets to processes
	static int lottery_values[NPROCS];

	// Linear-feedback shift register is commonly implemented as a pseudo-random number generator
	// Algorithm taken from http://en.wikipedia.org/wiki/Linear_feedback_shift_register
	unsigned linear_feedback_shift_register()
	{
		unsigned lfsr = tsc;
		unsigned bit;
		bit  = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5) ) & 1;
		return tsc =  (lfsr >> 1) | (bit << 15);
	}

//START: NATHAN'S IMPLEMENTATION

/*****************************************************************************
 * start
 *
 *   Initialize the hardware and process descriptors, then run
 *   the first process.
 *
 *****************************************************************************/

void
start(void)
{
	int i;

	// Set up hardware (schedos-x86.c)
	segments_init();
	interrupt_controller_init(1);
	console_clear();

	// Initialize process descriptors as empty
	memset(proc_array, 0, sizeof(proc_array));
	for (i = 0; i < NPROCS; i++) {
		proc_array[i].p_pid = i;
		proc_array[i].p_state = P_EMPTY;
	}

//START: NATHAN'S IMPLEMENTATION

	//Setting process priorities (should be 2323...1...4...)
	proc_array[1].p_priority = 1;
	proc_array[2].p_priority = 0;
	proc_array[3].p_priority = 0;
	proc_array[4].p_priority = 2;
	
	//Setting process shares (should be 1223334444...)
	proc_array[0].p_shares = 0;
	proc_array[1].p_shares = 1;
	proc_array[2].p_shares = 2;
	proc_array[3].p_shares = 3;
	proc_array[4].p_shares = 4;
	
	//Initialize each process share count to zero
	for (i = 0; i < NPROCS; i++) {
		proc_array[i].p_shareCount = 0;
	}

	//Use system call to grab current time
	asm volatile("rdtsc" : "=A" (tsc));
	
	//Set the lottery tickets for processes
	
	//Set indexes 1 through 4 to hold the number of lottery tickets assigned to that process
	//Chances are, this will produce something of order 4...1...3...2...
	lottery_values[1] = 30;
	lottery_values[2] = 2;
	lottery_values[3] = 10;
	lottery_values[4] = 100;

	//Now, our code will set each process's lottery value to be cumulative
	//I.E. using the above, process 1 has 4 tickets, process 2 has 7 tickets,
	//process 3 has 9 tickets, and process 4 has 10 tickets
	lottery_values[2] += lottery_values[1];
	lottery_values[3] += lottery_values[2];
	lottery_values[4] += lottery_values[3];
	
	//Index 0 holds the total number of lottery tickets assigned (same as process 4)
	lottery_values[0] = lottery_values[4];
	
//EMD: NATHAN'S IMPLEMENTATION

	// Set up process descriptors (the proc_array[])
	for (i = 1; i < NPROCS; i++) {
		process_t *proc = &proc_array[i];
		uint32_t stack_ptr = PROC1_START + i * PROC_SIZE;

		// Initialize the process descriptor
		special_registers_init(proc);

		// Set ESP
		proc->p_registers.reg_esp = stack_ptr;

		// Load process and set EIP, based on ELF image
		program_loader(i - 1, &proc->p_registers.reg_eip);

		// Mark the process as runnable!
		proc->p_state = P_RUNNABLE;
	}

	// Initialize the cursor-position shared variable to point to the
	// console's first character (the upper left).
	cursorpos = (uint16_t *) 0xB8000;

	// Initialize the scheduling algorithm.
	scheduling_algorithm = 0;

	// Switch to the first process.
	run(&proc_array[1]);

	// Should never get here!
	while (1)
		/* do nothing */;
}



/*****************************************************************************
 * interrupt
 *
 *   This is the weensy interrupt and system call handler.
 *   The current handler handles 4 different system calls (two of which
 *   do nothing), plus the clock interrupt.
 *
 *   Note that we will never receive clock interrupts while in the kernel.
 *
 *****************************************************************************/

void
interrupt(registers_t *reg)
{
	// Save the current process's register state
	// into its process descriptor
	current->p_registers = *reg;

	switch (reg->reg_intno) {

	case INT_SYS_YIELD:
		// The 'sys_yield' system call asks the kernel to schedule
		// the next process.
		schedule();

	case INT_SYS_EXIT:
		// 'sys_exit' exits the current process: it is marked as
		// non-runnable.
		// The application stored its exit status in the %eax register
		// before calling the system call.  The %eax register has
		// changed by now, but we can read the application's value
		// out of the 'reg' argument.
		// (This shows you how to transfer arguments to system calls!)
		current->p_state = P_ZOMBIE;
		current->p_exit_status = reg->reg_eax;
		schedule();

	case INT_SYS_SHARE:
		// 'sys_share' grabs a process's number of shares
		// from the %eax register
		current->p_shares = reg->reg_eax;
		run(current);

	case INT_SYS_PRIORITIZE:
		// 'sys_prioritize' grabs a process's priority level
		// from the %eax register
		current->p_priority = reg->reg_eax;
		run(current);
		
	case INT_SYS_ATOMIC_CHAR:
		// 'sys_share' atomically sets the char from the %eax register
		// and increments the cursor position to prevent interrupts and gaps
		*cursorpos++ = reg->reg_eax;
		schedule();

	case INT_CLOCK:
		// A clock interrupt occurred (so an application exhausted its
		// time quantum).
		// Switch to the next runnable process.
		schedule();

	default:
		while (1)
			/* do nothing */;

	}
}

/*****************************************************************************
 * schedule
 *
 *   This is the weensy process scheduler.
 *   It picks a runnable process, then context-switches to that process.
 *   If there are no runnable processes, it spins forever.
 *
 *   This function implements multiple scheduling algorithms, depending on
 *   the value of 'scheduling_algorithm'.  We've provided one; in the problem
 *   set you will provide at least one more.
 *
 *****************************************************************************/

void
schedule(void)
{
	pid_t pid = current->p_pid;

	if (scheduling_algorithm == 0) //round-robin scheduling
	{
		while (1)
		{
			pid = (pid + 1) % NPROCS;
			// Run the selected process, but skip
			// non-runnable processes.
			// Note that the 'run' function does not return.
			if (proc_array[pid].p_state == P_RUNNABLE)
				run(&proc_array[pid]);
		}
	}
	else if (scheduling_algorithm == 1) //priority scheduling based on pid
	{
		//Since we're using strict scheduling, we always schedule process 1 first
		pid = 1;
		while (1)
		{
			//If process is not runnable, we need to increment the pid
			if (proc_array[pid].p_state != P_RUNNABLE)
				pid = (pid + 1) % NPROCS;
			else //Otherwise, print it out
				run(&proc_array[pid]);
		}
	}
	else if (scheduling_algorithm == 2) //priority scheduling based on variable (p_priority)
	{
		//Determine the pid and priority of the process to run first
		int i;
		int maxPriority; //Notice that "max" here actually refers to the most negative number
		int nextPid;
		
		//Loop through processes until we find one that is runnable; this is our starting point
		while(proc_array[pid].p_state != P_RUNNABLE)
			pid = (pid + 1) % NPROCS;

		maxPriority = proc_array[pid].p_priority;
		nextPid = pid;
		
		//Find a runnable process that has the highest priority (lowest priority number)
		for(i=0; i<NPROCS-1; i++)
		{
			//Look at the next process, but not on the first go (otehrwise we skip one)
			pid = (pid + 1) % NPROCS;
			
			//If that process is runnable, consider its priority
			if(proc_array[pid].p_state == P_RUNNABLE && proc_array[pid].p_priority <= maxPriority)
			{
				//If we find something runnable that's even higher in priority
				//That's tentatively the next process we're going to run
				
				maxPriority = proc_array[pid].p_priority;
				nextPid = pid;
			}
		}
		
		//By now, we've cycled through all the processes and found the one with highest priority that hasn't been run
		//So now we run it; it should keep getting discovered in consecutive calls until it is no longer runnable
		run(&proc_array[nextPid]);
	}
	else if (scheduling_algorithm == 3) //priority scheduling based on variable (p_shares)
	{
		while (1)
		{
			//If we still have shares left, increment the counter and run the process if runnable
			if (proc_array[pid].p_shareCount < proc_array[pid].p_shares)
			{
				proc_array[pid].p_shareCount++;
				if (proc_array[pid].p_state == P_RUNNABLE)
				{
					run(&proc_array[pid]);
				}
				
			}
			else
			{
				//If the process share counter reaches the maximum amount of shares assigned
				//Then we increment the pid to move onto the next process and reset the share counter
				proc_array[pid].p_shareCount = 0;
				pid = (pid + 1) % NPROCS;
			}
		}
	}
	else if (scheduling_algorithm == 4) //lottery scheduling
	{
		while (1)
		{	
			int i;
			//Use LFSR algorithm and mod it by the total number of lottery tickets
			int lotterySelector = linear_feedback_shift_register() % lottery_values[0];
			
			for(i=1; i<NPROCS; i++)
			{
				//Selector has chosen some process (it might not be runnable)
				if (lotterySelector < lottery_values[i])
				{
					if(proc_array[i].p_state == P_RUNNABLE)
					{
						//If the process is runnable, run it and break
						run(&proc_array[i]);
						break;
					}
					else
					{
						//If the process is not runnable, we don't want to pass to the seletor on
						//since this inaccurately will give the next process a higher chance of running
						//due to cumulative tickets; we should just break
						break;
					}
				}
			}
		}	
	}
	
	// If we get here, we are running an unknown scheduling algorithm.
	cursorpos = console_printf(cursorpos, 0x100, "\nUnknown scheduling algorithm %d\n", scheduling_algorithm);
	while (1)
		/* do nothing */;
}
