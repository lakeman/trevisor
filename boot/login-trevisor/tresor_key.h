/*
 * TRESOR key derivation
 * 
 * Copyright (C) 2010	Tilo Mueller <tilo.mueller@informatik.uni-erlangen.de>
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


#ifndef __TRESOR_KEY
#define __TRESOR_KEY

typedef signed char             i8;
typedef signed short int        i16;
typedef signed int              i32;
typedef signed long long int    i64;
typedef unsigned char           u8;
typedef unsigned short int      u16;
typedef unsigned int            u32;
typedef unsigned long long int  u64;
typedef unsigned int            uint;
typedef unsigned long int       ulong;

void sha256(const char* message, int msglen, unsigned char* digest);
#endif
