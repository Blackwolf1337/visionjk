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

// vk_bake.cpp -- light the level adds to its own lightmaps, traced on the graphics card while the level is played
// (v_bake). Vulkan device with ray queries only.
//
// The lightmaps of a level hold the light it was compiled with: its light entities (which the compiler took out of
// the level) and its lamps (q3map_surfacelight). That light stays as it is; this adds to it, per texel of the
// lightmaps, two things traced from the texel (shaders/bake.comp): one bounce of all that light off the surfaces
// around (a red wall tints the floor beside it, dark corners get some light), and the light of surfaces that glow but
// were no lamps when the level was lit (glow and additive stages, surfaces drawn at full brightness, the emission of
// materials). The two are kept apart, so v_bakeBounce and v_bakeEmit change them at once, and v_bake 0 gives back
// the lightmaps as they came.
//
// Order: when a level is seen, its texels (each lightmapped surface rasterised into its page) and the attributes
// of the triangles of its acceleration structure (texture, lightmap, glow) go to the graphics card; a slice of the
// texels is traced each frame, about v_bakeRays x 4700 rays a frame; when the frame of the last slice is done
// (VK_FRAMES later) the sums are read, blurred within each page where the surface is the same, kept in
// vulkan/bake/ of the profile, and added to the pages. A level seen before is added from that file at once.
//
// Not touched: the light styles 1 to 3 (switched and flickering lights), surfaces lit by their vertices, the light
// grid (models are lit as before). Doors and lifts are not in the acceleration structure: they neither block nor
// bounce. The sky's light is in the lightmaps already, a ray that leaves the level adds nothing.

#include "vk_local.h"
#include "vk_bake.h"

static const uint32_t vk_bakeComp[] =
#include "shaders/bake_comp.inc"
;

#define BAKE_PAGE			128
#define BAKE_ROWS			6			// vec4 a triangle, as bake.comp
#define BAKE_RAYS_A_FRAME	600000		// rays traced each frame while a level bakes, about 1 ms on an RTX 4070 SUPER
#define BAKE_VERSION		1
#define BAKE_MAGIC			0x314b4256	// "VBK1"

cvar_t *v_bake;
cvar_t *v_bakeBounce;
cvar_t *v_bakeEmit;
cvar_t *v_bakeRays;

enum { BAKE_NEW, BAKE_TRACING, BAKE_READING, BAKE_DONE, BAKE_NONE };

typedef struct {
	int		magic, version;
	int		numPages, numTexels;
	float	scale;						// a stored unit is this much light
} bakeFileHeader_t;

static struct {
	int						loads;			// r_worldLoads of the level all this is of, -1: none
	int						state;
	std::vector<float>		texels;			// 8 a texel: where, the normal
	std::vector<uint32_t>	where;			// page << 16 | y << 8 | x of each texel
	std::vector<float>		bounce, glow;	// 3 a texel of the pages (numPages x 128 x 128), after the blur and the spread
	std::vector<byte>		covered;		// a texel of the pages that has light added
	uint32_t				next;			// the first texel of the next slice
	int						readAt;			// frameNumber from which the sums can be read
	int						frameNumber;
	int						started;		// ms
	char					cachePath[MAX_QPATH];
	vkBuffer_t				texelBuffer, triangleBuffer, resultBuffer;
	VkPipelineLayout		layout;
	VkPipeline				pipeline;
	VkShaderModule			shader;
	// what the pages have now: 0 the originals, else the strengths that were added
	qboolean				applied;
	float					appliedBounce, appliedEmit;
} bake = { -1 };

void VK_Bake_Register( void ) {
	v_bake = ri.Cvar_Get( "v_bake", "1", CVAR_ARCHIVE_ND, "Light the level adds to its lightmaps, traced on the graphics card when a level is first seen (a few seconds, then kept in vulkan/bake/): one bounce of the baked light and the light of surfaces that glow. 0: the lightmaps as they came" );
	v_bakeBounce = ri.Cvar_Get( "v_bakeBounce", "0.6", CVAR_ARCHIVE_ND, "v_bake: how much of the bounced light is added (0 none, 1 all of it)" );
	v_bakeEmit = ri.Cvar_Get( "v_bakeEmit", "1", CVAR_ARCHIVE_ND, "v_bake: how much light surfaces that glow give (0 none)" );
	v_bakeRays = ri.Cvar_Get( "v_bakeRays", "128", CVAR_ARCHIVE_ND, "v_bake: rays from each texel of the lightmaps (more: less noise, longer to make); a level baked with another number is baked again" );
}

static void VK_Bake_FreeBuffers( void ) {
	VK_DestroyBuffer( &bake.texelBuffer );
	VK_DestroyBuffer( &bake.triangleBuffer );
	VK_DestroyBuffer( &bake.resultBuffer );
}

void VK_Bake_Shutdown( void ) {
	if ( vk.device ) {
		VK_Bake_FreeBuffers();
		if ( bake.pipeline ) {
			qvkDestroyPipeline( vk.device, bake.pipeline, NULL );
		}
		if ( bake.layout ) {
			qvkDestroyPipelineLayout( vk.device, bake.layout, NULL );
		}
		if ( bake.shader ) {
			qvkDestroyShaderModule( vk.device, bake.shader, NULL );
		}
	}
	bake.pipeline = VK_NULL_HANDLE;
	bake.layout = VK_NULL_HANDLE;
	bake.shader = VK_NULL_HANDLE;
	bake.texels.clear();
	bake.where.clear();
	bake.bounce.clear();
	bake.glow.clear();
	bake.covered.clear();
	bake.loads = -1;
	bake.state = BAKE_NEW;
	bake.applied = qfalse;
}

static qboolean VK_Bake_Pipeline( VkDescriptorSetLayout setLayout ) {
	VkPushConstantRange				push;
	VkPipelineLayoutCreateInfo		layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkComputePipelineCreateInfo		info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	const VkDescriptorSetLayout		sets[2] = { setLayout, vk.textureSetLayout };

	if ( bake.pipeline ) {
		return qtrue;
	}
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = sizeof( uint32_t ) * 24;
	layout.setLayoutCount = 2;
	layout.pSetLayouts = sets;
	layout.pushConstantRangeCount = 1;
	layout.pPushConstantRanges = &push;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &layout, NULL, &bake.layout ) );
	bake.shader = VK_CreateShader( vk_bakeComp, sizeof( vk_bakeComp ) );
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.stage.module = bake.shader;
	info.stage.pName = "main";
	info.layout = bake.layout;
	VK_CHECK( qvkCreateComputePipelines( vk.device, vk.pipelineCache, 1, &info, NULL, &bake.pipeline ) );
	return qtrue;
}

/*
===============================================================================

THE SURFACES

===============================================================================
*/

// a corner of a triangle: where, the normal, the coordinates of the texture and of the lightmap (style 0)
typedef struct {
	const float *xyz, *normal, *st, *lm;
} bakeCorner_t;

// each triangle of a surface, split as VK_RT_SurfaceTriangles splits it
template <typename F>
static void VK_Bake_ForTriangles( const msurface_t *surf, F &&each ) {
	if ( *surf->data == SF_FACE ) {
		const srfSurfaceFace_t	*face = (const srfSurfaceFace_t *)surf->data;
		const int				*indexes = (const int *)( (const byte *)face + face->ofsIndices );

		for ( int j = 0; j + 2 < face->numIndices; j += 3 ) {
			bakeCorner_t c[3];

			for ( int k = 0; k < 3; k++ ) {
				const float *p = face->points[indexes[j + k]];

				c[k].xyz = p;
				c[k].normal = face->plane.normal;
				c[k].st = p + 3;
				c[k].lm = p + VERTEX_LM;
			}
			each( c );
		}
	} else if ( *surf->data == SF_TRIANGLES ) {
		const srfTriangles_t *tris = (const srfTriangles_t *)surf->data;

		for ( int j = 0; j + 2 < tris->numIndexes; j += 3 ) {
			bakeCorner_t c[3];

			for ( int k = 0; k < 3; k++ ) {
				const drawVert_t *v = &tris->verts[tris->indexes[j + k]];

				c[k].xyz = v->xyz;
				c[k].normal = v->normal;
				c[k].st = v->st;
				c[k].lm = v->lightmap[0];
			}
			each( c );
		}
	} else if ( *surf->data == SF_GRID ) {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)surf->data;

		for ( int y = 0; y < grid->height - 1; y++ ) {
			for ( int x = 0; x < grid->width - 1; x++ ) {
				const int v = y * grid->width + x;
				const int quad[6] = { v, v + grid->width, v + 1, v + 1, v + grid->width, v + grid->width + 1 };

				for ( int t = 0; t < 2; t++ ) {
					bakeCorner_t c[3];

					for ( int k = 0; k < 3; k++ ) {
						const drawVert_t *dv = &grid->verts[quad[t * 3 + k]];

						c[k].xyz = dv->xyz;
						c[k].normal = dv->normal;
						c[k].st = dv->st;
						c[k].lm = dv->lightmap[0];
					}
					each( c );
				}
			}
		}
	}
}

// the page of the lightmap a surface has for its light style 0, -1: none
static int VK_Bake_Page( const shader_t *shader ) {
	const int page = shader->lightmapIndex[0];

	if ( page < 0 || page >= tr.numLightmaps || !tr.lightmaps[page] || !tr.lightmapPixels[page] ) {
		return -1;
	}
	return page;
}

// the texture a surface shows (the first stage that is not its lightmap), NULL: none
static const image_t *VK_Bake_Texture( const shader_t *shader ) {
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *st = &shader->stages[i];

		if ( !st->active ) {
			break;
		}
		if ( st->bundle[0].image && !st->bundle[0].isLightmap ) {
			return st->bundle[0].image;
		}
		if ( st->bundle[1].image && !st->bundle[1].isLightmap ) {
			return st->bundle[1].image;
		}
	}
	return NULL;
}

// a stage of the shader that glows on its own (dynamic glow, or added to what is under it), with texture coordinates
// of the surface; NULL: none
static const image_t *VK_Bake_Glow( const shader_t *shader ) {
	for ( int i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *st = &shader->stages[i];
		const uint32_t dst = st->stateBits & GLS_DSTBLEND_BITS;

		if ( !st->active ) {
			break;
		}
		if ( ( st->glow || dst == GLS_DSTBLEND_ONE || dst == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) && st->bundle[0].image
			&& !st->bundle[0].isLightmap && st->bundle[0].tcGen == TCGEN_TEXTURE ) {
			return st->bundle[0].image;
		}
	}
	return NULL;
}

// a surface drawn at its full brightness, no lightmap and no light of vertices (a screen, a light fitting)
static qboolean VK_Bake_Fullbright( const shader_t *shader ) {
	const shaderStage_t *st = &shader->stages[0];

	return (qboolean)( shader->lightmapIndex[0] < 0 && shader->numUnfoggedPasses >= 1 && st->active && shader->sort == SS_OPAQUE
		&& ( st->rgbGen == CGEN_IDENTITY || st->rgbGen == CGEN_IDENTITY_LIGHTING ) && !st->bundle[0].isLightmap );
}

// the six rows of each triangle of the acceleration structure of the level, in its order (bake.comp)
static void VK_Bake_Triangles( std::vector<float> &rows ) {
	std::vector<const msurface_t *> surfaces;

	VK_RT_LevelSurfaces( surfaces );
	for ( const msurface_t *surf : surfaces ) {
		const shader_t	*shader = surf->shader;
		const int		page = VK_Bake_Page( shader );
		const image_t	*texture = VK_Bake_Texture( shader );
		const image_t	*glow = VK_Bake_Glow( shader );
		// a lamp of the light the level was compiled with: its light is in the lightmaps
		const qboolean	lamp = (qboolean)( shader->surfaceLight > 0.0f || ( shader->emissiveSet && shader->emissive[3] > 0.0f ) );
		float			emission[4] = { 0.0f, 0.0f, 0.0f, 0.7f };
		float			glowScale = 1.0f;

		for ( int i = 0; i < shader->numUnfoggedPasses && shader->stages[i].active; i++ ) {
			const stageMaterial_t *mat = shader->stages[i].mat;

			if ( mat && mat->emissionStrength > 0.0f ) {
				VectorScale( mat->emission, mat->emissionStrength, emission );
				emission[3] = mat->emissionThreshold;
			}
		}
		if ( !glow && VK_Bake_Fullbright( shader ) && !lamp ) {
			// drawn at full brightness: it shows its own colour, it is lit by nothing
			glow = texture;
			texture = NULL;
		}
		VK_Bake_ForTriangles( surf, [&]( const bakeCorner_t *c ) {
			const float r[BAKE_ROWS * 4] = {
				c[0].lm[0], c[0].lm[1], c[1].lm[0], c[1].lm[1],
				c[2].lm[0], c[2].lm[1], (float)page, page >= 0 ? (float)tr.lightmaps[page]->texnum : -1.0f,
				c[0].st[0], c[0].st[1], c[1].st[0], c[1].st[1],
				c[2].st[0], c[2].st[1], texture ? (float)texture->texnum : -1.0f, lamp ? 2.0f : 0.0f,
				emission[0], emission[1], emission[2], emission[3],
				( glow && !lamp ) ? (float)glow->texnum : -1.0f, glowScale, 0.0f, 0.0f,
			};

			rows.insert( rows.end(), r, r + BAKE_ROWS * 4 );
		} );
	}
}

// each lightmapped surface of the world into its page: every texel whose middle lies in one of its triangles
static void VK_Bake_Texels( void ) {
	const bmodel_t	*model = &tr.world->bmodels[0];
	std::vector<int> taken( (size_t)tr.numLightmaps * BAKE_PAGE * BAKE_PAGE, 0 );

	bake.texels.clear();
	bake.where.clear();
	for ( int s = 0; s < model->numSurfaces; s++ ) {
		const msurface_t	*surf = &model->firstSurface[s];
		const shader_t		*shader = surf->shader;
		const int			page = VK_Bake_Page( shader );

		if ( page < 0 || shader->sky || ( shader->surfaceFlags & ( SURF_SKY | SURF_NODRAW ) ) ) {
			continue;
		}
		VK_Bake_ForTriangles( surf, [&]( const bakeCorner_t *c ) {
			float px[3], py[3];

			for ( int k = 0; k < 3; k++ ) {
				px[k] = c[k].lm[0] * BAKE_PAGE;
				py[k] = c[k].lm[1] * BAKE_PAGE;
			}
			const float area = ( px[1] - px[0] ) * ( py[2] - py[0] ) - ( px[2] - px[0] ) * ( py[1] - py[0] );

			if ( fabsf( area ) < 1e-6f ) {
				return;
			}
			const int x0 = Com_Clampi( 0, BAKE_PAGE - 1, (int)floorf( Q_min( px[0], Q_min( px[1], px[2] ) ) ) );
			const int x1 = Com_Clampi( 0, BAKE_PAGE - 1, (int)ceilf( Q_max( px[0], Q_max( px[1], px[2] ) ) ) );
			const int y0 = Com_Clampi( 0, BAKE_PAGE - 1, (int)floorf( Q_min( py[0], Q_min( py[1], py[2] ) ) ) );
			const int y1 = Com_Clampi( 0, BAKE_PAGE - 1, (int)ceilf( Q_max( py[0], Q_max( py[1], py[2] ) ) ) );

			for ( int y = y0; y <= y1; y++ ) {
				for ( int x = x0; x <= x1; x++ ) {
					const float cx = x + 0.5f, cy = y + 0.5f;
					// barycentric weights of the middle of the texel
					float w1 = ( ( cx - px[0] ) * ( py[2] - py[0] ) - ( px[2] - px[0] ) * ( cy - py[0] ) ) / area;
					float w2 = ( ( px[1] - px[0] ) * ( cy - py[0] ) - ( cx - px[0] ) * ( py[1] - py[0] ) ) / area;
					float w0 = 1.0f - w1 - w2;
					int *slot = &taken[( (size_t)page * BAKE_PAGE + y ) * BAKE_PAGE + x];

					if ( w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f || *slot ) {
						continue;
					}
					vec3_t pos, normal;

					for ( int a = 0; a < 3; a++ ) {
						pos[a] = c[0].xyz[a] * w0 + c[1].xyz[a] * w1 + c[2].xyz[a] * w2;
						normal[a] = c[0].normal[a] * w0 + c[1].normal[a] * w1 + c[2].normal[a] * w2;
					}
					if ( VectorNormalize( normal ) < 0.01f ) {
						continue;
					}
					*slot = 1;
					const float t[8] = { pos[0], pos[1], pos[2], 0.0f, normal[0], normal[1], normal[2], 0.0f };

					bake.texels.insert( bake.texels.end(), t, t + 8 );
					bake.where.push_back( (uint32_t)( page << 16 | y << 8 | x ) );
				}
			}
		} );
	}
}

/*
===============================================================================

THE PAGES

===============================================================================
*/

// The pages with what was traced added, as strong as v_bakeBounce and v_bakeEmit say (or the originals, on: qfalse).
// The pages hold their light shifted by the overbright bits already; a texel that goes past 255 is scaled back as
// R_ColorShiftLightingBytes does, keeping its colour.
static void VK_Bake_Apply( qboolean on ) {
	const float sb = on ? Com_Clamp( 0.0f, 4.0f, v_bakeBounce->value ) : 0.0f;
	const float se = on ? Com_Clamp( 0.0f, 16.0f, v_bakeEmit->value ) : 0.0f;
	byte page[BAKE_PAGE * BAKE_PAGE * 4];

	if ( on == bake.applied && ( !on || ( sb == bake.appliedBounce && se == bake.appliedEmit ) ) ) {
		return;
	}
	for ( int p = 0; p < tr.numLightmaps; p++ ) {
		if ( !tr.lightmaps[p] || !tr.lightmapPixels[p] ) {
			continue;
		}
		memcpy( page, tr.lightmapPixels[p], sizeof( page ) );
		if ( on && !bake.covered.empty() ) {
			for ( int t = 0; t < BAKE_PAGE * BAKE_PAGE; t++ ) {
				const size_t i = (size_t)p * BAKE_PAGE * BAKE_PAGE + t;

				if ( !bake.covered[i] ) {
					continue;
				}
				float c[3], most = 0.0f;

				for ( int k = 0; k < 3; k++ ) {
					c[k] = page[t * 4 + k] + ( bake.bounce[i * 3 + k] * sb + bake.glow[i * 3 + k] * se ) * 255.0f;
					most = Q_max( most, c[k] );
				}
				const float scale = most > 255.0f ? 255.0f / most : 1.0f;

				for ( int k = 0; k < 3; k++ ) {
					page[t * 4 + k] = (byte)Com_Clampi( 0, 255, (int)( c[k] * scale + 0.5f ) );
				}
			}
		}
		R_ReplaceImagePixels( tr.lightmaps[p], page, BAKE_PAGE, BAKE_PAGE );
	}
	bake.applied = on;
	bake.appliedBounce = sb;
	bake.appliedEmit = se;
}

// The sums read back into the pages: blurred within each page between texels of the same surface (near, facing
// the same way), then spread one texel past the edges of what is covered, for the filtering of the lightmaps
static void VK_Bake_Take( const float *results ) {
	const size_t	pageTexels = (size_t)BAKE_PAGE * BAKE_PAGE;
	const size_t	total = (size_t)tr.numLightmaps * pageTexels;
	std::vector<int> index( total, -1 );

	for ( size_t i = 0; i < bake.where.size(); i++ ) {
		const uint32_t w = bake.where[i];

		index[( w >> 16 ) * pageTexels + ( ( w >> 8 ) & 255 ) * BAKE_PAGE + ( w & 255 )] = (int)i;
	}
	bake.bounce.assign( total * 3, 0.0f );
	bake.glow.assign( total * 3, 0.0f );
	bake.covered.assign( total, 0 );
	for ( size_t i = 0; i < bake.where.size(); i++ ) {
		const uint32_t	w = bake.where[i];
		const int		page = (int)( w >> 16 ), y = (int)( ( w >> 8 ) & 255 ), x = (int)( w & 255 );
		const float		*pi = &bake.texels[i * 8], *ni = pi + 4;
		float			sum[6] = { 0 }, weights = 0.0f;

		for ( int dy = -2; dy <= 2; dy++ ) {
			for ( int dx = -2; dx <= 2; dx++ ) {
				if ( x + dx < 0 || x + dx >= BAKE_PAGE || y + dy < 0 || y + dy >= BAKE_PAGE ) {
					continue;
				}
				const int j = index[page * pageTexels + ( y + dy ) * BAKE_PAGE + x + dx];

				if ( j < 0 ) {
					continue;
				}
				const float *pj = &bake.texels[(size_t)j * 8], *nj = pj + 4;
				const float facing = DotProduct( ni, nj );
				vec3_t d;

				VectorSubtract( pi, pj, d );
				if ( facing < 0.7f || DotProduct( d, d ) > 32.0f * 32.0f ) {
					continue;
				}
				const float weight = expf( -( dx * dx + dy * dy ) / 4.5f ) * facing * facing;

				for ( int k = 0; k < 3; k++ ) {
					sum[k] += results[(size_t)j * 8 + k] * weight;
					sum[3 + k] += results[(size_t)j * 8 + 4 + k] * weight;
				}
				weights += weight;
			}
		}
		const size_t t = (size_t)page * pageTexels + y * BAKE_PAGE + x;

		for ( int k = 0; k < 3; k++ ) {
			bake.bounce[t * 3 + k] = sum[k] / weights;
			bake.glow[t * 3 + k] = sum[3 + k] / weights;
		}
		bake.covered[t] = 1;
	}
	// one texel past the covered ones, the mean of those around
	std::vector<byte> was = bake.covered;

	for ( int p = 0; p < tr.numLightmaps; p++ ) {
		for ( int y = 0; y < BAKE_PAGE; y++ ) {
			for ( int x = 0; x < BAKE_PAGE; x++ ) {
				const size_t t = (size_t)p * pageTexels + y * BAKE_PAGE + x;
				float sum[6] = { 0 };
				int n = 0;

				if ( was[t] ) {
					continue;
				}
				for ( int dy = -1; dy <= 1; dy++ ) {
					for ( int dx = -1; dx <= 1; dx++ ) {
						const int xx = x + dx, yy = y + dy;

						if ( xx < 0 || xx >= BAKE_PAGE || yy < 0 || yy >= BAKE_PAGE || !was[(size_t)p * pageTexels + yy * BAKE_PAGE + xx] ) {
							continue;
						}
						const size_t u = (size_t)p * pageTexels + yy * BAKE_PAGE + xx;

						for ( int k = 0; k < 3; k++ ) {
							sum[k] += bake.bounce[u * 3 + k];
							sum[3 + k] += bake.glow[u * 3 + k];
						}
						n++;
					}
				}
				if ( n ) {
					for ( int k = 0; k < 3; k++ ) {
						bake.bounce[t * 3 + k] = sum[k] / n;
						bake.glow[t * 3 + k] = sum[3 + k] / n;
					}
					bake.covered[t] = 1;
				}
			}
		}
	}
}

/*
===============================================================================

THE CACHE

===============================================================================
*/

static unsigned long long VK_Bake_Hash( unsigned long long hash, const void *data, size_t size ) {
	const byte *b = (const byte *)data;

	for ( size_t i = 0; i < size; i++ ) {
		hash = ( hash ^ b[i] ) * 1099511628211ULL;
	}
	return hash;
}

// the file of this level: by its name, its lightmaps, the number of its surfaces, the rays and the version
static void VK_Bake_CachePath( void ) {
	unsigned long long hash = 14695981039346656037ULL;
	const int numbers[4] = { BAKE_VERSION, tr.numLightmaps, tr.world->numsurfaces, Com_Clampi( 8, 1024, v_bakeRays->integer ) };

	hash = VK_Bake_Hash( hash, numbers, sizeof( numbers ) );
	for ( int p = 0; p < tr.numLightmaps; p++ ) {
		if ( tr.lightmapPixels[p] ) {
			hash = VK_Bake_Hash( hash, tr.lightmapPixels[p], BAKE_PAGE * BAKE_PAGE * 4 );
		}
	}
	Com_sprintf( bake.cachePath, sizeof( bake.cachePath ), "vulkan/bake/%s_%016llx.dat", tr.world->baseName, hash );
}

// what is kept: the covered texels of all pages, each its place and the six sums in 16 bits of 1/4096
static void VK_Bake_Write( void ) {
	std::vector<byte> out( sizeof( bakeFileHeader_t ) );
	bakeFileHeader_t *header = (bakeFileHeader_t *)out.data();
	int count = 0;

	for ( size_t t = 0; t < bake.covered.size(); t++ ) {
		if ( !bake.covered[t] ) {
			continue;
		}
		uint16_t v[8];
		const uint32_t place = (uint32_t)t;

		memcpy( v, &place, sizeof( place ) );
		for ( int k = 0; k < 3; k++ ) {
			v[2 + k] = (uint16_t)Com_Clampi( 0, 65535, (int)( bake.bounce[t * 3 + k] * 4096.0f + 0.5f ) );
			v[5 + k] = (uint16_t)Com_Clampi( 0, 65535, (int)( bake.glow[t * 3 + k] * 4096.0f + 0.5f ) );
		}
		out.insert( out.end(), (const byte *)v, (const byte *)v + sizeof( v ) );
		count++;
	}
	header = (bakeFileHeader_t *)out.data();
	header->magic = BAKE_MAGIC;
	header->version = BAKE_VERSION;
	header->numPages = tr.numLightmaps;
	header->numTexels = count;
	header->scale = 1.0f / 4096.0f;
	ri.FS_WriteFile( bake.cachePath, out.data(), (int)out.size() );
}

static qboolean VK_Bake_Read( void ) {
	void *buffer = NULL;
	const long length = ri.FS_ReadFile( bake.cachePath, &buffer );
	qboolean ok = qfalse;

	if ( buffer && length >= (long)sizeof( bakeFileHeader_t ) ) {
		const bakeFileHeader_t *header = (const bakeFileHeader_t *)buffer;
		const size_t total = (size_t)tr.numLightmaps * BAKE_PAGE * BAKE_PAGE;

		if ( header->magic == BAKE_MAGIC && header->version == BAKE_VERSION && header->numPages == tr.numLightmaps
			&& length == (long)( sizeof( *header ) + (size_t)header->numTexels * 16 ) ) {
			const uint16_t *v = (const uint16_t *)( header + 1 );

			bake.bounce.assign( total * 3, 0.0f );
			bake.glow.assign( total * 3, 0.0f );
			bake.covered.assign( total, 0 );
			ok = qtrue;
			for ( int i = 0; i < header->numTexels; i++, v += 8 ) {
				uint32_t place;

				memcpy( &place, v, sizeof( place ) );
				if ( place >= total ) {
					ok = qfalse;
					break;
				}
				for ( int k = 0; k < 3; k++ ) {
					bake.bounce[place * 3 + k] = v[2 + k] * header->scale;
					bake.glow[place * 3 + k] = v[5 + k] * header->scale;
				}
				bake.covered[place] = 1;
			}
		}
	}
	if ( buffer ) {
		ri.FS_FreeFile( buffer );
	}
	return ok;
}

/*
===============================================================================

THE PASS

===============================================================================
*/

// the level is seen: from the cache, else its texels and triangles to the graphics card
static void VK_Bake_Begin( void ) {
	std::vector<float> rows;

	bake.loads = r_worldLoads;
	bake.applied = qfalse;
	bake.bounce.clear();
	bake.glow.clear();
	bake.covered.clear();
	VK_Bake_FreeBuffers();
	VK_Bake_CachePath();
	if ( VK_Bake_Read() ) {
		ri.Printf( PRINT_DEVELOPER, "v_bake: %s from the cache\n", tr.world->baseName );
		bake.state = BAKE_DONE;
		return;
	}
	bake.started = ri.Milliseconds();
	VK_Bake_Texels();
	VK_Bake_Triangles( rows );
	if ( bake.where.empty() || rows.empty() ) {
		bake.state = BAKE_NONE;
		return;
	}
	VK_CreateBuffer( &bake.texelBuffer, bake.texels.size() * sizeof( float ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
	VK_CreateBuffer( &bake.triangleBuffer, rows.size() * sizeof( float ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
	VK_CreateBuffer( &bake.resultBuffer, bake.where.size() * sizeof( float ) * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, qtrue );
	memcpy( bake.texelBuffer.mapped, bake.texels.data(), bake.texels.size() * sizeof( float ) );
	memcpy( bake.triangleBuffer.mapped, rows.data(), rows.size() * sizeof( float ) );
	bake.next = 0;
	bake.state = BAKE_TRACING;
	ri.Printf( PRINT_DEVELOPER, "v_bake: %s, %i texels, %i triangles, prepared in %i ms\n", tr.world->baseName, (int)bake.where.size(),
		(int)( rows.size() / ( BAKE_ROWS * 4 ) ), ri.Milliseconds() - bake.started );
}

// a slice of the texels through bake.comp
static void VK_Bake_Slice( VkCommandBuffer cmd, VkDescriptorSet set ) {
	const uint32_t	rays = (uint32_t)Com_Clampi( 8, 1024, v_bakeRays->integer );
	const uint32_t	total = (uint32_t)bake.where.size();
	const uint32_t	count = Q_min( total - bake.next, Q_max( 256u, (uint32_t)BAKE_RAYS_A_FRAME / rays ) );
	const VkDeviceAddress texels = VK_RT_BufferAddress( &bake.texelBuffer ), triangles = VK_RT_BufferAddress( &bake.triangleBuffer );
	const VkDeviceAddress results = VK_RT_BufferAddress( &bake.resultBuffer );
	const VkDescriptorSet sets[2] = { set, vk.textureSet };
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	struct {
		uint32_t	addresses[6];
		uint32_t	first, count, rays, seed, sphere;
		float		reach;
		float		gridOrigin[4], gridSize[4];
		uint32_t	gridBounds[4];
	} push;

	memset( &push, 0, sizeof( push ) );
	push.addresses[0] = (uint32_t)texels;
	push.addresses[1] = (uint32_t)( texels >> 32 );
	push.addresses[2] = (uint32_t)triangles;
	push.addresses[3] = (uint32_t)( triangles >> 32 );
	push.addresses[4] = (uint32_t)results;
	push.addresses[5] = (uint32_t)( results >> 32 );
	push.first = bake.next;
	push.count = count;
	push.rays = rays;
	push.seed = 0x9e3779b9u;
	push.reach = 4096.0f;

	qvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bake.pipeline );
	qvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bake.layout, 0, 2, sets, 0, NULL );
	qvkCmdPushConstants( cmd, bake.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
	qvkCmdDispatch( cmd, ( count + 63 ) / 64, 1, 1 );
	bake.next += count;
	if ( bake.next >= total ) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
		// (the fence of this frame has to come round before the CPU reads)
		bake.readAt = bake.frameNumber + VK_FRAMES + 1;
		bake.state = BAKE_READING;
	}
}

/*
===============
VK_Bake_Record

In VK_RT_Pass, after the top level of the frame is built and outside a render pass: begins a level, traces a slice,
takes what was traced, follows the cvars
===============
*/
void VK_Bake_Record( VkCommandBuffer cmd ) {
	VkDescriptorSet			set;
	VkDescriptorSetLayout	setLayout;

	bake.frameNumber++;
	if ( !tr.world || !v_bake ) {
		return;
	}
	if ( bake.loads != r_worldLoads ) {
		if ( !v_bake->integer ) {
			return;
		}
		if ( !VK_RT_BakeSet( &set, &setLayout ) ) {
			return;		// (the level is not built yet)
		}
		VK_Bake_Begin();
	}
	switch ( bake.state ) {
	case BAKE_TRACING:
		if ( !v_bake->integer ) {
			return;		// (it goes on when it is switched on again)
		}
		if ( !VK_RT_BakeSet( &set, &setLayout ) || !VK_Bake_Pipeline( setLayout ) ) {
			return;
		}
		VK_Bake_Slice( cmd, set );
		break;
	case BAKE_READING:
		if ( bake.frameNumber < bake.readAt ) {
			return;
		}
		VK_Bake_Take( (const float *)bake.resultBuffer.mapped );
		VK_Bake_FreeBuffers();
		bake.texels.clear();
		bake.where.clear();
		VK_Bake_Write();
		ri.Printf( PRINT_DEVELOPER, "v_bake: %s baked in %i ms\n", tr.world->baseName, ri.Milliseconds() - bake.started );
		bake.state = BAKE_DONE;
		// fall through
	case BAKE_DONE:
		VK_Bake_Apply( (qboolean)( v_bake->integer != 0 ) );
		break;
	default:
		break;
	}
}
