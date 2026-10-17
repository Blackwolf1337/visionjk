/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "client.h"
#include "snd_local.h"
#include "snd_spatial.h"

portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
int 	*snd_p, snd_linear_count, snd_vol;
short	*snd_out;



void S_WriteLinearBlastStereo16 (void)
{
	int		i;
	int		val;

	for (i=0 ; i<snd_linear_count ; i+=2)
	{
		val = snd_p[i]>>8;
		if (val > 0x7fff)
			snd_out[i] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i] = (short)0x8000;
		else
			snd_out[i] = val;

		val = snd_p[i+1]>>8;
		if (val > 0x7fff)
			snd_out[i+1] = 0x7fff;
		else if (val < (short)0x8000)
			snd_out[i+1] = (short)0x8000;
		else
			snd_out[i+1] = val;
	}
}


void S_TransferStereo16 (unsigned long *pbuf, int endtime)
{
	int		lpos;
	int		ls_paintedtime;

	snd_p = (int *) paintbuffer;
	ls_paintedtime = s_paintedtime;

	while (ls_paintedtime < endtime)
	{
	// handle recirculating buffer issues
		lpos = ls_paintedtime & ((dma.samples>>1)-1);

		snd_out = (short *) pbuf + (lpos<<1);

		snd_linear_count = (dma.samples>>1) - lpos;
		if (ls_paintedtime + snd_linear_count > endtime)
			snd_linear_count = endtime - ls_paintedtime;

		snd_linear_count <<= 1;

	// write a linear blast of samples
		S_WriteLinearBlastStereo16 ();

		snd_p += snd_linear_count;
		ls_paintedtime += (snd_linear_count>>1);

		if( CL_VideoRecording( ) )
		{
			if ( cls.state == CA_ACTIVE || cl_forceavidemo->integer) {
				CL_WriteAVIAudioFrame( (byte *)snd_out, snd_linear_count << 1 );
			}
		}
	}
}

/*
===================
S_MufflePaint

v_muffle, 0 to 1: the whole mix of the game goes through a low pass, the more the higher the value. At 1 what
is above about 600 Hz is damped, at 0 nothing is done. A module sets it while something stands in front of the
game: the menu in the level of the cgame (cgame/menu3d). The value is approached over a fifth of a second, so
nothing clicks. Sounds, music and the sounds of snd_spatial.cpp all pass here.

The mixer paints ahead of what is heard and starts again right in front of it with every frame
(S_GetSoundtime), so most samples are painted many times. A filter that just runs on would begin each of those
paints with what it had at the end of the one before, a fifth of a second later in the sound: the samples then
come out different every time, and where the device goes from one paint to the next the wave jumps. That was a
crackle with every frame. So what the filter has after a sample is kept by the time of the sample, as
snd_spatial.cpp keeps its ring, and a paint goes on from the sample in front of its first.
===================
*/
#define MUFFLE_RING		32768		// samples, a power of two: more than the mixer is ever ahead
#define MUFFLE_STEPS	128

static void S_MufflePaint( int from, int to ) {
	static cvar_t	*v_muffle;
	static struct muffleSample_s {
		float	left, right;		// what the filter gave for this sample
		float	amount;				// and how far it was muffled, 0 to 1
	} ring[MUFFLE_RING];
	static int		ringFrom, ringTo;			// the ring has the samples of these times
	static float	pass[MUFFLE_STEPS + 1];		// the part of a new sample the filter takes, by the amount
	static int		passSpeed;
	float			target, step, left, right, amount = 0.0f;
	int				t;

	if ( !v_muffle ) {
		v_muffle = Cvar_Get( "v_muffle", "0", CVAR_TEMP, "0 to 1: the sound of the game is muffled, set by the cgame while its menu stands in the level" );
	}
	target = Com_Clamp( 0.0f, 1.0f, v_muffle->value );
	if ( to - from > PAINTBUFFER_SIZE ) {
		to = from + PAINTBUFFER_SIZE;
	}
	if ( to <= from ) {
		return;
	}

	// where the filter stood one sample earlier
	left = (float)paintbuffer[0].left;
	right = (float)paintbuffer[0].right;
	if ( from > ringFrom && from <= ringTo ) {
		const struct muffleSample_s *last = &ring[( from - 1 ) & ( MUFFLE_RING - 1 )];

		left = last->left;
		right = last->right;
		amount = last->amount;
	} else {
		ringFrom = ringTo = from;		// nothing is known of that sample: the sound started, or it was not muffled
	}
	if ( target <= 0.0f && amount <= 0.0f ) {
		ringFrom = ringTo = from;
		return;
	}

	if ( passSpeed != dma.speed ) {
		passSpeed = dma.speed;
		pass[0] = 1.0f;		// all of the new sample: nothing is changed
		for ( t = 1; t <= MUFFLE_STEPS; t++ ) {
			// the corner of the low pass goes from 16 kHz down to 600 Hz, evenly for the ear
			const float cutoff = 16000.0f * powf( 600.0f / 16000.0f, (float)t / MUFFLE_STEPS );

			pass[t] = 1.0f - expf( -2.0f * M_PI * cutoff / dma.speed );
		}
	}

	step = 1.0f / ( 0.2f * dma.speed );
	for ( t = from; t < to; t++ ) {
		portable_samplepair_t		*pair = &paintbuffer[t - from];
		struct muffleSample_s		*slot = &ring[t & ( MUFFLE_RING - 1 )];
		float						take;

		if ( amount < target ) {
			amount = Q_min( target, amount + step );
		} else if ( amount > target ) {
			amount = Q_max( target, amount - step );
		}
		take = pass[(int)( amount * MUFFLE_STEPS + 0.5f )];
		left += take * ( pair->left - left );
		right += take * ( pair->right - right );
		pair->left = (int)left;
		pair->right = (int)right;
		slot->left = left;
		slot->right = right;
		slot->amount = amount;
	}
	if ( to > ringTo ) {
		ringTo = to;
	}
	if ( ringTo - ringFrom > MUFFLE_RING ) {
		ringFrom = ringTo - MUFFLE_RING;
	}
}

/*
===================
S_TransferPaintBuffer

===================
*/
void S_TransferPaintBuffer(int endtime)
{
	int 	out_idx;
	int 	count;
	int 	out_mask;
	int 	*p;
	int 	step;
	int		val;
	unsigned long *pbuf;

	pbuf = (unsigned long *)dma.buffer;


	if ( s_testsound->integer ) {
		int		i;
		int		count;

		// write a fixed sine wave
		count = (endtime - s_paintedtime);
		for (i=0 ; i<count ; i++)
			paintbuffer[i].left = paintbuffer[i].right = (int)(sin((s_paintedtime+i)*0.1)*20000*256);
	}


	if (dma.samplebits == 16 && dma.channels == 2)
	{	// optimized case
		S_TransferStereo16 (pbuf, endtime);
	}
	else
	{	// general case
		p = (int *) paintbuffer;
		count = (endtime - s_paintedtime) * dma.channels;
		out_mask = dma.samples - 1;
		out_idx = s_paintedtime * dma.channels & out_mask;
		step = 3 - dma.channels;

		if (dma.samplebits == 16)
		{
			short *out = (short *) pbuf;
			while (count--)
			{
				val = *p >> 8;
				p+= step;
				if (val > 0x7fff)
					val = 0x7fff;
				else if (val < (short)0x8000)
					val = (short)0x8000;
				out[out_idx] = (short)val;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
		else if (dma.samplebits == 8)
		{
			unsigned char *out = (unsigned char *) pbuf;
			while (count--)
			{
				val = *p >> 8;
				p+= step;
				if (val > 0x7fff)
					val = 0x7fff;
				else if (val < (short)0x8000)
					val = (short)0x8000;
				out[out_idx] = (short)((val>>8) + 128);
				out_idx = (out_idx + 1) & out_mask;
			}
		}
	}
}


/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/
static void S_PaintChannelFrom16( channel_t *ch, const sfx_t *sfx, int count, int sampleOffset, int bufferOffset )
{
	portable_samplepair_t	*pSamplesDest;
	int iData;
	float ofst = sampleOffset;

	int iLeftVol	= ch->leftvol  * snd_vol;
	int iRightVol	= ch->rightvol * snd_vol;

	pSamplesDest	= &paintbuffer[ bufferOffset ];

	for ( int i=0 ; i<count ; i++ )
	{
		iData = sfx->pSoundData[ (int)ofst ];

		pSamplesDest[i].left  += (iData * iLeftVol )>>8;
		pSamplesDest[i].right += (iData * iRightVol)>>8;
		if (ch->doppler && ch->dopplerScale > 1) {
			ofst += 1 * ch->dopplerScale;
		} else {
			ofst++;
		}
	}
}


void S_PaintChannelFromMP3( channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset )
{
	int data;
	int leftvol, rightvol;
	signed short *sfx;
	int	i;
	portable_samplepair_t	*samp;
	static short tempMP3Buffer[PAINTBUFFER_SIZE];

	MP3Stream_GetSamples( ch, sampleOffset, count, tempMP3Buffer, qfalse );	// qfalse = not stereo

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;
	sfx = tempMP3Buffer;

	samp = &paintbuffer[ bufferOffset ];


	while ( count & 3 ) {
		data = *sfx;
		samp->left += (data * leftvol)>>8;
		samp->right += (data * rightvol)>>8;

		sfx++;
		samp++;
		count--;
	}

	for ( i=0 ; i<count ; i += 4 ) {
		data = sfx[i];
		samp[i].left += (data * leftvol)>>8;
		samp[i].right += (data * rightvol)>>8;

		data = sfx[i+1];
		samp[i+1].left += (data * leftvol)>>8;
		samp[i+1].right += (data * rightvol)>>8;

		data = sfx[i+2];
		samp[i+2].left += (data * leftvol)>>8;
		samp[i+2].right += (data * rightvol)>>8;

		data = sfx[i+3];
		samp[i+3].left += (data * leftvol)>>8;
		samp[i+3].right += (data * rightvol)>>8;
	}
}


/*
===============
S_SampleRun

For the sound of snd_spatial.cpp, which mixes on its own: up to count samples of a sound, the first at pos, the
next step further on each (in samples of the sound, fractions between them interpolated: that is the doppler of
snd_spatial.cpp), as floats from -1 to 1. A loop goes on from its end to its start in between two of its samples.
Returns how many there were before the sound ended, at most PAINTBUFFER_SIZE, step at most 2. A sound that is an
MP3 stream is decoded through the channel that plays it, without one it gives nothing, and it can not be read
back from where it was read before.
===============
*/
int S_SampleRun( sfx_t *sfx, channel_t *mp3Channel, double pos, float step, int count, qboolean loop, float *out )
{
	static short	tempMP3Buffer[PAINTBUFFER_SIZE * 2 + 2];
	const int		length = sfx->iSoundLengthInSamples;
	const short		*data;
	int				first = 0;		// the sample of the sound data[0] is

	if ( pos < 0.0 || pos >= length || step <= 0.0f )
	{
		return 0;
	}
	count = Q_min( count, PAINTBUFFER_SIZE );
	step = Q_min( step, 2.0f );

	if ( sfx->eSoundCompressionMethod == ct_16 )
	{
		data = sfx->pSoundData;
	}
	else if ( sfx->eSoundCompressionMethod == ct_MP3 && mp3Channel )
	{
		// what of it the samples asked for lie in, and the one after the last
		first = (int)pos;
		MP3Stream_GetSamples( mp3Channel, first, Q_min( (int)( pos + ( count - 1 ) * (double)step ) + 2, length ) - first,
			tempMP3Buffer, qfalse );	// qfalse = not stereo
		data = tempMP3Buffer;
	}
	else
	{
		return 0;
	}
	if ( !data )
	{
		return 0;
	}

	for ( int i = 0; i < count; i++, pos += step )
	{
		const int	at = (int)pos;
		float		a, b;

		if ( at >= length )
		{
			return i;
		}
		a = data[at - first];
		if ( at + 1 < length )
		{
			b = data[at + 1 - first];
		}
		else
		{
			b = ( loop && !first ) ? data[0] : 0.0f;	// a loop on into its start (loops are never MP3)
		}
		out[i] = ( a + ( b - a ) * (float)( pos - at ) ) * ( 1.0f / 32768.0f );
	}
	return count;
}


// subroutinised to save code dup (called twice)	-ste
//
void ChannelPaint(channel_t *ch, sfx_t *sc, int count, int sampleOffset, int bufferOffset)
{
	switch (sc->eSoundCompressionMethod)
	{
		case ct_16:

			S_PaintChannelFrom16		(ch, sc, count, sampleOffset, bufferOffset);
			break;

		case ct_MP3:

			S_PaintChannelFromMP3		(ch, sc, count, sampleOffset, bufferOffset);
			break;

		default:

			assert(0);	// debug aid, ignored in release. FIXME: Should we ERR_DROP here for badness-catch?
			break;
	}
}



void S_PaintChannels( int endtime ) {
	int 	i;
	int 	end;
	channel_t *ch;
	sfx_t	*sc;
	int		ltime, count;
	int		sampleOffset;
	int	normal_vol,voice_vol;

	snd_vol = normal_vol = s_volume->value*256;
	voice_vol  = (int)(s_volumeVoice->value*256);

//Com_Printf ("%i to %i\n", s_paintedtime, endtime);
	while ( s_paintedtime < endtime ) {
		// if paintbuffer is smaller than DMA buffer
		// we may need to fill it multiple times
		end = endtime;
		if ( endtime - s_paintedtime > PAINTBUFFER_SIZE ) {
			end = s_paintedtime + PAINTBUFFER_SIZE;
		}

		// clear the paint buffer to either music or zeros
		if ( s_rawend < s_paintedtime ) {
			if ( s_rawend ) {
				//Com_DPrintf ("background sound underrun\n");
			}
			memset(paintbuffer, 0, (end - s_paintedtime) * sizeof(portable_samplepair_t));
		} else {
			// copy from the streaming sound source
			int		s;
			int		stop;

			stop = (end < s_rawend) ? end : s_rawend;

			for ( i = s_paintedtime ; i < stop ; i++ ) {
				s = i&(MAX_RAW_SAMPLES-1);
				paintbuffer[i-s_paintedtime] = s_rawsamples[s];
			}
//		if (i != end)
//			Com_Printf ("partial stream\n");
//		else
//			Com_Printf ("full stream\n");
			for ( ; i < end ; i++ ) {
				paintbuffer[i-s_paintedtime].left =
				paintbuffer[i-s_paintedtime].right = 0;
			}
		}

		// paint in the channels.
		ch = s_channels;
		for ( i = 0; i < MAX_CHANNELS ; i++, ch++ ) {
			if ( !ch->thesfx || (ch->leftvol<0.25 && ch->rightvol<0.25 )) {
				continue;
			}
			// a sound in the level that snd_spatial.cpp plays (v_spatial)
			if ( ch->spatial ) {
				continue;
			}

			if ( ch->entchannel == CHAN_VOICE || ch->entchannel == CHAN_VOICE_ATTEN || ch->entchannel == CHAN_VOICE_GLOBAL )
				snd_vol = voice_vol;
			else
				snd_vol = normal_vol;

			ltime = s_paintedtime;
			sc = ch->thesfx;

			// we might have to make 2 passes if it is
			//	a looping sound effect and the end of
			//	the sameple is hit...
			//
			do
			{
				if (ch->loopSound) {
					sampleOffset = ltime % sc->iSoundLengthInSamples;
				} else {
					sampleOffset = ltime - ch->startSample;
				}

				count = end - ltime;
				if ( ch->doppler && ch->dopplerScale > 1 ) {
					if ( sampleOffset + (count * ch->dopplerScale) > sc->iSoundLengthInSamples ) {
						count = (sc->iSoundLengthInSamples - sampleOffset) / ch->dopplerScale;

						// avoid infinite loop once length of remaining pSoundData (numerator)
						//	is smaller than dopplerScale (denominator), resulting in 0.
						if ( count == 0 ) {
							break;
						}
					}
				} else {
					if ( sampleOffset + count > sc->iSoundLengthInSamples ) {
						count = sc->iSoundLengthInSamples - sampleOffset;
					}
				}

				if ( count > 0 ) {
					ChannelPaint(ch, sc, count, sampleOffset, ltime - s_paintedtime);
					ltime += count;
				}
			} while ( ltime < end && ch->loopSound );
		}
/* temprem
		// paint in the looped channels.
		ch = loop_channels;
		for ( i = 0; i < numLoopChannels ; i++, ch++ ) {
			if ( !ch->thesfx || (!ch->leftvol && !ch->rightvol )) {
				continue;
			}

			{

				ltime = s_paintedtime;
				sc = ch->thesfx;

				if (sc->soundData==NULL || sc->soundLength==0) {
					continue;
				}
				// we might have to make two passes if it
				// is a looping sound effect and the end of
				// the sample is hit
				do {
					sampleOffset = (ltime % sc->soundLength);

					count = end - ltime;
					if ( sampleOffset + count > sc->soundLength ) {
						count = sc->soundLength - sampleOffset;
					}

					if ( count > 0 )
					{
						ChannelPaint(ch, sc, count, sampleOffset, ltime - s_paintedtime);
						ltime += count;
					}

				} while ( ltime < end);
			}
		}
*/
		// the sounds in the level that snd_spatial.cpp plays
		if ( S_SpatialActive() ) {
			S_SpatialPaint( s_paintedtime, end, paintbuffer );
		}

		// muffled while a module asks for it
		S_MufflePaint( s_paintedtime, end );

		// transfer out according to DMA format
		S_TransferPaintBuffer( end );
		s_paintedtime = end;
	}
}
