/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
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

#include "tr_common.h"

const int MAX_IMAGE_LOADERS = 10;
struct ImageLoaderMap
{
	const char *extension;
	ImageLoaderFn loader;
} imageLoaders[MAX_IMAGE_LOADERS];
int numImageLoaders;

/*
=================
Finds the image loader associated with the given extension.
=================
*/
const ImageLoaderMap *FindImageLoader ( const char *extension )
{
	for ( int i = 0; i < numImageLoaders; i++ )
	{
		if ( Q_stricmp (extension, imageLoaders[i].extension) == 0 )
		{
			return &imageLoaders[i];
		}
	}

	return NULL;
}

/*
=================
Adds a new image loader to load the specified image file extension.
The 'extension' string should not begin with a period (full stop).
=================
*/
qboolean R_ImageLoader_Add ( const char *extension, ImageLoaderFn imageLoader )
{
	if ( numImageLoaders >= MAX_IMAGE_LOADERS )
	{
		ri.Printf (PRINT_DEVELOPER, "R_AddImageLoader: Cannot add any more image loaders (maximum %d).\n", MAX_IMAGE_LOADERS);
		return qfalse;
	}

	if ( FindImageLoader (extension) != NULL )
	{
		ri.Printf (PRINT_DEVELOPER, "R_AddImageLoader: Image loader already exists for extension \"%s\".\n", extension);
		return qfalse;
	}

	ImageLoaderMap *newImageLoader = &imageLoaders[numImageLoaders];
	newImageLoader->extension = extension;
	newImageLoader->loader = imageLoader;

	numImageLoaders++;

	return qtrue;
}

/*
=================
Initializes the image loader, and adds the built-in
image loaders
=================
*/
void R_ImageLoader_Init()
{
	Com_Memset (imageLoaders, 0, sizeof (imageLoaders));
	numImageLoaders = 0;

	R_ImageLoader_Add ("jpg", LoadJPG);
	R_ImageLoader_Add ("png", LoadPNG);
	R_ImageLoader_Add ("tga", LoadTGA);
}

/*
=================
Loads any of the supported image types into a cannonical
32 bit format.
=================
*/
void R_LoadImage( const char *shortname, byte **pic, int *width, int *height ) {
	*pic = NULL;
	*width = 0;
	*height = 0;

	// Try loading the image with the original extension (if possible).
	const char *extension = COM_GetExtension (shortname);
	const ImageLoaderMap *imageLoader = FindImageLoader (extension);
	if ( imageLoader != NULL )
	{
		imageLoader->loader (shortname, pic, width, height);
		if ( *pic )
		{
			return;
		}
	}

	// Loop through all the image loaders trying to load this image.
	char extensionlessName[MAX_QPATH];
	COM_StripExtension(shortname, extensionlessName, sizeof( extensionlessName ));
	for ( int i = 0; i < numImageLoaders; i++ )
	{
		const ImageLoaderMap *tryLoader = &imageLoaders[i];
		if ( tryLoader == imageLoader )
		{
			// Already tried this one.
			continue;
		}

		const char *name = va ("%s.%s", extensionlessName, tryLoader->extension);
		tryLoader->loader (name, pic, width, height);
		if ( *pic )
		{
			return;
		}
	}
}

/*
=================
R_LoadImageFit

The picture of a file (any type R_LoadImage takes, a png of any size) in width x height RGBA for a module, its sides kept: it fills
one side and is centred on the other, where the pixels keep an alpha of 0. Shrunk, a pixel is the mean of the pixels
of the file it covers, grown, the mix of the four nearest, both weighted by their alpha, so the black of transparent
pixels does not darken an edge. qfalse: there is no such picture.
=================
*/
qboolean R_LoadImageFit( const char *name, byte *rgba, int width, int height ) {
	byte	*pic = NULL;
	int		w, h, fw, fh, ox, oy, x, y;

	if ( !Q_stricmp( COM_GetExtension( name ), "png" ) )
		LoadPNGPicture( name, &pic, &w, &h );
	else
		R_LoadImage( name, &pic, &w, &h );
	if ( !pic )
		return qfalse;
	memset( rgba, 0, width * height * 4 );
	if ( w * height >= h * width ) {
		fw = width;
		fh = Q_max( 1, h * width / w );
	} else {
		fh = height;
		fw = Q_max( 1, w * height / h );
	}
	ox = ( width - fw ) / 2;
	oy = ( height - fh ) / 2;
	if ( fw > w ) {
		for ( y = 0; y < fh; y++ ) {
			const float	fy = Com_Clamp( 0.0f, (float)( h - 1 ), ( y + 0.5f ) * h / fh - 0.5f );
			const int	y0 = (int)fy, y1 = Q_min( y0 + 1, h - 1 );
			const float	b = fy - y0;

			for ( x = 0; x < fw; x++ ) {
				const float	fx = Com_Clamp( 0.0f, (float)( w - 1 ), ( x + 0.5f ) * w / fw - 0.5f );
				const int	x0 = (int)fx, x1 = Q_min( x0 + 1, w - 1 );
				const float	a = fx - x0;
				const byte	*in[4] = { pic + ( y0 * w + x0 ) * 4, pic + ( y0 * w + x1 ) * 4, pic + ( y1 * w + x0 ) * 4, pic + ( y1 * w + x1 ) * 4 };
				const float	weight[4] = { ( 1.0f - a ) * ( 1.0f - b ), a * ( 1.0f - b ), ( 1.0f - a ) * b, a * b };
				byte		*out = rgba + ( ( oy + y ) * width + ox + x ) * 4;
				float		sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

				for ( int k = 0; k < 4; k++ ) {
					for ( int c = 0; c < 3; c++ )
						sum[c] += in[k][c] * in[k][3] * weight[k];
					sum[3] += in[k][3] * weight[k];
				}
				if ( sum[3] > 0.0f ) {
					for ( int c = 0; c < 3; c++ )
						out[c] = (byte)Com_Clamp( 0.0f, 255.0f, sum[c] / sum[3] );
				}
				out[3] = (byte)Com_Clamp( 0.0f, 255.0f, sum[3] );
			}
		}
		ri.Z_Free( pic );
		return qtrue;
	}
	for ( y = 0; y < fh; y++ ) {
		const int y0 = y * h / fh, y1 = Q_max( y0 + 1, ( y + 1 ) * h / fh );

		for ( x = 0; x < fw; x++ ) {
			const int	x0 = x * w / fw, x1 = Q_max( x0 + 1, ( x + 1 ) * w / fw );
			byte		*out = rgba + ( ( oy + y ) * width + ox + x ) * 4;
			unsigned	sum[4] = { 0, 0, 0, 0 };
			int			sx, sy;

			for ( sy = y0; sy < y1; sy++ ) {
				for ( sx = x0; sx < x1; sx++ ) {
					const byte *in = pic + ( sy * w + sx ) * 4;

					sum[0] += in[0] * in[3];
					sum[1] += in[1] * in[3];
					sum[2] += in[2] * in[3];
					sum[3] += in[3];
				}
			}
			if ( sum[3] ) {
				out[0] = (byte)( sum[0] / sum[3] );
				out[1] = (byte)( sum[1] / sum[3] );
				out[2] = (byte)( sum[2] / sum[3] );
			}
			out[3] = (byte)( sum[3] / ( ( y1 - y0 ) * ( x1 - x0 ) ) );
		}
	}
	ri.Z_Free( pic );
	return qtrue;
}
