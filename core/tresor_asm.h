/***************************************************************************
 *
 * Tresor set/get to debug registers
 * 
 * 
 * Copyright (C) 2010   Tilo Mueller <tilo.mueller@informatik.uni-erlangen.de>
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
 *
 ***************************************************************************/

#ifndef _CORE_TRESOR_H
#define _CORE_TRESOR_H
void tresor_set_key(volatile const u8 *in_key);
void tresor_get_key(volatile const u8 *out_key);
int tresor_capable(void);
#endif
