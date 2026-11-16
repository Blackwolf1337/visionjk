/*
===========================================================================
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

// vk_paint.cpp -- the marks on a tessellated surface painted onto its bumps (VK_PaintMarks, called by
// RB_StageIteratorGeneric in vk_shade.cpp); its draws are made with the helpers of vk_shade.cpp.
#include "vk_local.h"

/*
===============
VK_MarkRows

One poly of a mark for generic.frag Decals(), into out (NULL: only how many rows it takes). Rows: the two axes of its
plane with their offsets (u = dot( row0.xyz, p ) - row0.w, v likewise), its plane (normal, distance), its colour, a
sphere around it and the reach of the bumps (centre, radius), its number of corners (and, from VK_PaintBuild, the
row its styles begin at, how many they are and its cluster), the box round its corners in u and v (lowest, highest),
then each corner as u, v, s, t. 0: a poly too thin to have a plane.
===============
*/
#define VK_MARK_HEADER_ROWS		7
static int VK_MarkRows( const srfPoly_t *poly, float reach, float *out ) {
	const polyVert_t	*v = poly->verts;
	vec3_t				e1, e2, cross, n, u, w, centre;
	float				area = 0.0f, radius = 0.0f;
	int					best = -1;

	// the plane from the largest triangle of the fan
	for ( int i = 1; i < poly->numVerts - 1; i++ ) {
		VectorSubtract( v[i].xyz, v[0].xyz, e1 );
		VectorSubtract( v[i + 1].xyz, v[0].xyz, e2 );
		CrossProduct( e1, e2, cross );
		const float a = DotProduct( cross, cross );		// (the square of twice its area)
		if ( a > area ) {
			area = a;
			best = i;
			VectorCopy( cross, n );
		}
	}
	if ( best < 0 || area < 1e-8f ) {
		return 0;
	}
	if ( !out ) {
		return VK_MARK_HEADER_ROWS + poly->numVerts;
	}
	VectorScale( n, 1.0f / sqrtf( area ), n );
	// (an axis across the normal: from the axis of the world the normal points along the least)
	{
		const int least = ( fabsf( n[0] ) < fabsf( n[1] ) ) ? ( fabsf( n[0] ) < fabsf( n[2] ) ? 0 : 2 ) : ( fabsf( n[1] ) < fabsf( n[2] ) ? 1 : 2 );
		vec3_t axis = { 0.0f, 0.0f, 0.0f };

		axis[least] = 1.0f;
		CrossProduct( n, axis, u );
		VectorNormalize( u );
	}
	CrossProduct( n, u, w );
	VectorClear( centre );
	for ( int i = 0; i < poly->numVerts; i++ ) {
		VectorAdd( centre, v[i].xyz, centre );
	}
	VectorScale( centre, 1.0f / poly->numVerts, centre );
	for ( int i = 0; i < poly->numVerts; i++ ) {
		radius = Q_max( radius, DistanceSquared( centre, v[i].xyz ) );
	}

	VectorCopy( u, out );		out[3] = DotProduct( u, v[0].xyz );
	VectorCopy( w, out + 4 );	out[7] = DotProduct( w, v[0].xyz );
	VectorCopy( n, out + 8 );	out[11] = DotProduct( n, v[0].xyz );
	for ( int c = 0; c < 4; c++ ) {
		out[12 + c] = v[0].modulate[c] / 255.0f;
	}
	VectorCopy( centre, out + 16 );	out[19] = sqrtf( radius + reach * reach );
	out[20] = (float)poly->numVerts;	out[21] = out[22] = out[23] = 0.0f;
	float *box = out + 24;
	out += 4 * VK_MARK_HEADER_ROWS;
	box[0] = box[1] = box[2] = box[3] = 0.0f;
	for ( int i = 0; i < poly->numVerts; i++, out += 4 ) {
		out[0] = DotProduct( u, v[i].xyz ) - DotProduct( u, v[0].xyz );
		out[1] = DotProduct( w, v[i].xyz ) - DotProduct( w, v[0].xyz );
		out[2] = v[i].st[0];
		out[3] = v[i].st[1];
		box[0] = Q_min( box[0], out[0] );
		box[1] = Q_min( box[1], out[1] );
		box[2] = Q_max( box[2], out[0] );
		box[3] = Q_max( box[3], out[1] );
	}
	return VK_MARK_HEADER_ROWS + poly->numVerts;
}

/*
===============
VK_PaintMarks

VisionJK: the marks on the tessellated surface of this batch (srfPoly_t tessShader, tr_marks.cpp) painted onto
its bumps. A mark laid flat as its polys lies under the bumps, and a poly cut into other triangles than the
surface never follows them exactly. So the surface itself is drawn again, through the same tessellation as its
stage (VK_TessPass, depth EQUAL): once for all that the marks take from it or lay over it, and once for all the
light they add, however many marks and shaders there are. (A pass for each run of marks and each stage of their
shaders drew the ground many times over: a cut on the sand of ffa3 ran the fragment shader 8 times a pixel of the
whole frame.)

generic.frag Decals() finds the marks of a pixel in a table: each mark lies in the cells of a flat grid across the
axis its normal points along the most (the bumps move a place along that normal, which leaves its cell alone), and
the cells are hashed into the buckets of that axis, as many as it has cells with marks (a power of two; with fewer,
the marks of far cells share a bucket and every pixel goes through them). A pixel looks into one bucket an axis and goes
through the few marks there: the poly it lies in, seen along the normal of the poly, its texture coordinates as the
poly would have had them, and for each stage of the shader of the mark (a style) the texture moved as the stage
says (tcMod), coloured as the stage says (rgbGen, alphaGen), put together as its blend says, lit as a groove where
the stage is one.

The ground of a level may be a few triangles as large as the view, so leaving triangles out (generic.tesc does,
by the box of the pass) saves little there, and a pass over the whole view for a cut of a few units was most of
what the marks cost. So the marks are sorted into clusters by where they lie (cells of VK_PAINT_CLUSTER units),
and each cluster gets its passes inside the rectangle of the screen its box covers (the scissor), painting its own
marks only. Where the rectangles together are larger than the view (marks everywhere), all are painted at once.

The rows, from vkDraw_t decals on: how many buckets each axis has, then the table, axis after axis (two buckets a
row: first element and count, twice). The elements
(four a row) are the rows marks begin at (VK_MarkRows), the marks name the rows their styles begin at
(VK_PAINT_STYLE_ROWS each: texture, kind, depth of the groove, its height map or -1; baseColor; vertColor; texMat0
twice; texTurb0) and their cluster.
===============
*/
#define VK_MAX_PAINTED_MARKS	8192
#define VK_PAINT_SURFACES		16		// tessellated surfaces with marks in one scene
#define VK_PAINT_GROUPS			32		// shaders of marks on one surface
#define VK_PAINT_STYLES			64		// their stages
#define VK_PAINT_STYLE_ROWS		6
#define VK_PAINT_AXIS_BUCKETS	32768	// the most buckets of an axis
#define VK_PAINT_CLUSTERS		32
#define VK_PAINT_CLUSTER		64.0f	// units a cell of the clusters is wide
#define VK_PAINT_ELEMENTS		65536	// most cells all marks of a surface may lie in; with more the cells get larger
#define VK_PAINT_MARK_ROWS		131072	// rows of the styles and marks of one surface
#define VK_PAINT_TABLE_ROWS		( 1 + ( 3 * VK_PAINT_AXIS_BUCKETS + 1 ) / 2 + ( VK_PAINT_ELEMENTS + 3 ) / 4 )

typedef struct {
	vec3_t		lo, hi;					// the box round its marks and how far the bumps move the surface
	qboolean	takes, adds;			// it has marks that change what is there, that add light
	int			cell[3];
} vkPaintCluster_t;

// The marks on one tessellated surface in one scene, laid into the frame once: a large surface is drawn in several
// batches, and each of them reads the same rows.
typedef struct {
	const shader_t	*surface;
	int				frameCount, frameSceneNum;
	float			reach;
	qboolean		grooves;
	uint32_t		table;				// the row the table begins at
	float			cell;
	int				numClusters;		// (0: no marks)
	vkPaintCluster_t	clusters[VK_PAINT_CLUSTERS];
	vkPaintCluster_t	all;
} vkPaintSurface_t;

typedef struct {
	uint32_t	row;
	int			axis;
	float		lo[2], hi[2];			// across the axis, with what the bumps may move it
} vkPaintMark_t;

// (base: the first bucket of the axis, size: how many it has; generic.frag Decals() does the same)
static inline uint32_t VK_PaintBucket( uint32_t base, uint32_t size, int cx, int cy ) {
	return base + ( ( (uint32_t)cx * 73856093u ^ (uint32_t)cy * 19349663u ) & ( size - 1 ) );
}

// how the marks of a stage are put together in generic.frag Decals(), from its blend: 0 the colour takes away
// (filter), 1 times (modulate), 2 over by alpha, 3 added, 4 times rgb + 1 - alpha (the saber scars), 5 times twice
// the colour, 6 added by alpha
static int VK_PaintKind( uint32_t stateBits ) {
	const uint32_t src = stateBits & GLS_SRCBLEND_BITS, dst = stateBits & GLS_DSTBLEND_BITS;

	if ( src == GLS_SRCBLEND_DST_COLOR && dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) {
		return 4;
	}
	if ( src == GLS_SRCBLEND_DST_COLOR && dst == GLS_DSTBLEND_SRC_COLOR ) {
		return 5;
	}
	if ( src == GLS_SRCBLEND_ZERO && dst == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) {
		return 0;
	}
	if ( ( src == GLS_SRCBLEND_DST_COLOR && dst == GLS_DSTBLEND_ZERO ) || ( src == GLS_SRCBLEND_ZERO && dst == GLS_DSTBLEND_SRC_COLOR ) ) {
		return 1;
	}
	if ( dst == GLS_DSTBLEND_ONE ) {
		// added, by its alpha or as it is
		return ( src == GLS_SRCBLEND_SRC_ALPHA ) ? 6 : 3;
	}
	return 2;
}

static const vkPaintSurface_t *VK_PaintBuild( const shader_t *surface, float reach, float shift ) {
	static vkPaintSurface_t	cache[VK_PAINT_SURFACES];
	static int				nextCache;
	static const srfPoly_t	*marks[VK_MAX_PAINTED_MARKS];
	static byte				markGroup[VK_MAX_PAINTED_MARKS];
	static vkPaintMark_t	laid[VK_MAX_PAINTED_MARKS];
	static uint32_t			counts[3 * VK_PAINT_AXIS_BUCKETS], firsts[3 * VK_PAINT_AXIS_BUCKETS];
	static int				lastMark[3 * VK_PAINT_AXIS_BUCKETS];
	// The rows are put together here and copied into the frame once: the memory of the frame is for writing in a
	// row, and reading what was written there back (the normal and the sphere of a mark, the box of its corners)
	// took 2.6 ms for the marks of three cuts.
	static float			markScratch[VK_PAINT_MARK_ROWS * 4], tableScratch[VK_PAINT_TABLE_ROWS * 4];
	float					styles[VK_PAINT_STYLES][VK_PAINT_STYLE_ROWS * 4];
	int						groupStyle[VK_PAINT_GROUPS], groupStyles[VK_PAINT_GROUPS];
	qboolean				groupTakes[VK_PAINT_GROUPS], groupAdds[VK_PAINT_GROUPS];
	qhandle_t				groupShader[VK_PAINT_GROUPS];
	vkPaintSurface_t		*ps;
	int						numMarks = 0, numGroups = 0, numStyles = 0, numLaid = 0, markRows = 0;

	for ( int i = 0; i < VK_PAINT_SURFACES; i++ ) {
		ps = &cache[i];
		if ( ps->surface == surface && ps->frameCount == backEnd.viewParms.frameCount
			&& ps->frameSceneNum == backEnd.viewParms.frameSceneNum && ps->reach == reach ) {
			return ps;
		}
	}
	ps = &cache[nextCache];
	nextCache = ( nextCache + 1 ) % VK_PAINT_SURFACES;
	ps->surface = surface;
	ps->frameCount = backEnd.viewParms.frameCount;
	ps->frameSceneNum = backEnd.viewParms.frameSceneNum;
	ps->reach = reach;
	ps->grooves = qfalse;
	ps->numClusters = 0;

	// the marks on this surface, each with the group of its shader; the groups in the order their shaders came first,
	// which is the order they lie over each other in (the char of a scar under its glow)
	for ( int i = 0; i < backEnd.refdef.numPolys && numMarks < VK_MAX_PAINTED_MARKS; i++ ) {
		const srfPoly_t *poly = &backEnd.refdef.polys[i];
		int g;

		if ( !poly->tessShader || ( poly->tessShader != surface && poly->tessShader->remappedShader != surface ) ) {
			continue;
		}
		for ( g = 0; g < numGroups && groupShader[g] != poly->hShader; g++ ) {
		}
		if ( g == numGroups ) {
			if ( numGroups == VK_PAINT_GROUPS ) {
				continue;
			}
			const shader_t *markShader = R_GetShaderByHandle( poly->hShader );

			if ( markShader->remappedShader ) {
				markShader = markShader->remappedShader;
			}
			groupShader[g] = poly->hShader;
			groupStyle[g] = numStyles;
			groupTakes[g] = groupAdds[g] = qfalse;
			// a style for each stage of the shader
			for ( int st = 0; st < markShader->numUnfoggedPasses && markShader->stages[st].active && numStyles < VK_PAINT_STYLES; st++ ) {
				const shaderStage_t *pass = &markShader->stages[st];
				float *style = styles[numStyles];
				vkDraw_t draw;

				if ( !pass->bundle[0].image ) {
					continue;
				}
				const int kind = VK_PaintKind( pass->stateBits );
				VK_InitDraw( &draw, 0, 0 );
				// its colours as baseColor + vertColor times the colour of the mark (rgbGen and alphaGen of the stage),
				// its texture moving as the stage says (tcMod)
				VK_StageColors( pass, 0, &draw );
				VK_BundleTexCoords( &pass->bundle[0], draw.texMat0, draw.texTurb0 );
				style[0] = (float)VK_BundleTexture( &pass->bundle[0] );
				style[1] = (float)kind;
				style[2] = 0.0f;
				style[3] = -1.0f;
				// a groove (stageMaterial_t groove), its height from its map or from how dark it is
				if ( pass->mat && pass->mat->groove > 0.0f && kind == 4 && v_pbr->integer ) {
					style[2] = pass->mat->groove;
					if ( pass->mat->grooveMap ) {
						style[3] = (float)pass->mat->grooveMap->texnum;
					}
					ps->grooves = qtrue;
				}
				VectorCopy4( draw.baseColor, style + 4 );
				VectorCopy4( draw.vertColor, style + 8 );
				memcpy( style + 12, draw.texMat0, sizeof( float ) * 8 );
				VectorCopy4( draw.texTurb0, style + 20 );
				if ( kind == 3 || kind == 6 ) {
					groupAdds[g] = qtrue;
				} else {
					groupTakes[g] = qtrue;
					if ( kind == 2 ) {
						groupAdds[g] = qtrue;
					}
				}
				numStyles++;
			}
			groupStyles[g] = numStyles - groupStyle[g];
			numGroups++;
		}
		// (the most rows it may take: a poly too thin to have a plane takes none, VK_MarkRows says when it is laid)
		const int polyRows = groupStyles[g] ? VK_MARK_HEADER_ROWS + poly->numVerts : 0;
		if ( polyRows && VK_PAINT_STYLE_ROWS * VK_PAINT_STYLES + markRows + polyRows <= VK_PAINT_MARK_ROWS ) {
			markRows += polyRows;
			markGroup[numMarks] = (byte)g;
			marks[numMarks++] = poly;
		}
	}
	if ( !numMarks ) {
		return ps;
	}

	// the styles and the marks, group after group
	uint32_t first;
	float *frameRows = VK_FrameRows( VK_PAINT_STYLE_ROWS * numStyles + markRows, &first );
	if ( !frameRows ) {
		return ps;
	}
	const uint32_t styleRow = first;
	float *rows = markScratch, *out = rows;
	int numClusters = 0;

	memcpy( out, styles, sizeof( float ) * 4 * VK_PAINT_STYLE_ROWS * numStyles );
	out += 4 * VK_PAINT_STYLE_ROWS * numStyles;
	ClearBounds( ps->all.lo, ps->all.hi );
	ps->all.takes = ps->all.adds = qfalse;
	for ( int g = 0; g < numGroups; g++ ) {
		for ( int i = 0; i < numMarks; i++ ) {
			if ( markGroup[i] != g ) {
				continue;
			}
			const srfPoly_t *poly = marks[i];
			const int n = VK_MarkRows( poly, reach, out );
			vkPaintMark_t *m = &laid[numLaid];

			if ( !n ) {
				continue;
			}
			const float *normal = out + 8;
			int a, b;

			out[21] = (float)( styleRow + VK_PAINT_STYLE_ROWS * groupStyle[g] );
			out[22] = (float)groupStyles[g];
			m->row = first + (uint32_t)( ( out - rows ) / 4 );
			m->axis = ( fabsf( normal[0] ) > fabsf( normal[1] ) ) ? ( fabsf( normal[0] ) > fabsf( normal[2] ) ? 0 : 2 )
				: ( fabsf( normal[1] ) > fabsf( normal[2] ) ? 1 : 2 );
			a = ( m->axis == 0 ) ? 1 : 0;
			b = ( m->axis == 2 ) ? 1 : 2;
			// the bumps move a place along the normal of the surface (by shift at most): across the axis by no more
			// than this
			const float slide = shift * sqrtf( Q_max( 0.0f, 1.0f - normal[m->axis] * normal[m->axis] ) ) + 0.05f;
			m->lo[0] = m->hi[0] = poly->verts[0].xyz[a];
			m->lo[1] = m->hi[1] = poly->verts[0].xyz[b];
			for ( int v = 1; v < poly->numVerts; v++ ) {
				m->lo[0] = Q_min( m->lo[0], poly->verts[v].xyz[a] );
				m->hi[0] = Q_max( m->hi[0], poly->verts[v].xyz[a] );
				m->lo[1] = Q_min( m->lo[1], poly->verts[v].xyz[b] );
				m->hi[1] = Q_max( m->hi[1], poly->verts[v].xyz[b] );
			}
			m->lo[0] -= slide;	m->lo[1] -= slide;
			m->hi[0] += slide;	m->hi[1] += slide;
			// its cluster: the one of the cell it lies in, or with all clusters taken the nearest
			int cellAt[3], c, best = 0;
			float bestDistance = 0.0f;
			for ( int k = 0; k < 3; k++ ) {
				cellAt[k] = (int)floorf( out[16 + k] / VK_PAINT_CLUSTER );
			}
			for ( c = 0; c < numClusters; c++ ) {
				const int *at = ps->clusters[c].cell;
				const float d = (float)( at[0] - cellAt[0] ) * ( at[0] - cellAt[0] ) + (float)( at[1] - cellAt[1] ) * ( at[1] - cellAt[1] )
					+ (float)( at[2] - cellAt[2] ) * ( at[2] - cellAt[2] );

				if ( d == 0.0f ) {
					break;
				}
				if ( !c || d < bestDistance ) {
					bestDistance = d;
					best = c;
				}
			}
			if ( c == numClusters ) {
				if ( numClusters < VK_PAINT_CLUSTERS ) {
					vkPaintCluster_t *cluster = &ps->clusters[numClusters++];

					ClearBounds( cluster->lo, cluster->hi );
					cluster->takes = cluster->adds = qfalse;
					memcpy( cluster->cell, cellAt, sizeof( cluster->cell ) );
				} else {
					c = best;
				}
			}
			out[23] = (float)c;
			for ( int k = 0; k < 2; k++ ) {
				vkPaintCluster_t *cluster = k ? &ps->all : &ps->clusters[c];

				for ( int v = 0; v < poly->numVerts; v++ ) {
					for ( int x = 0; x < 3; x++ ) {
						cluster->lo[x] = Q_min( cluster->lo[x], poly->verts[v].xyz[x] - shift - 1.0f );
						cluster->hi[x] = Q_max( cluster->hi[x], poly->verts[v].xyz[x] + shift + 1.0f );
					}
				}
				cluster->takes = (qboolean)( cluster->takes || groupTakes[g] );
				cluster->adds = (qboolean)( cluster->adds || groupAdds[g] );
			}
			numLaid++;
			out += 4 * n;
		}
	}

	// cells small enough that a pixel finds few marks in its own, large enough that all marks fit the elements
	float cell = 4.0f;
	uint32_t elements, axisElements[3];
	for ( ;; cell *= 2.0f ) {
		axisElements[0] = axisElements[1] = axisElements[2] = 0;
		for ( int i = 0; i < numLaid; i++ ) {
			const vkPaintMark_t *m = &laid[i];

			axisElements[m->axis] += (uint32_t)( (int)floorf( m->hi[0] / cell ) - (int)floorf( m->lo[0] / cell ) + 1 )
				* (uint32_t)( (int)floorf( m->hi[1] / cell ) - (int)floorf( m->lo[1] / cell ) + 1 );
		}
		elements = axisElements[0] + axisElements[1] + axisElements[2];
		if ( elements <= VK_PAINT_ELEMENTS || cell >= 4096.0f ) {
			break;
		}
	}
	if ( elements > VK_PAINT_ELEMENTS ) {
		return ps;
	}
	// the buckets of each axis: none without marks
	uint32_t size[3], base[3], buckets = 0;
	for ( int a = 0; a < 3; a++ ) {
		size[a] = 0;
		if ( axisElements[a] ) {
			for ( size[a] = 64; size[a] < axisElements[a] && size[a] < VK_PAINT_AXIS_BUCKETS; size[a] *= 2 ) {
			}
		}
		base[a] = buckets;
		buckets += size[a];
	}
	const uint32_t tableRows = 1 + ( buckets + 1 ) / 2;
	uint32_t table;
	float *frameTable = VK_FrameRows( tableRows + ( elements + 3 ) / 4, &table );
	if ( !frameTable ) {
		return ps;
	}
	float *head = tableScratch, *list = head + 4 * tableRows;
	const uint32_t firstElement = ( table + tableRows ) * 4;

	head[0] = (float)size[0];	head[1] = (float)size[1];	head[2] = (float)size[2];	head[3] = 0.0f;
	// how many marks each bucket gets (a mark once, though several of its cells may fall into one), where each
	// begins, then the marks in the order they were laid
	for ( int fill = 0; fill < 2; fill++ ) {
		if ( fill ) {
			uint32_t at = firstElement;

			for ( uint32_t k = 0; k < buckets; k++ ) {
				firsts[k] = at;
				head[4 + 4 * ( k / 2 ) + 2 * ( k & 1 )] = (float)at;
				head[4 + 4 * ( k / 2 ) + 2 * ( k & 1 ) + 1] = (float)counts[k];
				at += counts[k];
			}
			if ( buckets & 1 ) {
				head[4 + 4 * ( buckets / 2 ) + 2] = head[4 + 4 * ( buckets / 2 ) + 3] = 0.0f;
			}
		}
		memset( counts, 0, sizeof( counts[0] ) * buckets );
		memset( lastMark, -1, sizeof( lastMark[0] ) * buckets );
		for ( int i = 0; i < numLaid; i++ ) {
			const vkPaintMark_t *m = &laid[i];
			const int x1 = (int)floorf( m->hi[0] / cell ), y1 = (int)floorf( m->hi[1] / cell );

			for ( int x = (int)floorf( m->lo[0] / cell ); x <= x1; x++ ) {
				for ( int y = (int)floorf( m->lo[1] / cell ); y <= y1; y++ ) {
					const uint32_t k = VK_PaintBucket( base[m->axis], size[m->axis], x, y );

					if ( lastMark[k] == i ) {
						continue;
					}
					lastMark[k] = i;
					if ( fill ) {
						list[firsts[k] - firstElement + counts[k]] = (float)m->row;
					}
					counts[k]++;
				}
			}
		}
	}
	memcpy( frameRows, markScratch, sizeof( float ) * ( out - markScratch ) );
	memcpy( frameTable, tableScratch, sizeof( float ) * 4 * ( tableRows + ( elements + 3 ) / 4 ) );
	ps->table = table;
	ps->cell = cell;
	ps->numClusters = numClusters;
	return ps;
}

// The rectangle of the frame a box covers in this view: left, top, right, bottom in pixels from the upper left
// corner, inside the viewport. qfalse: none of it is on the screen.
static qboolean VK_PaintRect( const vec3_t lo, const vec3_t hi, int rect[4] ) {
	const int	*vp = vk.viewport;		// (x, y from the lower edge, width, height)
	float		low[2] = { 1.0f, 1.0f }, high[2] = { -1.0f, -1.0f };
	qboolean	behind = qfalse;

	for ( int i = 0; i < 8 && !behind; i++ ) {
		vec3_t corner;
		vec4_t eye, clip;

		VectorSet( corner, ( i & 1 ) ? hi[0] : lo[0], ( i & 2 ) ? hi[1] : lo[1], ( i & 4 ) ? hi[2] : lo[2] );
		R_TransformModelToClip( corner, backEnd.ori.modelMatrix, backEnd.viewParms.projectionMatrix, eye, clip );
		if ( clip[3] < 0.01f ) {
			// (it reaches behind the eye: all of the view)
			behind = qtrue;
			break;
		}
		for ( int k = 0; k < 2; k++ ) {
			const float at = Com_Clamp( -1.0f, 1.0f, clip[k] / clip[3] );

			low[k] = Q_min( low[k], at );
			high[k] = Q_max( high[k], at );
		}
	}
	if ( behind ) {
		low[0] = low[1] = -1.0f;
		high[0] = high[1] = 1.0f;
	}
	rect[0] = Q_max( vp[0] + (int)floorf( ( low[0] * 0.5f + 0.5f ) * vp[2] ) - 2, vp[0] );
	rect[2] = Q_min( vp[0] + (int)ceilf( ( high[0] * 0.5f + 0.5f ) * vp[2] ) + 2, vp[0] + vp[2] );
	rect[1] = Q_max( vk.height - ( vp[1] + (int)ceilf( ( high[1] * 0.5f + 0.5f ) * vp[3] ) ) - 2, vk.height - ( vp[1] + vp[3] ) );
	rect[3] = Q_min( vk.height - ( vp[1] + (int)floorf( ( low[1] * 0.5f + 0.5f ) * vp[3] ) ) + 2, vk.height - vp[1] );
	return (qboolean)( rect[2] > rect[0] && rect[3] > rect[1] && ( behind || ( high[0] > low[0] && high[1] > low[1] ) ) );
}

void VK_PaintMarks( uint32_t transform, int cullMode, vkProgram_t program, uint32_t bones, const vkDraw_t *tessFrom ) {
	const shader_t			*surface = tess.shader;
	// how far from the plane of a poly the bumps reach, so it does not paint another floor above or below it
	const float				reach = tessFrom->tess[0] + 4.0f;
	// how far the bumps move a place of the surface: up from where the polygon lies in the height, or down
	const float				shift = tessFrom->tess[0] * Q_max( Com_Clamp( 0.0f, 1.0f, tessFrom->tess[1] ), 1.0f - Com_Clamp( 0.0f, 1.0f, tessFrom->tess[1] ) ) + 0.5f;
	const vkPaintSurface_t	*ps = VK_PaintBuild( surface, reach, shift );
	int						rects[VK_PAINT_CLUSTERS + 1][4];
	qboolean				seen[VK_PAINT_CLUSTERS + 1];
	float					area = 0.0f;

	if ( !ps->numClusters ) {
		return;
	}
	// The clusters each in its rectangle, or all marks at once in the rectangle round all: what costs less, with a
	// pass over the whole view as 1 and what a pass costs whatever its rectangle as VK_PAINT_PASS (measured on the
	// sand of ffa3: 0.19 ms for the view, about 0.01 ms for a pass with a small rectangle).
#define VK_PAINT_PASS	0.06f
	const float view = (float)vk.viewport[2] * vk.viewport[3];
	float costEach = 0.0f, costAll = 0.0f;
	for ( int c = 0; c < ps->numClusters; c++ ) {
		seen[c] = VK_PaintRect( ps->clusters[c].lo, ps->clusters[c].hi, rects[c] );
		if ( seen[c] ) {
			costEach += ( ( ps->clusters[c].takes ? 1 : 0 ) + ( ps->clusters[c].adds ? 1 : 0 ) )
				* ( (float)( rects[c][2] - rects[c][0] ) * ( rects[c][3] - rects[c][1] ) / view + VK_PAINT_PASS );
		}
	}
	seen[VK_PAINT_CLUSTERS] = VK_PaintRect( ps->all.lo, ps->all.hi, rects[VK_PAINT_CLUSTERS] );
	if ( seen[VK_PAINT_CLUSTERS] ) {
		const int *r = rects[VK_PAINT_CLUSTERS];

		costAll = ( ( ps->all.takes ? 1 : 0 ) + ( ps->all.adds ? 1 : 0 ) ) * ( (float)( r[2] - r[0] ) * ( r[3] - r[1] ) / view + VK_PAINT_PASS );
	}
	const qboolean each = (qboolean)( ps->numClusters > 1 && costEach < costAll );
	for ( int c = each ? 0 : VK_PAINT_CLUSTERS; c < ( each ? ps->numClusters : VK_PAINT_CLUSTERS + 1 ); c++ ) {
		const vkPaintCluster_t *cluster = each ? &ps->clusters[c] : &ps->all;

		if ( !seen[c] ) {
			continue;
		}
		VK_Scissor( rects[c][0], rects[c][1], rects[c][2], rects[c][3] );
		// what the marks take from the surface and lay over it (the surface times a colour up to twice as bright,
		// generic.frag TimesSurface), then the light they add, which what lies over it has dimmed
		for ( int pass = 0; pass < 2; pass++ ) {
			vkDraw_t draw;

			if ( !( pass ? cluster->adds : cluster->takes ) ) {
				continue;
			}
			VK_InitDraw( &draw, transform, bones );
			const vkProgram_t passProgram = VK_TessPass( &draw, tessFrom, program );
			draw.flags |= VKD_DECALS;
			draw.tex0 = draw.tex1 = tr.whiteImage->texnum;
			draw.decals = ps->table;
			// which cluster + 1, 0: the marks of all
			draw.numDecals = each ? (uint32_t)c + 1 : 0;
			draw.parm[0] = (float)pass;
			draw.parm[1] = reach;
			draw.parm[2] = ps->cell;
			// the grooves are lit by the grid
			draw.parm[3] = ( ps->grooves && VK_GridDraw( &draw ) ) ? 1.0f : 0.0f;
			// the box of the pass: nothing of it outside (generic.tesc, Decals())
			VectorCopy( cluster->lo, draw.material );
			VectorCopy( cluster->hi, draw.material2 );
			VK_DrawTess( &draw, passProgram, ( pass ? ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
				: ( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) ) | GLS_DEPTHFUNC_EQUAL, cullMode,
				(qboolean)surface->polygonOffset );
		}
	}
	// the scissor back to the viewport
	VK_Scissor( vk.viewport[0], vk.height - ( vk.viewport[1] + vk.viewport[3] ), vk.viewport[0] + vk.viewport[2], vk.height - vk.viewport[1] );
}
