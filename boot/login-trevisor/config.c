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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include "tresor_key.h"
#include "core/config.h"
#include "configparser.h"

#define SALTLEN	8

typedef unsigned char		u8;
typedef unsigned short int	u16;
typedef unsigned int		u32;
typedef unsigned long long int	u64;
void sha256(const char* message, int msglen, unsigned char* digest);

/*
 * Terminal configuration for restore
 */
struct termios saved_term;

/*
 * Restore terminal configuration
 */
void
restore_terminal(void)
{
    if( -1 == tcsetattr(fileno(stdin), TCSANOW, &saved_term) ){
        perror("tcsetattr(): Cannot revert terminal setting");
        exit(EXIT_FAILURE);
    }
}

/*
 * Reads the password from stdin and disable the echo 
 * of the terminal
 */

int
readpw(char *passwd)
{
    int i;
    int c;
    char passwd2[128];
    struct termios tmp_term;
    int no_match; 
    unsigned int length;

 
    /* Save current terminal configuration */
    if ( -1 == tcgetattr(fileno(stdin), &saved_term) ) {
        perror("tcgetattr(): Cannot retrieve the current terminal setting");
        exit(EXIT_FAILURE);
    }
    tmp_term = saved_term;
   
    /* Set terminal options for password prompt */
    tmp_term.c_lflag &= ~ECHO;  /* Disable echo-back */
    if ( -1 == tcsetattr(fileno(stdin), TCSANOW, &tmp_term) ) {
        perror("tcsetattr(): Cannot update terminal setting");
        exit(EXIT_FAILURE);
    }

    do {
            /* Prompt */
        printf("Password: ");
        i = 0;
        do {
            /* Get character from stdin */
            c = fgetc(stdin);
            if ( isascii(c) && '\r' != c && '\n' != c ) {
                if ( i < sizeof(passwd) - 1 ) {
                    passwd[i] = c;
                    i++;
                }
            }
        } while ( c != '\n' );
        passwd[i] = '\0';

        printf("\nRetype Password: ");
        i = 0;
        do {
            /* Get character from stdin */
            c = fgetc(stdin);
            if ( isascii(c) && '\r' != c && '\n' != c ) {
                if ( i < sizeof(passwd) - 1 ) {
                    passwd2[i] = c;
                    i++;
                }
            }
        } while ( c != '\n' );
        passwd2[i] = '\0';
        printf("\n");
        if (strncmp(passwd, passwd2,64)) {
            no_match = 1;
            printf("Passwords do not match!\n\n");
        } else {
            no_match = 0;
        }
        length=i;
    
    } while (no_match);



    printf("\n");

    /* Restore terminal configuration */
    restore_terminal();

    return length;
}


int
main (int argc, char **argv, const char *const envp[])
{
	static char buf[8192];
	static struct config_data config;
	int i=0;
	static char pw[64];
   	static FILE* fp;
	static FILE* modulefp;
	static unsigned pwlen;
	static unsigned char digest[32];
    static FILE* masterkey = NULL;

	if (argc < 3) {
		fprintf(stderr, "%s config_file.conf module2.bin [masterkey]", argv[0]);
		exit(EXIT_FAILURE);
	}
	/* Open the configuration */
	if ( ( fp = fopen (argv[1], "r")) == NULL){
		perror("fopen: ");
		exit(EXIT_FAILURE);
	}
    /* Open the output file */
	if ( ( modulefp = fopen (argv[2], "w")) == NULL){
		perror("fopen: ");
		exit(EXIT_FAILURE);
	}

    if (argc == 4) {
        if ( ( masterkey = fopen (argv[3], "w")) == NULL){
            perror("fopen: ");
    		exit(EXIT_FAILURE);
	    }
    }
   

	memset (&config, 0, sizeof config);
		config.len = sizeof config;
		configparser (buf, sizeof buf, fp, &config, setconfig);
	load_random_seed (&config, "/dev/urandom");
	pwlen = readpw(pw);
    /* copy random bytes (salt) behind the password string
       and compute the hash sum over it  */
	memcpy(&pw[pwlen], config.vmm.randomSeed, SALTLEN);
	pwlen = pwlen + SALTLEN; 	
    sha256((const char*)pw, pwlen, (unsigned char *)digest);

    if (masterkey != NULL) {
        if (fwrite(&digest, sizeof(digest), 1, masterkey) != 1) { 
            perror("fwrite");
            exit(EXIT_FAILURE);
        }
    }
    
    /* copy the salt into the configuration */
	for (i=0; i<NUM_OF_STORAGE_KEYS_CONF; i++) {
		if (strcmp(config.storage.keys_conf[i].crypto_name, "tresor") == 0)  {
			memcpy(&config.storage.keys_conf[i].salt, config.vmm.randomSeed, SALTLEN);
			sha256((const char*)digest, 32,(unsigned char*)config.storage.keys[i]); 
		}
	}
    /* write the configuration to disk */
	if (fwrite (&config, sizeof config, 1, modulefp) != 1) {
		perror ("fwrite");
		exit (EXIT_FAILURE);
	}
	exit (EXIT_SUCCESS);
}
