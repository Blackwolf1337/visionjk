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

#pragma once

// wa_totp.c: codes by time, the second step of a login to the web admin panel

#define WA_TOTP_SECRET		20		// bytes, the length SHA-1 is made for
#define WA_TOTP_SECONDS		30
#define WA_HMAC_MESSAGE		64

void		WA_HmacSha1( const byte *key, int keyLen, const byte *message, int messageLen, byte *digest );
int			WA_TotpCode( const byte *secret, int secretLen, long long step );
long long	WA_TotpMatch( const byte *secret, int secretLen, const char *text, long long now );
void		WA_Base32( const byte *in, int len, char *out, int outSize );
