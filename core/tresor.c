/*
 * TRESOR Core
 * 
 * Copyright (C) 2012	Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de>
 *			 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#include "initfunc.h"
#include "config.h"
#include "pcpu.h"
#include "tresor_asm.h"
#include "ap.h"
#include "string.h"
#include "printf.h"
#include "tresor.h"
#include "tresor_sha256.h"
#include "keyboard.h"
#include "process.h"
#include "beep.h"
#include "sleep.h"
#include "spinlock.h"
#include <tcg.h>

#define MAXCPU      16
#define SALTLEN     8

#define NOTINIT     0
#define KEYAVAIL    1
#define KEYSET      2     

#define TPM_DIGEST_SIZE 20

static volatile int             init[MAXCPU];
static volatile unsigned char   key_swap[32];
static volatile unsigned int    init_cpus;
static volatile int             sleeped;
static spinlock_t               lock;

static u8 pcr0[TPM_DIGEST_SIZE];
static u8 pcr1[TPM_DIGEST_SIZE];
static u8 pcr2[TPM_DIGEST_SIZE];
static u8 pcr3[TPM_DIGEST_SIZE];
static u8 pcr4[TPM_DIGEST_SIZE];

/* If all CPUs are initialized the key must be erased 
   from RAM. */
void clear_swap(void) {
    int cpus = 0;
    spinlock_lock(&lock);
    cpus = ++init_cpus;
    spinlock_unlock(&lock);
    if (cpus > num_of_processors) {
        memset((void*)key_swap, 0, 32);
    }
}


void tresor_init_ap(void) {
    // key has already been set
    if (init[currentcpu->cpunum] == KEYSET) return;

    // waiting for the BSP to be ready
    if (init[currentcpu->cpunum] != KEYAVAIL) return;

    //printf("Starting Tresor on CPU %d of %d\n",currentcpu->cpunum, num_of_processors);

    tresor_set_key(key_swap);

    clear_swap();
    init[currentcpu->cpunum] = KEYSET;  
}

/* Called directly after entering the password
   This function parses the configuration and sets the password to 
   the debug register. 
   Furthermore it copies the key to a temporal variable and replaces
   the key in configuration with its hash value (required to verify the 
   password after wakeup) 
*/
   
void tresor_init_bsp(void) {
    int j,i;
    // Function is called more than once
    // We will set the key only one time!
    static int key_set = NOTINIT;

    if (key_set != NOTINIT)
        return;

    key_set     = 1;
    init_cpus   = 0;
    sleeped     = 0;

    for (i=num_of_processors; i>=0; i--)
             init[i] = NOTINIT;

    spinlock_init(&lock);
    
    // Find the tresor key in the configuration
    for (j = 0; j < 32; j++) {
        if (!strcmp(config.storage.keys_conf[j].crypto_name,"tresor")) {
            int idx=config.storage.keys_conf[j].keyindex;
            u8* key = config.storage.keys[idx];

            tresor_set_key(key);
            tresor_get_key(key_swap);

            // save the hash of the key 
            // we need this to check the password after standby
            u8 digest[32];
            tresor_sha256((const char*) key, 32, (unsigned char*) digest);
            memcpy(key, digest, 32);
            memset(digest, 0, 32);

            clear_swap();
            printf("Initialized TRESOR on CPU %d of %d\n",currentcpu->cpunum, num_of_processors);
            for (i=num_of_processors; i>=0; i--)
               init[i] = KEYAVAIL;

            // Take only the first tresor match
            return;
        }
    }
    printf("TRESOR not initialized; key not found\n");
    for (i=num_of_processors; i>=0; i--)
             init[i] = KEYSET;

}


void 
blinking_capslock() {
    int i;
    beep_on();
    for (i=0; i<3; i++) {
        beep_on();
        setkbdled (LED_CAPSLOCK_BIT);	
        usleep (100000);
        setkbdled (0);	
        beep_off();
        usleep (100000);
    }
    /* PS2 Keyboard driver seems to be buggy */
    keyboard_flush(); 
    keyboard_reset(); 
}

/* Called after waking up from Suspend to RAM
   used to reinitialize the debug registers.
   Thereto, the password must be reentered 
*/
void
tresor_wakeup() {
    int ttyin;
    int c;
    char pw[64];
    int i=0;
    u8* key = NULL;
    char* salt;

    if (currentcpu->cpunum != 0) 
        return;

    ttyin = msgopen("keyboard");
    
    // Find hash over key in config
    for (i = 0; i < 32; i++) {
        if (!strcmp(config.storage.keys_conf[i].crypto_name,"tresor")) {
            int idx=config.storage.keys_conf[i].keyindex;
            key = config.storage.keys[idx];
            salt = config.storage.keys_conf[i].salt;
            break;
        }
    }
    if (key == NULL) {
        for (i=num_of_processors; i>=0; i--)
             init[i] = 1;
        return;
    }
    /* show the user the he has to enter the password */
    blinking_capslock();

    i=0;
    /* loop while the entered password is wrong */
   
    while (1) {
        c = msgsendint (ttyin, 0);
//        blinking_capslock();
        if (c == 0xa) {
            char digest[32];
            char digest2[32];
            //copy salt after the password
            memcpy(&pw[i], salt, SALTLEN);
            tresor_sha256(pw, i+SALTLEN, (unsigned char *)digest);
            tresor_sha256(digest, 32, (unsigned char *)digest2);

            if (memcmp(digest2, key, 32) == 0) {
                // Correct password
                tresor_set_key((const u8 *)digest);
                memset(digest, 0, 32);
                memset(digest2, 0, 32);
                break;
            } else {
                // Wrong password
                i = 0;
                blinking_capslock();
            }
        } else {
            pw[i++] = (char)c;
            i=i%64;
        } 
    };
    
    // clear password memory
    memset(pw, 0, 64);
    tresor_get_key(key_swap);
    clear_swap(); 
    /* other processors may now get the key from RAM */
    for (i=num_of_processors; i>=0; i--)
        init[i] = KEYAVAIL;

}

/* Called when the system goes in stand-by mode;
   After wakeup the key must be redistributed */
void tresor_sleep(void) {
    int i;
    sleeped = 1;
    init_cpus = 0;
    for (i=num_of_processors; i!=0; i--)
        init[i] = NOTINIT;

}


#ifdef TCG_BIOS
/* Reads the contents of the PCR registers */
static void
readPCR (void)
{
    read_pcr(0, pcr0);
    read_pcr(1, pcr1);
    read_pcr(2, pcr2);
    read_pcr(3, pcr3);
    read_pcr(4, pcr4);
    /*int i=0; 
    for (i=0; i<20; i++)
        printf("%hhx ", pcr0[i]);
    printf("\n");*/
}

/* interface that allows to retrieve the stored contents
   of the PCRs; used because realmode is not available while
   the mini OS is active */
void 
getPCR(u32 idx, u8* data){
	switch(idx){
		case 0:	memcpy(data, pcr0, TPM_DIGEST_SIZE); break;
		case 1:	memcpy(data, pcr1, TPM_DIGEST_SIZE); break;
		case 2:	memcpy(data, pcr2, TPM_DIGEST_SIZE); break;
		case 3:	memcpy(data, pcr3, TPM_DIGEST_SIZE); break;
		case 4:	memcpy(data, pcr4, TPM_DIGEST_SIZE); break;
	}
}

INITFUNC ("global3", readPCR);
#endif



