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

// bg_auth.h -- admin login with a key, the part the game module and the cgame share. Not from JA+.
//
// An admin has an Ed25519 key, the kind ssh uses. The server knows the public half (a line of admin_keys.txt, as
// a line of authorized_keys looks), the client has the secret half in a file or lets an ssh-agent sign with it.
// No password goes over the wire, and nothing the server learns lets it log in anywhere else.
//
//   client   vs_auth hello <public key>               the 32 bytes as base64
//   server   vs_auth nonce <nonce>                    32 random bytes as base64, good for one answer and 30 s
//            vs_auth unknown                          no such key in admin_keys.txt
//   client   vs_auth sign <signature> <address>       the 64 bytes as base64, the address as the client sees it
//   server   vs_auth ok <rank>
//            vs_auth fail <why>
//
// What is signed (BG_AuthMessage): a fixed first line, the nonce, the address of the server as the client sees
// it and the number of the client. The first line is text, so the message can never be mistaken for what ssh
// lets a key sign when it logs in somewhere (that starts with a length of four bytes): a server can not use
// this login to get a signature that is worth something elsewhere. The address is in it against a server that
// passes the nonce of another server on: the real one can compare it with its own (vs_authHosts).
//
// Sizes are the ones of Ed25519. A secret key is kept the way Monocypher and the key files of OpenSSH keep it:
// the 32 bytes of the seed and behind them the 32 bytes of the public key.

#define AUTH_PUBLIC_SIZE		32
#define AUTH_SECRET_SIZE		64
#define AUTH_SIGNATURE_SIZE		64
#define AUTH_NONCE_SIZE			32
#define AUTH_BASE64_SIZE		96		// room for the longest of them as base64 with its end
#define AUTH_MESSAGE_SIZE		192
#define AUTH_ADDRESS_SIZE		64
#define AUTH_LINE_SIZE			256		// "ssh-ed25519 <68 characters> <comment>"
#define AUTH_FILE_SIZE			1024	// a key file of OpenSSH with an Ed25519 key has about 400 bytes
#define AUTH_NONCE_MSEC			30000

#define AUTH_CAPS_BIT			4		// in the serverinfo vs_caps: this server knows the login

// bytes nobody can guess, from the system. qfalse: there are none, and nothing may be made from the buffer.
qboolean	BG_AuthRandom( byte *out, int size );

// The message a client signs and a server checks. Returns its length, 0: it does not fit.
int			BG_AuthMessage( byte *out, int outSize, const byte *nonce, const char *address, int clientNum );

// "ssh-ed25519 AAAA... comment", as ssh-keygen writes it and authorized_keys has it. The line may start
// anywhere in front of the "ssh-ed25519". qfalse: this is no such line.
qboolean	BG_AuthParsePublicLine( const char *line, byte *pub, char *comment, int commentSize );
void		BG_AuthPublicLine( const byte *pub, const char *comment, char *out, int outSize );

// "SHA256:..." as ssh-keygen -l and 1Password show a key
void		BG_AuthFingerprint( const byte *pub, char *out, int outSize );

// the public key of a "vs_auth hello" and the like: base64 of exactly size bytes
qboolean	BG_AuthDecode( const char *text, byte *out, int size );
void		BG_AuthEncode( const byte *in, int size, char *out, int outSize );

// A key file of OpenSSH ("-----BEGIN OPENSSH PRIVATE KEY-----") with an Ed25519 key and no passphrase.
// qfalse and a reason in why: it is something else.
qboolean	BG_AuthReadKeyFile( const char *text, byte *secret, char *comment, int commentSize, char *why, int whySize );
// Writes one. Returns the length of the text, 0: it does not fit.
int			BG_AuthWriteKeyFile( const byte *secret, const char *comment, char *out, int outSize );
