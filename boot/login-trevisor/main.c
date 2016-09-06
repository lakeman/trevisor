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

#include "core/config.h"
#include "tresor_key.h"

#define SALTLEN 8

typedef unsigned char		u8;
typedef unsigned short int	u16;
typedef unsigned int		u32;
typedef unsigned long long int	u64;

struct loadcfg_data {
	unsigned int len;
	unsigned int pass, passlen;
	unsigned int data, datalen;
};

int getkey (void);
void call0x10 (int, int, int, int);
int decrypt (struct loadcfg_data *);
void boot (struct config_data *);
void showPCR(u8 *);

char title[] = "TreVisor";
char randseed[4096];
int randseedsize = sizeof randseed;

void
fillchar (int attr, unsigned char c, int left, int top, int width, int height)
{
	int i;

	for (i = 0; i < height; i++) {
		call0x10 (0x200, 0, 0, ((top + i) << 8) | left);
		call0x10 (0x900 + c, attr, width, 0);
	}
}

void
printstr (int attr, char *str, int left, int top)
{
	int cur;

	cur = (top << 8) | left;
	while (*str) {
		call0x10 (0x200, 0, 0, cur++);
		call0x10 (0x900 + (unsigned char)*str++, attr, 1, 0);
	}
}

void
drawframe (int attr, int attrshadow, int left, int top, int width, int height)
{
	int right = left + width - 1;
	int bottom = top + height - 1;

	fillchar (attr, 0xDA, left, top, 1, 1);
	fillchar (attr, 0xC4, left + 1, top, width - 2, 1);
	fillchar (attrshadow, 0xBF, right, top, 1, 1);
	fillchar (attr, 0xB3, left, top + 1, 1, height - 2);
	fillchar (attrshadow, 0xB3, right, top + 1, 1, height - 2);
	fillchar (attr, 0xC0, left, bottom, 1, 1);
	fillchar (attrshadow, 0xC4, left + 1, bottom, width - 2, 1);
	fillchar (attrshadow, 0xD9, right, bottom, 1, 1);
}

void
drawdlgframe (int left, int top, int width, int height)
{
	int right = left + width - 1;
	int bottom = top + height - 1;

	fillchar (0x00, ' ', left + 2, top + 1, width, height);
	fillchar (0x70, ' ', left, top, width, height);
	drawframe (0x7F, 0x70, left, top, width, height);
	fillchar (0x7F, 0xC3, left, bottom - 2, 1, 1);
	fillchar (0x7F, 0xC4, left + 1, bottom - 2, width - 2, 1);
	fillchar (0x70, 0xB4, right, bottom - 2, 1, 1);
}

void
putpass (char *string)
{
	while (*string)
		call0x10 (0x0E00 + (unsigned char)*string++, 7, 0, 0);
}

void
get_password (char *buf, int len)
{
	char c;
	int r;
	int i;
	int width = 45;

	i = 0;
	for (;;) {
		r = getkey ();
		if (!(r & 0xFF))
			continue;
		c = r & 0xFF;
		switch (c) {
		case '\n':
		case '\r':
			break;
		case '\0':
			continue;
		case '\b':
		case '\177':
			if (i > 0) {
				if (i < width)
					putpass ("\b \b");
				i--;
			}
			continue;
		case '\25':
			while (i > 0) {
				if (i < width)
					putpass ("\b \b");
				i--;
			}
			continue;
		default:
			if (i + 1 < len) {
				buf[i] = c;
				i++;
				if (i < width)
					putpass ("*");
			}
			continue;
		}
		break;
	}
	buf[i] = '\0';
}

int
strlen (char *string)
{
	int i = 0;

	while (*string++)
		i++;
	return i;
}
int strcmp(char *s, char *t) 
{
	int len = strlen(s);
	if (strlen(t) != len)
		return 1;
	int i=0;
	for (i=0; i<len; i++) {
		if (s[i] != t[i])
			return 1;
	}
	return 0;	
}
int memcmp(const void *s1, const void *s2, size_t n)
{
     const unsigned char *us1 = (const unsigned char *) s1;
     const unsigned char *us2 = (const unsigned char *) s2;
     while (n-- != 0) {
         if (*us1 != *us2)
             return (*us1 < *us2) ? -1 : +1;
         us1++;
         us2++;
     }
     return 0;
}

void
drawinputdlg (char *title1, char *title2, char *msg1, int textwidth, char *pcr0, char *pcr1, char* pcr2, char *pcr3, char *pcr4)
{
	int left, top, width, height, tleft, ttop, twidth, theight;
	int shadowx = 2, shadowy = 1;
	int swidth = 80, sheight = 25;

	twidth = textwidth + 2;
	theight = 3;
	width = twidth + 4;
	height = theight + 11;
	left = (swidth - (width + shadowx) + 1) / 2;
	top = (sheight - 2 - (height + shadowy) + 1) / 2 + 2;
	tleft = left + 2;
	ttop = top + 2;

	call0x10 (3, 0, 0, 0);	/* set mode */
	fillchar (0x13, ' ', 0, 0, swidth, sheight); /* paint blue */
	printstr (0x1B, title1, 1, 0); /* title */
	fillchar (0x1B, 0xC4, 1, 1, swidth - 2, 1); /* line */
	drawdlgframe (left, top, width, height); /* frame */
	printstr (0x79, title2, left + (width - strlen (title2)) / 2, top);
	printstr (0x74, msg1, left + 2, top + 1);
	printstr (0x74, "PCR0", left + 2, top + 6);
	printstr (0x74,  pcr0 , left + 7, top + 6);
	printstr (0x74, "PCR1", left + 2, top + 7);
	printstr (0x74,  pcr1 , left + 7, top + 7);
	printstr (0x74, "PCR2", left + 2, top + 8);
	printstr (0x74,  pcr2 , left + 7, top + 8);
	printstr (0x74, "PCR3", left + 2, top + 9);
	printstr (0x74,  pcr3 , left + 7, top + 9);
	printstr (0x74, "PCR4", left + 2, top + 10);
	printstr (0x74,  pcr4 , left + 7, top + 10);
	drawframe (0x70, 0x7F, tleft, ttop, twidth, theight);
	printstr (0x1F, "<      >", left + (width - 8) / 2, top + height - 2);
	printstr (0x1F, "O", left + (width - 8) / 2 + 3, top + height - 2);
	printstr (0x1E, "K", left + (width - 8) / 2 + 4, top + height - 2);
	printstr (0x70, " ", tleft + 1, ttop + 1);
}

void
os_main (char *p)
{
	static char buf[4096];
	void *data;
	unsigned int datalen;
	int i, j, k;
	struct loadcfg_data ld;
	struct config_data *cfg;
	static u8 pcrdata[5*(40+1)];
	
	char* pcr0 = (char*)pcrdata;
	char* pcr1 = (char*)&pcrdata[41];
	char* pcr2 = (char*)&pcrdata[82];
	char* pcr3 = (char*)&pcrdata[123];
	char* pcr4 = (char*)&pcrdata[164];

	showPCR(pcrdata);
	data = *(void **)&p[0x218];
	datalen = *(unsigned int *)&p[0x21C];
	for (i = 0; i < 3; i++) {
            //drawinputdlg (title, "Login", "Enter Your Password.", 45);
            //get_password (buf, sizeof buf);
            ld.len = sizeof ld;
            ld.pass = (unsigned int)buf;
            ld.passlen = strlen (buf);
            ld.data = (unsigned int)data;
            ld.datalen = datalen;
            //if (decrypt (&ld)) {
            cfg = data;
            for (j = 0; j < sizeof cfg->vmm.randomSeed; j++)
                    cfg->vmm.randomSeed[j] += randseed[j];
            // Get the tresor key
            for (j = 0; j <  NUM_OF_STORAGE_KEYS_CONF; j++) {
                    /* search for TRESOR configuration entries  
                       and take the first match
                    */
                    if (!strcmp(cfg->storage.keys_conf[j].crypto_name,"tresor")) {
                            int l;
                            unsigned char digest[64];
                            unsigned char digest2[64]; 
                            for (l=0; l<10; l++) {
                                    drawinputdlg (title, "Login", "Enter Your TRESOR Password.", 45, pcr0, pcr1, pcr2, pcr3, pcr4);
                                    //drawinputdlg (title, "Login", pcr, 5);
                                    get_password (buf, sizeof buf);
                                    k = strlen(buf);
                                    int m=0;
                                    /* Copy the corresponding salt behind  the password string  
                                       and compute the digest (encryption key) and second digest
                                       to verify the password. */
                                    for(m=0; m<SALTLEN; m++) buf[k+m] = cfg->storage.keys_conf[j].salt[m];
                                    sha256(buf, k+SALTLEN, digest);
                                    sha256((char *)digest, 32, digest2);

                                    /* Check if the computed hash value matches the hash in the config */
                                    if (memcmp(digest2, cfg->storage.keys[cfg->storage.keys_conf[j].keyindex], 32) == 0 ) {
                                            for (k=0; k<32; k++) 
                                                    cfg->storage.keys[cfg->storage.keys_conf[j].keyindex][k] = digest[k];

                                            for (j=0; j<sizeof(buf); j++)
                                                    buf[k] = 0;
                                            ld.passlen = 0;
                                            /* clear the cache */
                                            asm("wbinvd");
                    
                                            /* start the guest operating system */
                                            boot (cfg);
                                            // no return;
                                    }
                            }
                            call0x10 (3, 0, 0, 0);	/* set mode */
                            printstr (0x07, "Authentication failure ", 0, 0);

                    }
            }
            /* if no TRESOR configuration has been found we can simply
               go on. however other keys are not encrypted inside the 
               configuration */
            boot (cfg);
    }
}
