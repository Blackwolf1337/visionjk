/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

/*
cg_voice.h -- the voice chat of the client: what cg_voice.c (the game side: network, codec, who is heard how)
asks of cg_voice_audio.c (the microphone and the speakers). Not from JA+, see docs/japlus/voice.md.

The sound devices run in threads of the audio library. Those threads only ever touch the ring buffers and
the settings behind the functions below, every function here is called from the thread of the game.
All sound is 16 bit, one channel, VOICE_RATE samples a second.

The client that runs in a browser (__EMSCRIPTEN__) has no threads and can not send UDP. There the devices are
the ones of the page, and the datagrams go through a WebSocket (cg_voice_web.c).
*/

#pragma once

#define VOICE_SPEAKERS			( 32 + 8 )	// one for every client number, MAX_CLIENTS, and one for every box of the boombox, BOX_MAX
#define VOICE_DEVICE_NAME		128
#define VOICE_MAX_DEVICES		16

int		CG_VoiceAudioInit( const char *backend );
void	CG_VoiceAudioShutdown( void );
int		CG_VoiceAudioDevices( int capture, char names[][VOICE_DEVICE_NAME], int max );

int		CG_VoiceAudioOpenPlayback( const char *device );
int		CG_VoiceAudioOpenCapture( const char *device );
void	CG_VoiceAudioCloseCapture( void );
int		CG_VoiceAudioCapturing( void );

int		CG_VoiceAudioRead( short *samples, int count );
void	CG_VoiceAudioDropCapture( void );

int		CG_VoiceAudioQueued( int speaker );
int		CG_VoiceAudioWrite( int speaker, const short *samples, int count );
void	CG_VoiceAudioMix( int speaker, float left, float right, float muffle );

void	CG_VoiceAudioTap( int on );
int		CG_VoiceAudioPlayed( void );
int		CG_VoiceAudioTapRead( short *stereo, int maxFrames );

#ifdef __EMSCRIPTEN__
// A page plays and records in the thread the game runs in, this many samples at a time (43 ms): more than a
// frame of the game lasts, so that a frame is not a gap in the sound. That much of a voice has to wait at the
// device in addition to what waits there anyway.
#define VOICE_PAGE_PERIOD		2048

// cg_voice_audio.c: the microphone of a page, which the browser has to give it
int			CG_VoiceAudioAsking( void );			// 1: CG_VoiceAudioOpenCapture() gave 0 because the player is being asked
const char	*CG_VoiceAudioNoMicrophone( void );

// cg_voice_web.c: the way to the relay, a WebSocket to the web host of the server in place of a UDP socket
int		CG_VoiceWebOpen( void );
void	CG_VoiceWebClose( void );
void	CG_VoiceWebSend( const byte *datagram, int len );
int		CG_VoiceWebReceive( byte *datagram, int size );
#endif
