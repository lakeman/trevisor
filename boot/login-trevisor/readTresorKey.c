/*
 * TRESOR key creation
 * 
 * Copyright (C) 2012   Tilo Mueller <tilo.mueller@informatik.uni-erlangen.de>
 *                      Benjamin Taubmann <taubmann.benjamin@informatik.stud.uni-erlangen.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include "tresor_key.h"
#include "readTresorKey.h"
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


int
readpw(const char *passwd)
{
    int i;
    int c;
    //char passwd[128];
    char passwd2[128];
    unsigned char digest[64];
    unsigned char digest2[64];
    struct termios tmp_term;
    int no_match;

    if (argc != 2) {
         fprintf(stderr, "Wrong parameters: %s keyfile\n", argv[0]);
         exit(EXIT_FAILURE);
    }

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
        
        if (strncmp(passwd, passwd2,128)) {
            no_match = 1;
            printf("Passwords do not match!\n\n");
        } else {
            no_match = 0;
        }
    
    } while (no_match);



 //   printf("\nEntered [%s]\n",passwd);
    printf("\n");

 /*   sha256(passwd, strlen(passwd), digest);
    sha256((char*)digest, 32, digest2);
    FILE *keyfile;
    keyfile = fopen(argv[1],"w");  
    for(i=0; i<32; i++) {
	if (fputc(digest2[i], keyfile) == EOF)  {
            perror("fputc"); 
            exit(EXIT_FAILURE);
        }
    } 
    fclose(keyfile);  */

    /* Restore terminal configuration */
    restore_terminal();

    return 0;
}


