/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

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
*/

#pragma once

#define VOICE_SPEAKERS			32		// one for every client number, MAX_CLIENTS
#define VOICE_DEVICE_NAME		128
#define VOICE_MAX_DEVICES		16

int		CG_VoiceAudioInit( const char *backend );
void	CG_VoiceAudioShutdown( void );
int		CG_VoiceAudioDevices( int capture, char names[][VOICE_DEVICE_NAME], int max );

int		CG_VoiceAudioOpenPlayback( const char *device );
int		CG_VoiceAudioOpenCapture( const char *device );
void	CG_VoiceAudioCloseCapture( void );
int		CG_VoiceAudioCapturing( void );
int		CG_VoiceAudioMicDenied( void );

int		CG_VoiceAudioRead( short *samples, int count );
void	CG_VoiceAudioDropCapture( void );

int		CG_VoiceAudioQueued( int speaker );
int		CG_VoiceAudioWrite( int speaker, const short *samples, int count );
void	CG_VoiceAudioMix( int speaker, float left, float right, float muffle );

void	CG_VoiceAudioTap( int on );
int		CG_VoiceAudioTapRead( short *stereo, int maxFrames );
