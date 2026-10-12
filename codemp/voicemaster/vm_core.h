/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// vm_core.h -- the voice master without a socket (vm_core.c). Read it after the socket headers of the file
// (game/bg_net.h, or what the engine has) and after q_shared.h.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define VM_DEFAULT_KBPS		4000

typedef void (*vmSend_t)( const struct sockaddr_in *to, const byte *data, int len );
typedef void (*vmPrint_t)( const char *line );

qboolean	VM_Init( vmSend_t send, vmPrint_t print, int capBytes, int64_t now );
void		VM_Shutdown( int64_t now );
void		VM_Datagram( const struct sockaddr_in *from, const byte *data, int len, int64_t now );
void		VM_Frame( int64_t now );
void		VM_SetTemple( const byte *server );

#ifdef __cplusplus
}
#endif
