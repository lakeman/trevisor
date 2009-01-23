/*
 * Copyright (c) 2007, 2008 University of Tsukuba
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University of Tsukuba nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "constants.h"
#include "current.h"
#include "debug.h"
#include "initfunc.h"
#include "panic.h"
#include "printf.h"
#include "process.h"
#include "thread.h"
#include "spinlock.h"
#include "vmmcall.h"

static volatile int s, r;
static spinlock_t dbgsh_lock, dbgsh_lock2;
static bool stopped;
static tid_t tid;
static bool i;

static int
dbgsh_ttyin_msghandler (int m, int c)
{
	int tmp;

	if (m == 0) {
	retry:
		s = 0;
		while (r == -1 || s != -1) {
#ifndef FWDBG
			spinlock_lock (&dbgsh_lock2);
			stopped = true;
			thread_will_stop ();
			spinlock_unlock (&dbgsh_lock2);
#endif
			schedule ();
		}
		tmp = r;
		r = -1;
		if (tmp == 0)
			goto retry;
		return tmp;
	}
	return 0;
}

static int
dbgsh_ttyout_msghandler (int m, int c)
{
	if (m == 0) {
		if (c == 0)
			c = ' ';
		s = c;
		while (r == -1 || s != -1) {
#ifndef FWDBG
			spinlock_lock (&dbgsh_lock2);
			stopped = true;
			thread_will_stop ();
			spinlock_unlock (&dbgsh_lock2);
#endif
			schedule ();
		}
		r = -1;
	}
	return 0;
}

static void
dbgsh_thread (void *arg)
{
	int shell, ttyin, ttyout;

	msgregister ("dbgsh_ttyin", dbgsh_ttyin_msghandler);
	msgregister ("dbgsh_ttyout", dbgsh_ttyout_msghandler);
	debug_msgregister ();
	ttyin = msgopen ("dbgsh_ttyin");
	ttyout = msgopen ("dbgsh_ttyout");
	for (;;) {
		shell = newprocess ("shell");
		if (ttyin < 0 || ttyout < 0 || shell < 0)
			panic ("dbgsh_thread");
		msgsenddesc (shell, ttyin);
		msgsenddesc (shell, ttyout);
		msgsendint (shell, 0);
		msgclose (shell);
		schedule ();
	}
}

static void
dbgsh (void)
{
	ulong rbx;
	int b;

	spinlock_lock (&dbgsh_lock);
	if (!i) {
		tid = thread_new (dbgsh_thread, NULL, PAGESIZE);
		i = true;
	}
	spinlock_unlock (&dbgsh_lock);
	current->vmctl.read_general_reg (GENERAL_REG_RBX, &rbx);
	b = (int)rbx;
	if (b != -1) {
		r = b;
		s = -1;
#ifndef FWDBG
		spinlock_lock (&dbgsh_lock2);
		if (stopped)
			thread_wakeup (tid);
		spinlock_unlock (&dbgsh_lock2);
#endif
	}
	current->vmctl.write_general_reg (GENERAL_REG_RAX, (ulong)s);
}

static void
vmmcall_dbgsh_init_pcpu (void)
{
#ifdef FWDBG
	char buf[100];

	spinlock_lock (&dbgsh_lock);
	if (!i) {
		snprintf (buf, 100, "dbgsh:s=%p,r=%p\n", &s, &r);
		debug_addstr (buf);
		thread_new (dbgsh_thread, NULL, PAGESIZE);
		i = true;
	}
	spinlock_unlock (&dbgsh_lock);
#endif
}

static void
vmmcall_dbgsh_init (void)
{
#ifdef DBGSH
	s = r = -1;
	i = false;
	stopped = false;
	spinlock_init (&dbgsh_lock);
	spinlock_init (&dbgsh_lock2);
	vmmcall_register ("dbgsh", dbgsh);
#else
	if (0)
		dbgsh ();
#endif
}

INITFUNC ("vmmcal0", vmmcall_dbgsh_init);
INITFUNC ("pcpu1", vmmcall_dbgsh_init_pcpu);

/************************************************************
#include <stdio.h>
#include <stdlib.h>
#ifdef __MINGW32__
#include <conio.h>
#define NOECHO()
#define GETCHAR() getch ()
#define PUTCHAR(c) putch (c)
#define ECHO()
#else
#define NOECHO() system ("stty -echo -icanon")
#define GETCHAR() getchar ()
#define PUTCHAR(c) putchar (c), fflush (stdout)
#define ECHO() system ("stty echo icanon")
#endif

static int
vmcall_dbgsh (int c)
{
	int r, n;

	asm volatile ("push (%%ebx); push 4(%%ebx); lea 8(%%esp),%%esp; vmcall"
		      : "=a" (n) : "a" (0), "b" ("dbgsh"));
	if (!n)
		return -1;
	asm volatile ("vmcall" : "=a" (r) : "a" (n), "b" (c));
	return r;
}

void
e (void)
{
	ECHO ();
}

int
main ()
{
	int s, r;

	if (vmcall_dbgsh (-1) == -1)
		exit (1);
	atexit (e);
	NOECHO ();
	s = -1;
	for (;;) {
		r = vmcall_dbgsh (s);
		s = -1;
		if (r == 0) {
			s = GETCHAR ();
		} else if (r > 0) {
			PUTCHAR (r);
			s = 0;
		}
	}
}
 ************************************************************/
