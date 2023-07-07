// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2023 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_md2.c
/// \brief 3D Model Handling
///	Inspired from md2.c by Mete Ciragan (mete@swissquake.ch)

#ifdef __GNUC__
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../d_main.h"
#include "../doomdef.h"
#include "../doomstat.h"
#include "../fastcmp.h"

#ifdef HWRENDER
#include "hw_drv.h"
#include "hw_light.h"
#include "hw_md2.h"
#include "../d_main.h"
#include "../r_bsp.h"
#include "../r_fps.h"
#include "../r_main.h"
#include "../m_misc.h"
#include "../m_textreader.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../r_things.h"
#include "../r_draw.h"
#include "../p_tick.h"
#include "../w_wad.h"
#include "../w_handle.h"
#include "hw_model.h"

#include "hw_main.h"
#include "../v_video.h"
#ifdef HAVE_PNG

#ifndef _MSC_VER
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#endif

#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 0
#endif

 #include "png.h"
 #ifndef PNG_READ_SUPPORTED
 #undef HAVE_PNG
 #endif
 #if PNG_LIBPNG_VER < 100207
 //#undef HAVE_PNG
 #endif
#endif

#ifndef errno
#include "errno.h"
#endif

md2_t md2_models[NUMSPRITES];
md2_t md2_playermodels[MAXSKINS];

static wadfile_t *modelpack = NULL;

static model_t *md2_readModel(const char *filename)
{
	if (modelpack)
		return LoadModel(filename, PU_STATIC, modelpack);

	char *fn = M_FindFile(filename);
	if (!fn)
		return NULL;

	return LoadModel(fn, PU_STATIC, NULL);
}

#ifdef HAVE_PNG
static void PNG_error(png_structp PNG, png_const_charp pngtext)
{
	CONS_Debug(DBG_RENDER, "libpng error at %p: %s", PNG, pngtext);
	//I_Error("libpng error at %p: %s", PNG, pngtext);
}

static void PNG_warn(png_structp PNG, png_const_charp pngtext)
{
	CONS_Debug(DBG_RENDER, "libpng warning at %p: %s", PNG, pngtext);
}

static void PNG_IOReader(png_structp png_ptr, png_bytep data, png_size_t length)
{
	png_io_t *f = png_get_io_ptr(png_ptr);
	if (length > (f->size - f->position))
		png_error(png_ptr, "PNG_IOReader: buffer overrun");
	memcpy(data, f->buffer + f->position, length);
	f->position += length;
}

static GLTextureFormat_t PNG_Load(const UINT8 *source, size_t source_size, int *w, int *h, GLPatch_t *grpatch)
{
	png_structp png_ptr;
	png_infop png_info_ptr;
	png_uint_32 width, height;
	int bit_depth, color_type;
#ifdef PNG_SETJMP_SUPPORTED
#ifdef USE_FAR_KEYWORD
	jmp_buf jmpbuf;
#endif
#endif
	png_io_t png_io;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
		PNG_error, PNG_warn);
	if (!png_ptr)
	{
		CONS_Debug(DBG_RENDER, "PNG_Load: Error on initialize libpng\n");
		return 0;
	}

	png_info_ptr = png_create_info_struct(png_ptr);
	if (!png_info_ptr)
	{
		CONS_Debug(DBG_RENDER, "PNG_Load: Error on allocate for libpng\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return 0;
	}

#ifdef USE_FAR_KEYWORD
	if (setjmp(jmpbuf))
#else
	if (setjmp(png_jmpbuf(png_ptr)))
#endif
	{
		png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);
		Z_Free(grpatch->mipmap->data);
		return 0;
	}
#ifdef USE_FAR_KEYWORD
	png_memcpy(png_jmpbuf(png_ptr), jmpbuf, sizeof jmp_buf);
#endif

	png_io.buffer = source;
	png_io.size = source_size;
	png_io.position = 0;
	png_set_read_fn(png_ptr, &png_io, PNG_IOReader);

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
	png_set_user_limits(png_ptr, 2048, 2048);
#endif

	png_read_info(png_ptr, png_info_ptr);

	png_get_IHDR(png_ptr, png_info_ptr, &width, &height, &bit_depth, &color_type,
	 NULL, NULL, NULL);

	if (bit_depth == 16)
		png_set_strip_16(png_ptr);

	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);
	else if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);

	if (png_get_valid(png_ptr, png_info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);
	else if (color_type != PNG_COLOR_TYPE_RGB_ALPHA && color_type != PNG_COLOR_TYPE_GRAY_ALPHA)
	{
#if PNG_LIBPNG_VER < 10207
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
#else
		png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
#endif
	}

	png_read_update_info(png_ptr, png_info_ptr);

	{
		png_uint_32 i, pitch = png_get_rowbytes(png_ptr, png_info_ptr);
		png_bytep PNG_image = Z_Malloc(pitch*height, PU_HWRMODELTEXTURE, &grpatch->mipmap->data);
		png_bytepp row_pointers = png_malloc(png_ptr, height * sizeof (png_bytep));
		for (i = 0; i < height; i++)
			row_pointers[i] = PNG_image + i*pitch;
		png_read_image(png_ptr, row_pointers);
		png_free(png_ptr, (png_voidp)row_pointers);
	}

	png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);

	*w = (int)width;
	*h = (int)height;
	return GL_TEXFMT_RGBA;
}
#endif

static GLTextureFormat_t LoadTexture(const char *filename, int *w, int *h, GLPatch_t *grpatch)
{
	GLTextureFormat_t fmt = 0;
	char pngfilename[4096];
	UINT8 *buffer = NULL;
	size_t fileLen = 0;

	snprintf(pngfilename, sizeof pngfilename, "models/%s", filename);

	FIL_ForceExtension(pngfilename, ".png");

	if (modelpack)
	{
		UINT16 lump = Resource_CheckNumForName(modelpack, pngfilename);
		if (lump == INT16_MAX)
			return 0;

		buffer = Resource_CacheLumpNum(modelpack, lump, PU_STATIC);
		fileLen = Resource_LumpLength(modelpack, lump);
	}
	else
	{
		char *fn = M_FindFile(pngfilename);
		if (!fn)
			return 0;

		filehandle_t *f = File_Open(fn, "rb", FILEHANDLE_SDL);
		if (!f) // still couldn't open it somehow
			return 0;

		fileLen = File_Size(f);
		buffer = ZZ_Alloc(fileLen);

		File_Read(buffer, fileLen, 1, f);
		File_Close(f);
	}

#ifdef HAVE_PNG
	fmt = PNG_Load(buffer, fileLen, w, h, grpatch);
#endif

	Z_Free(buffer);

	return fmt;
}

// -----------------+
// md2_loadTexture  : Download a png texture for models
// -----------------+
static void md2_loadTexture(md2_t *model)
{
	patch_t *patch;
	GLPatch_t *grPatch = NULL;
	const char *filename = model->filename;

	if (model->grpatch)
	{
		patch = model->grpatch;
		grPatch = (GLPatch_t *)(patch->hardware);
		if (grPatch)
			Z_Free(grPatch->mipmap->data);
	}
	else
		model->grpatch = patch = Patch_Create(NULL, 0, NULL);

	if (!patch->hardware)
		Patch_AllocateHardwarePatch(patch);

	if (grPatch == NULL)
		grPatch = (GLPatch_t *)(patch->hardware);

	if (!grPatch->mipmap->downloaded && !grPatch->mipmap->data)
	{
		int w = 0, h = 0;
		UINT32 size;
		RGBA_t *image;

		grPatch->mipmap->format = LoadTexture(filename, &w, &h, grPatch);
		if (grPatch->mipmap->format == 0)
		{
			model->notexturefile = true; // mark it so its not searched for again repeatedly
			return;
		}

		grPatch->mipmap->downloaded = 0;
		grPatch->mipmap->flags = 0;

		patch->width = (INT16)w;
		patch->height = (INT16)h;
		grPatch->mipmap->width = (UINT16)w;
		grPatch->mipmap->height = (UINT16)h;

		// Lactozilla: Apply colour cube
		image = grPatch->mipmap->data;
		size = w*h;
		while (size--)
		{
			V_CubeApply(&image->s.red, &image->s.green, &image->s.blue);
			image++;
		}
	}
	GPU->SetTexture(grPatch->mipmap);
}

// -----------------+
// md2_loadBlendTexture  : Download a png texture for blending MD2 models
// -----------------+
static void md2_loadBlendTexture(md2_t *model)
{
	patch_t *patch;
	GLPatch_t *grPatch = NULL;
	char *filename = Z_Malloc(strlen(model->filename)+7, PU_STATIC, NULL);

	strcpy(filename, model->filename);
	FIL_ForceExtension(filename, "_blend.png");

	if (model->blendgrpatch)
	{
		patch = model->blendgrpatch;
		grPatch = (GLPatch_t *)(patch->hardware);
		if (grPatch)
			Z_Free(grPatch->mipmap->data);
	}
	else
		model->blendgrpatch = patch = Patch_Create(NULL, 0, NULL);

	if (!patch->hardware)
		Patch_AllocateHardwarePatch(patch);

	if (grPatch == NULL)
		grPatch = (GLPatch_t *)(patch->hardware);

	if (!grPatch->mipmap->downloaded && !grPatch->mipmap->data)
	{
		int w = 0, h = 0;
		grPatch->mipmap->format = LoadTexture(filename, &w, &h, grPatch);
		if (grPatch->mipmap->format == 0)
		{
			model->noblendfile = true; // mark it so its not searched for again repeatedly
			Z_Free(filename);
			return;
		}

		grPatch->mipmap->downloaded = 0;
		grPatch->mipmap->flags = 0;

		patch->width = (INT16)w;
		patch->height = (INT16)h;
		grPatch->mipmap->width = (UINT16)w;
		grPatch->mipmap->height = (UINT16)h;
	}
	GPU->SetTexture(grPatch->mipmap); // We do need to do this so that it can be cleared and knows to recreate it when necessary

	Z_Free(filename);
}

// Don't spam the console, or the OS with fopen requests!
static boolean nomd2s = false;

static char *GetModelDefFile(const char *filename, size_t *size)
{
	if (nomd2s)
		return NULL;

	char *fn = M_FindFile(filename);
	if (!fn)
		return NULL;

	// read the models.dat file
	filehandle_t *f = File_Open(fn, "rt", FILEHANDLE_SDL);

	if (!f)
	{
		CONS_Alert(CONS_ERROR, "Error while loading model definition file: Could not open file %s\n", fn);
		nomd2s = true;
		return NULL;
	}

	size_t sz = File_Size(f);
	char *text = ZZ_Alloc(sz);

	if (File_Read(text, 1, sz, f) < sz)
	{
		CONS_Alert(CONS_ERROR, "Error while loading model definition file: Could not read file %s\n", fn);
		Z_Free(text);
		return NULL;
	}

	*size = sz;

	File_Close(f);

	return text;
}

void HWR_InitModels(void)
{
	size_t i;
	INT32 s;

	for (s = 0; s < MAXSKINS; s++)
	{
		md2_playermodels[s].scale = -1.0f;
		md2_playermodels[s].offset = 0.0f;
		md2_playermodels[s].model = NULL;
		md2_playermodels[s].grpatch = NULL;
		md2_playermodels[s].blendgrpatch = NULL;
		md2_playermodels[s].notexturefile = false;
		md2_playermodels[s].noblendfile = false;
		md2_playermodels[s].skin = -1;
		md2_playermodels[s].notfound = true;
		md2_playermodels[s].error = false;
	}
	for (i = 0; i < NUMSPRITES; i++)
	{
		md2_models[i].scale = -1.0f;
		md2_models[i].offset = 0.0f;
		md2_models[i].model = NULL;
		md2_models[i].grpatch = NULL;
		md2_models[i].blendgrpatch = NULL;
		md2_models[i].notexturefile = false;
		md2_models[i].noblendfile = false;
		md2_models[i].skin = -1;
		md2_models[i].notfound = true;
		md2_models[i].error = false;
	}

	nomd2s = false;
}

static void ReadModelDefs(char *data, size_t size)
{
	size_t i;
	INT32 s;
	char name[24], filename[32];
	float scale, offset;

	textreader_t *r = TextReader_New(data, size);
	char *line = NULL;

	size_t l = TextReader_GetLineLength(r);
	while (l)
	{
		line = Z_Realloc(line, l, PU_STATIC, NULL);
		TextReader_GetLine(r, line, l);

		if (sscanf(line, "%25s %31s %f %f", name, filename, &scale, &offset) != 4)
			break;

		char *skinname = name;
		size_t len = strlen(name);

		// Check for the player model prefix
		if (len > sizeof(PLAYERMODELPREFIX) && !strnicmp(name, PLAYERMODELPREFIX, sizeof(PLAYERMODELPREFIX)))
		{
			skinname += sizeof(PLAYERMODELPREFIX);
			goto addskinmodel;
		}

		// Add sprite model
		// Must be 4 characters long exactly, otherwise it's not a sprite name
		if (len == 4)
		{
			for (i = 0; i < NUMSPRITES; i++)
			{
				if (stricmp(name, sprnames[i]) == 0)
				{
					md2_models[i].scale = scale;
					md2_models[i].offset = offset;
					md2_models[i].notfound = false;
					strcpy(md2_models[i].filename, filename);
					goto modelfound;
				}
			}
		}

addskinmodel:
		// Add player model
		for (s = 0; s < MAXSKINS; s++)
		{
			if (stricmp(skinname, skins[s].name) == 0)
			{
				md2_playermodels[s].skin = s;
				md2_playermodels[s].scale = scale;
				md2_playermodels[s].offset = offset;
				md2_playermodels[s].notfound = false;
				strcpy(md2_playermodels[s].filename, filename);
				goto modelfound;
			}
		}

modelfound:
		// Move on to the next line...
		l = TextReader_GetLineLength(r);
	}

	Z_Free(line);

	TextReader_Delete(r);
}

boolean HWR_ModelPackExists(const char *filename)
{
	char buf[4096];

	strlcpy(buf, filename, sizeof buf);

	return M_FindFile(buf) != NULL;
}

static boolean LoadModelPack(char *filename)
{
	char *fn = M_FindFile(filename);
	if (!fn)
		return false;

	modelpack = W_LoadResourceFile(fn, FILEHANDLE_SDL);

	return modelpack != NULL;
}

// Looks for a model pack or a model.dat file to read model definitions from.
// If modelpack is set to its default value, then models.dat is loaded first.
// Otherwise, we look for a model pack on the filesystem.
// If that fails, then we just look for a models.dat again.
void HWR_ReadModels(void)
{
	char *text = NULL;
	size_t size = 0;

	if (!cv_usemodelpack.value)
	{
		text = GetModelDefFile("models.dat", &size);
		if (text)
		{
			ReadModelDefs(text, size);
			Z_Free(text);
		}
		return;
	}

	if (strcmp(cv_modelpack.string, cv_modelpack.defaultvalue) == 0)
		text = GetModelDefFile("models.dat", &size);

	if (!text)
	{
		char buf[4096];

		strlcpy(buf, cv_modelpack.string, sizeof buf);

		if (LoadModelPack(buf))
		{
			UINT16 lump = Resource_CheckNumForName(modelpack, "models.dat");
			if (lump != INT16_MAX)
			{
				text = Resource_CacheLumpNum(modelpack, lump, PU_STATIC);
				size = Resource_LumpLength(modelpack, lump);
			}
		}
		else
			text = GetModelDefFile("models.dat", &size);
	}

	if (text)
	{
		ReadModelDefs(text, size);
		Z_Free(text);
	}
}

static void FreeModelData(md2_t *md2)
{
	if (md2->model)
	{
		UnloadModel(md2->model);
		md2->model = NULL;
	}

	if (md2->grpatch)
	{
		Patch_Free(md2->grpatch);
		md2->grpatch = NULL;
	}

	if (md2->blendgrpatch)
	{
		Patch_Free(md2->blendgrpatch);
		md2->blendgrpatch = NULL;
	}
}

void HWR_FreeModelData(void)
{
	size_t i;
	INT32 s;

	if (GPU)
		GPU->DeleteModelData();

	for (s = 0; s < MAXSKINS; s++)
		FreeModelData(&md2_playermodels[s]);
	for (i = 0; i < NUMSPRITES; i++)
		FreeModelData(&md2_models[i]);

	if (modelpack)
	{
		W_DeleteResourceFile(modelpack);
		modelpack = NULL;
	}

	HWR_InitModels();
}

// Define for getting accurate color brightness readings according to how the human eye sees them.
// https://en.wikipedia.org/wiki/Relative_luminance
// 0.2126 to red
// 0.7152 to green
// 0.0722 to blue
#define SETBRIGHTNESS(brightness,r,g,b) \
	brightness = (UINT8)(((1063*(UINT16)(r))/5000) + ((3576*(UINT16)(g))/5000) + ((361*(UINT16)(b))/5000))

static void HWR_CreateBlendedTexture(patch_t *gpatch, patch_t *blendgpatch, GLMipmap_t *grMipmap, INT32 skinnum, skincolornum_t color)
{
	GLPatch_t *hwrPatch = gpatch->hardware;
	GLPatch_t *hwrBlendPatch = blendgpatch->hardware;
	UINT16 w = gpatch->width, h = gpatch->height;
	UINT32 size = w*h;
	RGBA_t *image, *blendimage, *cur, blendcolor;
	UINT16 translation[16]; // First the color index
	UINT8 cutoff[16]; // Brightness cutoff before using the next color
	UINT8 translen = 0;
	UINT8 i;

	blendcolor = V_GetColor(0); // initialize
	memset(translation, 0, sizeof(translation));
	memset(cutoff, 0, sizeof(cutoff));

	if (grMipmap->width == 0)
	{
		grMipmap->width = gpatch->width;
		grMipmap->height = gpatch->height;

		// no wrap around, no chroma key
		grMipmap->flags = 0;

		// setup the texture info
		grMipmap->format = GL_TEXFMT_RGBA;
	}

	if (grMipmap->data)
	{
		Z_Free(grMipmap->data);
		grMipmap->data = NULL;
	}

	cur = Z_Malloc(size*4, PU_HWRMODELTEXTURE, &grMipmap->data);
	memset(cur, 0x00, size*4);

	image = hwrPatch->mipmap->data;
	blendimage = hwrBlendPatch->mipmap->data;

	// TC_METALSONIC includes an actual skincolor translation, on top of its flashing.
	if (skinnum == TC_METALSONIC)
		color = SKINCOLOR_COBALT;

	if (color != SKINCOLOR_NONE && color < numskincolors)
	{
		UINT8 numdupes = 1;

		translation[translen] = skincolors[color].ramp[0];
		cutoff[translen] = 255;

		for (i = 1; i < 16; i++)
		{
			if (translation[translen] == skincolors[color].ramp[i])
			{
				numdupes++;
				continue;
			}

			if (translen > 0)
			{
				cutoff[translen] = cutoff[translen-1] - (256 / (16 / numdupes));
			}

			numdupes = 1;
			translen++;

			translation[translen] = (UINT16)skincolors[color].ramp[i];
		}

		translen++;
	}

	while (size--)
	{
		if (skinnum == TC_ALLWHITE)
		{
			// Turn everything white
			cur->s.red = cur->s.green = cur->s.blue = 255;
			cur->s.alpha = image->s.alpha;
		}
		else
		{
			// Everything below requires a blend image
			if (blendimage == NULL)
			{
				cur->rgba = image->rgba;
				goto skippixel;
			}

			// Metal Sonic dash mode
			if (skinnum == TC_DASHMODE)
			{
				if (image->s.alpha == 0 && blendimage->s.alpha == 0)
				{
					// Don't bother with blending the pixel if the alpha of the blend pixel is 0
					cur->rgba = image->rgba;
				}
				else
				{
					UINT8 ialpha = 255 - blendimage->s.alpha, balpha = blendimage->s.alpha;
					RGBA_t icolor = *image, bcolor;

					memset(&bcolor, 0x00, sizeof(RGBA_t));

					if (blendimage->s.alpha)
					{
						bcolor.s.blue = 0;
						bcolor.s.red = 255;
						bcolor.s.green = (blendimage->s.red + blendimage->s.green + blendimage->s.blue) / 3;
					}

					if (image->s.alpha && image->s.red > image->s.green << 1) // this is pretty arbitrary, but it works well for Metal Sonic
					{
						icolor.s.red = image->s.blue;
						icolor.s.blue = image->s.red;
					}

					cur->s.red = (ialpha * icolor.s.red + balpha * bcolor.s.red)/255;
					cur->s.green = (ialpha * icolor.s.green + balpha * bcolor.s.green)/255;
					cur->s.blue = (ialpha * icolor.s.blue + balpha * bcolor.s.blue)/255;
					cur->s.alpha = image->s.alpha;
				}
			}
			else
			{
				// All settings that use skincolors!
				UINT16 brightness;

				if (translen <= 0)
				{
					cur->rgba = image->rgba;
					goto skippixel;
				}

				// Don't bother with blending the pixel if the alpha of the blend pixel is 0
				if (skinnum == TC_RAINBOW)
				{
					if (image->s.alpha == 0 && blendimage->s.alpha == 0)
					{
						cur->rgba = image->rgba;
						goto skippixel;
					}
					else
					{
						UINT16 imagebright, blendbright;
						SETBRIGHTNESS(imagebright,image->s.red,image->s.green,image->s.blue);
						SETBRIGHTNESS(blendbright,blendimage->s.red,blendimage->s.green,blendimage->s.blue);
						// slightly dumb average between the blend image color and base image colour, usually one or the other will be fully opaque anyway
						brightness = (imagebright*(255-blendimage->s.alpha))/255 + (blendbright*blendimage->s.alpha)/255;
					}
				}
				else
				{
					if (blendimage->s.alpha == 0)
					{
						cur->rgba = image->rgba;
						goto skippixel; // for metal sonic blend
					}
					else
					{
						SETBRIGHTNESS(brightness,blendimage->s.red,blendimage->s.green,blendimage->s.blue);
					}
				}

				// Calculate a sort of "gradient" for the skincolor
				// (Me splitting this into a function didn't work, so I had to ruin this entire function's groove...)
				{
					RGBA_t nextcolor;
					UINT8 firsti, secondi, mul, mulmax;
					INT32 r, g, b;

					// Rainbow needs to find the closest match to the textures themselves, instead of matching brightnesses to other colors.
					// Ensue horrible mess.
					if (skinnum == TC_RAINBOW)
					{
						UINT16 brightdif = 256;
						UINT8 colorbrightnesses[16];
						INT32 compare, m, d;

						// Ignore pure white & pitch black
						if (brightness > 253 || brightness < 2)
						{
							cur->rgba = image->rgba;
							cur++; image++; blendimage++;
							continue;
						}

						firsti = 0;
						mul = 0;
						mulmax = 1;

						for (i = 0; i < translen; i++)
						{
							RGBA_t tempc = V_GetColor(translation[i]);
							SETBRIGHTNESS(colorbrightnesses[i], tempc.s.red, tempc.s.green, tempc.s.blue); // store brightnesses for comparison
						}

						for (i = 0; i < translen; i++)
						{
							if (brightness > colorbrightnesses[i]) // don't allow greater matches (because calculating a makeshift gradient for this is already a huge mess as is)
								continue;

							compare = abs((INT16)(colorbrightnesses[i]) - (INT16)(brightness));

							if (compare < brightdif)
							{
								brightdif = (UINT16)compare;
								firsti = i; // best matching color that's equal brightness or darker
							}
						}

						secondi = firsti+1; // next color in line
						if (secondi >= translen)
						{
							m = (INT16)brightness; // - 0;
							d = (INT16)colorbrightnesses[firsti]; // - 0;
						}
						else
						{
							m = (INT16)brightness - (INT16)colorbrightnesses[secondi];
							d = (INT16)colorbrightnesses[firsti] - (INT16)colorbrightnesses[secondi];
						}

						if (m >= d)
							m = d-1;

						mulmax = 16;

						// calculate the "gradient" multiplier based on how close this color is to the one next in line
						if (m <= 0 || d <= 0)
							mul = 0;
						else
							mul = (mulmax-1) - ((m * mulmax) / d);
					}
					else
					{
						// Just convert brightness to a skincolor value, use distance to next position to find the gradient multipler
						firsti = 0;

						for (i = 1; i < translen; i++)
						{
							if (brightness >= cutoff[i])
								break;
							firsti = i;
						}

						secondi = firsti+1;

						mulmax = cutoff[firsti];
						if (secondi < translen)
							mulmax -= cutoff[secondi];

						mul = cutoff[firsti] - brightness;
					}

					blendcolor = V_GetColor(translation[firsti]);

					if (secondi >= translen)
						mul = 0;

					if (mul > 0) // If it's 0, then we only need the first color.
					{
#if 0
						if (secondi >= translen)
						{
							// blend to black
							nextcolor = V_GetColor(31);
						}
						else
#endif
							nextcolor = V_GetColor(translation[secondi]);

						// Find difference between points
						r = (INT32)(nextcolor.s.red - blendcolor.s.red);
						g = (INT32)(nextcolor.s.green - blendcolor.s.green);
						b = (INT32)(nextcolor.s.blue - blendcolor.s.blue);

						// Find the gradient of the two points
						r = ((mul * r) / mulmax);
						g = ((mul * g) / mulmax);
						b = ((mul * b) / mulmax);

						// Add gradient value to color
						blendcolor.s.red += r;
						blendcolor.s.green += g;
						blendcolor.s.blue += b;
					}
				}

				if (skinnum == TC_RAINBOW)
				{
					UINT32 tempcolor;
					UINT16 colorbright;

					SETBRIGHTNESS(colorbright,blendcolor.s.red,blendcolor.s.green,blendcolor.s.blue);
					if (colorbright == 0)
						colorbright = 1; // no dividing by 0 please

					tempcolor = (brightness * blendcolor.s.red) / colorbright;
					tempcolor = min(255, tempcolor);
					cur->s.red = (UINT8)tempcolor;

					tempcolor = (brightness * blendcolor.s.green) / colorbright;
					tempcolor = min(255, tempcolor);
					cur->s.green = (UINT8)tempcolor;

					tempcolor = (brightness * blendcolor.s.blue) / colorbright;
					tempcolor = min(255, tempcolor);
					cur->s.blue = (UINT8)tempcolor;
					cur->s.alpha = image->s.alpha;
				}
				else
				{
					// Color strength depends on image alpha
					INT32 tempcolor;

					tempcolor = ((image->s.red * (255-blendimage->s.alpha)) / 255) + ((blendcolor.s.red * blendimage->s.alpha) / 255);
					tempcolor = min(255, tempcolor);
					cur->s.red = (UINT8)tempcolor;

					tempcolor = ((image->s.green * (255-blendimage->s.alpha)) / 255) + ((blendcolor.s.green * blendimage->s.alpha) / 255);
					tempcolor = min(255, tempcolor);
					cur->s.green = (UINT8)tempcolor;

					tempcolor = ((image->s.blue * (255-blendimage->s.alpha)) / 255) + ((blendcolor.s.blue * blendimage->s.alpha) / 255);
					tempcolor = min(255, tempcolor);
					cur->s.blue = (UINT8)tempcolor;
					cur->s.alpha = image->s.alpha;
				}

skippixel:

				// *Now* we can do Metal Sonic's flashing
				if (skinnum == TC_METALSONIC)
				{
					// Blend dark blue into white
					if (cur->s.alpha > 0 && cur->s.red == 0 && cur->s.green == 0 && cur->s.blue < 255 && cur->s.blue > 31)
					{
						// Sal: Invert non-blue
						cur->s.red = cur->s.green = (255 - cur->s.blue);
						cur->s.blue = 255;
					}

					cur->s.alpha = image->s.alpha;
				}
				else if (skinnum == TC_BOSS)
				{
					// Turn everything below a certain threshold white
					if ((image->s.red == image->s.green) && (image->s.green == image->s.blue) && image->s.blue < 127)
					{
						// Lactozilla: Invert the colors
						cur->s.red = cur->s.green = cur->s.blue = (255 - image->s.blue);
					}
				}
			}
		}

		cur++; image++;

		if (blendimage != NULL)
			blendimage++;
	}

	return;
}

#undef SETBRIGHTNESS

static void HWR_GetBlendedTexture(patch_t *patch, patch_t *blendpatch, INT32 skinnum, const UINT8 *colormap, skincolornum_t color)
{
	// mostly copied from HWR_GetMappedPatch, hence the similarities and comment
	GLPatch_t *grPatch = patch->hardware;
	GLPatch_t *grBlendPatch = NULL;
	GLMipmap_t *grMipmap, *newMipmap;

	if (blendpatch == NULL || colormap == colormaps || colormap == NULL)
	{
		// Don't do any blending
		GPU->SetTexture(grPatch->mipmap);
		return;
	}

	if ((blendpatch && (grBlendPatch = blendpatch->hardware) && grBlendPatch->mipmap->format)
		&& (patch->width != blendpatch->width || patch->height != blendpatch->height))
	{
		// Blend image exists, but it's bad.
		GPU->SetTexture(grPatch->mipmap);
		return;
	}

	// search for the mipmap
	// skip the first (no colormap translated)
	for (grMipmap = grPatch->mipmap; grMipmap->nextcolormap; )
	{
		grMipmap = grMipmap->nextcolormap;
		if (grMipmap->colormap && grMipmap->colormap->source == colormap)
		{
			if (grMipmap->downloaded && grMipmap->data)
			{
				if (memcmp(grMipmap->colormap->data, colormap, 256 * sizeof(UINT8)))
				{
					M_Memcpy(grMipmap->colormap->data, colormap, 256 * sizeof(UINT8));
					HWR_CreateBlendedTexture(patch, blendpatch, grMipmap, skinnum, color);
					GPU->UpdateTexture(grMipmap);
				}
				else
					GPU->SetTexture(grMipmap); // found the colormap, set it to the correct texture

				Z_ChangeTag(grMipmap->data, PU_HWRMODELTEXTURE_UNLOCKED);
				return;
			}
		}
	}

	// If here, the blended texture has not been created
	// So we create it

	//BP: WARNING: don't free it manually without clearing the cache of harware renderer
	//              (it have a liste of mipmap)
	//    this malloc is cleared in HWR_FreeColormapCache
	//    (...) unfortunately z_malloc fragment alot the memory :(so malloc is better
	newMipmap = calloc(1, sizeof (*newMipmap));
	if (newMipmap == NULL)
		I_Error("%s: Out of memory", "HWR_GetBlendedTexture");
	grMipmap->nextcolormap = newMipmap;

	newMipmap->colormap = Z_Calloc(sizeof(*newMipmap->colormap), PU_HWRPATCHCOLMIPMAP, NULL);
	newMipmap->colormap->source = colormap;
	M_Memcpy(newMipmap->colormap->data, colormap, 256 * sizeof(UINT8));

	HWR_CreateBlendedTexture(patch, blendpatch, newMipmap, skinnum, color);

	GPU->SetTexture(newMipmap);
	Z_ChangeTag(newMipmap->data, PU_HWRMODELTEXTURE_UNLOCKED);
}

#define NORMALFOG 0x00000000
#define FADEFOG 0x19000000

static boolean HWR_AllowModel(mobj_t *mobj)
{
	// Signpost overlay. Not needed.
	if (mobj->state-states == S_PLAY_SIGN)
		return false;

	// Otherwise, render the model.
	return true;
}

static boolean HWR_CanInterpolateModel(mobj_t *mobj, model_t *model)
{
	if (cv_glmodelinterpolation.value == 2) // Always interpolate
		return true;
	return model->interpolate[(mobj->frame & FF_FRAMEMASK)];
}

static boolean HWR_CanInterpolateSprite2(modelspr2frames_t *spr2frame)
{
	if (cv_glmodelinterpolation.value == 2) // Always interpolate
		return true;
	return spr2frame->interpolate;
}

//
// HWR_GetModelSprite2 (see P_GetSkinSprite2)
// For non-super players, tries each sprite2's immediate predecessor until it finds one with a number of frames or ends up at standing.
// For super players, does the same as above - but tries the super equivalent for each sprite2 before the non-super version.
//

static UINT8 HWR_GetModelSprite2(md2_t *md2, skin_t *skin, UINT8 spr2, player_t *player)
{
	UINT8 super = 0, i = 0;

	if (!md2 || !md2->model || !md2->model->spr2frames || !skin)
		return 0;

	if ((playersprite_t)(spr2 & ~FF_SPR2SUPER) >= free_spr2)
		return 0;

	while (!md2->model->spr2frames[spr2].numframes
		&& spr2 != SPR2_STND
		&& ++i != 32) // recursion limiter
	{
		if (spr2 & FF_SPR2SUPER)
		{
			super = FF_SPR2SUPER;
			spr2 &= ~FF_SPR2SUPER;
			continue;
		}

		switch(spr2)
		{
		// Normal special cases.
		case SPR2_JUMP:
			spr2 = ((player
					? player->charflags
					: skin->flags)
					& SF_NOJUMPSPIN) ? SPR2_SPNG : SPR2_ROLL;
			break;
		case SPR2_TIRE:
			spr2 = ((player
					? player->charability
					: skin->ability)
					== CA_SWIM) ? SPR2_SWIM : SPR2_FLY;
			break;
		// Use the handy list, that's what it's there for!
		default:
			spr2 = spr2defaults[spr2];
			break;
		}

		spr2 |= super;
	}

	if (i >= 32) // probably an infinite loop...
		return 0;

	return spr2;
}

// Adjust texture coords of model to fit into a patch's max_s and max_t
static void adjustTextureCoords(model_t *model, patch_t *patch)
{
	int i;
	GLPatch_t *gpatch = ((GLPatch_t *)patch->hardware);

	for (i = 0; i < model->numMeshes; i++)
	{
		int j;
		mesh_t *mesh = &model->meshes[i];
		int numVertices;
		float *uvReadPtr = mesh->originaluvs;
		float *uvWritePtr;

		// i dont know if this is actually possible, just logical conclusion of structure in CreateModelVBOs
		if (!mesh->frames && !mesh->tinyframes) continue;

		if (mesh->frames) // again CreateModelVBO and CreateModelVBOTiny iterate like this so I'm gonna do that too
			numVertices = mesh->numTriangles * 3;
		else
			numVertices = mesh->numVertices;

		// if originaluvs points to uvs, we need to allocate new memory for adjusted uvs
		// the old uvs are kept around for use in possible readjustments
		if (mesh->uvs == mesh->originaluvs)
			mesh->uvs = Z_Malloc(numVertices * 2 * sizeof(float), PU_STATIC, NULL);

		uvWritePtr = mesh->uvs;

		// fix uvs (texture coordinates) to take into account that the actual texture
		// has empty space added until the next power of two
		for (j = 0; j < numVertices; j++)
		{
			*uvWritePtr++ = *uvReadPtr++ * gpatch->max_s;
			*uvWritePtr++ = *uvReadPtr++ * gpatch->max_t;
		}
	}
	// Save the values we adjusted the uvs for
	model->max_s = gpatch->max_s;
	model->max_t = gpatch->max_t;
}

//
// HWR_DrawModel
//

boolean HWR_DrawModel(gl_vissprite_t *spr)
{
	md2_t *md2;

	char filename[64];
	INT32 frame = 0;
	INT32 nextFrame = -1;
	UINT8 spr2 = 0;
	FTransform p;
	FSurfaceInfo Surf;
	FBITFIELD flags;

	if (!cv_glmodels.value)
		return false;

	if (spr->precip)
		return false;

	// Lactozilla: Disallow certain models from rendering
	if (!HWR_AllowModel(spr->mobj))
		return false;

	memset(&p, 0x00, sizeof(FTransform));

	// MD2 colormap fix
	// colormap test
	if (spr->mobj->subsector)
	{
		sector_t *sector = spr->mobj->subsector->sector;
		UINT8 lightlevel = 255;
		extracolormap_t *colormap = NULL;

		if (sector->numlights)
		{
			INT32 light;

			light = R_GetPlaneLight(sector, spr->mobj->z + spr->mobj->height, false); // Always use the light at the top instead of whatever I was doing before

			if (R_ThingIsFullDark(spr->mobj))
				lightlevel = 0;
			else if (R_ThingIsSemiBright(spr->mobj))
				lightlevel = 128 + (*sector->lightlist[light].lightlevel>>1);
			else if (!R_ThingIsFullBright(spr->mobj))
				lightlevel = *sector->lightlist[light].lightlevel > 255 ? 255 : *sector->lightlist[light].lightlevel;

			if (*sector->lightlist[light].extra_colormap)
				colormap = *sector->lightlist[light].extra_colormap;
		}
		else
		{
			if (R_ThingIsFullDark(spr->mobj))
				lightlevel = 0;
			else if (R_ThingIsSemiBright(spr->mobj))
				lightlevel = 128 + (sector->lightlevel>>1);
			else if (!R_ThingIsFullBright(spr->mobj))
				lightlevel = sector->lightlevel > 255 ? 255 : sector->lightlevel;

			if (sector->extra_colormap)
				colormap = sector->extra_colormap;
		}

		HWR_Lighting(&Surf, lightlevel, colormap);
	}
	else
		Surf.PolyColor.rgba = 0xFFFFFFFF;

	// Look at HWR_ProjectSprite for more
	{
		patch_t *gpatch, *blendgpatch;
		GLPatch_t *hwrPatch = NULL, *hwrBlendPatch = NULL;
		float durs = (float)spr->mobj->state->tics;
		float tics = (float)spr->mobj->tics;
		const boolean papersprite = (R_ThingIsPaperSprite(spr->mobj) && !R_ThingIsFloorSprite(spr->mobj));
		const UINT8 flip = (UINT8)(!(spr->mobj->eflags & MFE_VERTICALFLIP) != !R_ThingVerticallyFlipped(spr->mobj));
		const UINT8 hflip = (UINT8)(!(spr->mobj->mirrored) != !R_ThingHorizontallyFlipped(spr->mobj));
		spritedef_t *sprdef;
		spriteframe_t *sprframe;
		spriteinfo_t *sprinfo;
		angle_t ang;
		INT32 mod;
		float finalscale;
		interpmobjstate_t interp;

		if (R_UsingFrameInterpolation() && !paused)
		{
			R_InterpolateMobjState(spr->mobj, rendertimefrac, &interp);
		}
		else
		{
			R_InterpolateMobjState(spr->mobj, FRACUNIT, &interp);
		}

		// Apparently people don't like jump frames like that, so back it goes
		//if (tics > durs)
			//durs = tics;

		INT32 blendmode;
		if (spr->mobj->frame & FF_BLENDMASK)
			blendmode = ((spr->mobj->frame & FF_BLENDMASK) >> FF_BLENDSHIFT) + 1;
		else
			blendmode = spr->mobj->blendmode;

		if (spr->mobj->frame & FF_TRANSMASK)
			Surf.PolyFlags = HWR_SurfaceBlend(blendmode, (spr->mobj->frame & FF_TRANSMASK)>>FF_TRANSSHIFT, &Surf);
		else
		{
			Surf.PolyColor.s.alpha = (spr->mobj->flags2 & MF2_SHADOW) ? 0x40 : 0xff;
			Surf.PolyFlags = HWR_GetBlendModeFlag(blendmode);
		}

		// don't forget to enable the depth test because we can't do this
		// like before: model polygons are not sorted

		// 1. load model+texture if not already loaded
		// 2. draw model with correct position, rotation,...
		if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY) // Use the player MD2 list if the mobj has a skin and is using the player sprites
		{
			md2 = &md2_playermodels[(skin_t*)spr->mobj->skin-skins];
			md2->skin = (skin_t*)spr->mobj->skin-skins;
			sprinfo = &((skin_t *)spr->mobj->skin)->sprinfo[spr->mobj->sprite2];
		}
		else
		{
			md2 = &md2_models[spr->mobj->sprite];
			sprinfo = &spriteinfo[spr->mobj->sprite];
		}

		// texture loading before model init, so it knows if sprite graphics are used, which
		// means that texture coordinates have to be adjusted
		gpatch = md2->grpatch;
		if (gpatch)
			hwrPatch = ((GLPatch_t *)gpatch->hardware);

		if (!gpatch || !hwrPatch
		|| ((!hwrPatch->mipmap->format || !hwrPatch->mipmap->downloaded) && !md2->notexturefile))
			md2_loadTexture(md2);

		// Load it again, because it isn't being loaded into gpatch after md2_loadtexture...
		gpatch = md2->grpatch;
		if (gpatch)
			hwrPatch = ((GLPatch_t *)gpatch->hardware);

		// Load blend texture
		blendgpatch = md2->blendgrpatch;
		if (blendgpatch)
			hwrBlendPatch = ((GLPatch_t *)blendgpatch->hardware);

		if ((gpatch && hwrPatch && hwrPatch->mipmap->format) // don't load the blend texture if the base texture isn't available
			&& (!blendgpatch || !hwrBlendPatch
			|| ((!hwrBlendPatch->mipmap->format || !hwrBlendPatch->mipmap->downloaded) && !md2->noblendfile)))
			md2_loadBlendTexture(md2);

		// Load it again, because it isn't being loaded into blendgpatch after md2_loadblendtexture...
		blendgpatch = md2->blendgrpatch;
		if (blendgpatch)
			hwrBlendPatch = ((GLPatch_t *)blendgpatch->hardware);

		if (md2->error)
			return false; // we already failed loading this before :(
		if (!md2->model)
		{
			//CONS_Debug(DBG_RENDER, "Loading model... (%s)", sprnames[spr->mobj->sprite]);
			sprintf(filename, "models/%s", md2->filename);
			md2->model = md2_readModel(filename);

			if (md2->model)
			{
				// If model uses sprite patch as texture, then
				// adjust texture coordinates to take power of two textures into account
				if (!gpatch || !hwrPatch->mipmap->format)
					adjustTextureCoords(md2->model, spr->gpatch);
				// note down the max_s and max_t that end up in the VBO
				md2->model->vbo_max_s = md2->model->max_s;
				md2->model->vbo_max_t = md2->model->max_t;
			}
			else
			{
				//CONS_Debug(DBG_RENDER, " FAILED\n");
				md2->error = true; // prevent endless fail
				return false;
			}
		}

		if (!md2->model->hasVBOs)
		{
			GPU->CreateModelVBOs(md2->model);
			md2->model->hasVBOs = true;
		}

		finalscale = md2->scale;

		if (gpatch && hwrPatch && hwrPatch->mipmap->format) // else if meant that if a texture couldn't be loaded, it would just end up using something else's texture
		{
			INT32 skinnum = TC_DEFAULT;

			if ((spr->mobj->flags & (MF_ENEMY|MF_BOSS)) && (spr->mobj->flags2 & MF2_FRET) && !(spr->mobj->flags & MF_GRENADEBOUNCE) && (leveltime & 1)) // Bosses "flash"
			{
				if (spr->mobj->type == MT_CYBRAKDEMON || spr->mobj->colorized)
					skinnum = TC_ALLWHITE;
				else if (spr->mobj->type == MT_METALSONIC_BATTLE)
					skinnum = TC_METALSONIC;
				else
					skinnum = TC_BOSS;
			}
			else if ((skincolornum_t)spr->mobj->color != SKINCOLOR_NONE)
			{
				if (spr->mobj->colorized)
					skinnum = TC_RAINBOW;
				else if (spr->mobj->player && spr->mobj->player->dashmode >= DASHMODE_THRESHOLD
					&& (spr->mobj->player->charflags & SF_DASHMODE)
					&& ((leveltime/2) & 1))
				{
					if (spr->mobj->player->charflags & SF_MACHINE)
						skinnum = TC_DASHMODE;
					else
						skinnum = TC_RAINBOW;
				}
				else if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY)
					skinnum = (INT32)((skin_t*)spr->mobj->skin-skins);
				else
					skinnum = TC_DEFAULT;
			}

			// Translation or skin number found
			HWR_GetBlendedTexture(gpatch, blendgpatch, skinnum, spr->colormap, (skincolornum_t)spr->mobj->color);
		}
		else // Sprite
		{
			// Check if sprite dimensions are different from previously used sprite.
			// If so, uvs need to be readjusted.
			// Comparing floats with the != operator here should be okay because they
			// are just copies of glpatches' max_s and max_t values.
			// Instead of the != operator, memcmp is used to avoid a compiler warning.
			if (memcmp(&(hwrPatch->max_s), &(md2->model->max_s), sizeof(md2->model->max_s)) != 0 ||
				memcmp(&(hwrPatch->max_t), &(md2->model->max_t), sizeof(md2->model->max_t)) != 0)
				adjustTextureCoords(md2->model, spr->gpatch);
			HWR_GetMappedPatch(spr->gpatch, spr->colormap);
		}

		if (spr->mobj->frame & FF_ANIMATE)
		{
			// set duration and tics to be the correct values for FF_ANIMATE states
			durs = (float)spr->mobj->state->var2;
			tics = (float)spr->mobj->anim_duration;
		}

		frame = (spr->mobj->frame & FF_FRAMEMASK);
		if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY && md2->model->spr2frames)
		{
			spr2 = HWR_GetModelSprite2(md2, spr->mobj->skin, spr->mobj->sprite2, spr->mobj->player);
			mod = md2->model->spr2frames[spr2].numframes;
#ifndef DONTHIDEDIFFANIMLENGTH // by default, different anim length is masked by the mod
			if (mod > (INT32)((skin_t *)spr->mobj->skin)->sprites[spr2].numframes)
				mod = ((skin_t *)spr->mobj->skin)->sprites[spr2].numframes;
#endif
			if (!mod)
				mod = 1;
			frame = md2->model->spr2frames[spr2].frames[frame%mod];
		}
		else
		{
			mod = md2->model->meshes[0].numFrames;
			if (!mod)
				mod = 1;
		}

#ifdef USE_MODEL_NEXTFRAME
		// Interpolate the model interpolation. (lol)
		tics -= FixedToFloat(rendertimefrac);

#define INTERPOLERATION_LIMIT (TICRATE * 0.25f)

		if (cv_glmodelinterpolation.value && tics <= durs && tics <= INTERPOLERATION_LIMIT)
		{
			if (durs > INTERPOLERATION_LIMIT)
				durs = INTERPOLERATION_LIMIT;

			if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY && md2->model->spr2frames)
			{
				if (HWR_CanInterpolateSprite2(&md2->model->spr2frames[spr2])
					&& (spr->mobj->frame & FF_ANIMATE
					|| (spr->mobj->state->nextstate != S_NULL
					&& states[spr->mobj->state->nextstate].sprite == SPR_PLAY
					&& ((P_GetSkinSprite2(spr->mobj->skin, (((spr->mobj->player && spr->mobj->player->powers[pw_super]) ? FF_SPR2SUPER : 0)|states[spr->mobj->state->nextstate].frame) & FF_FRAMEMASK, spr->mobj->player) == spr->mobj->sprite2)))))
				{
					nextFrame = (spr->mobj->frame & FF_FRAMEMASK) + 1;
					if (nextFrame >= mod)
					{
						if (spr->mobj->state->frame & FF_SPR2ENDSTATE)
							nextFrame--;
						else
							nextFrame = 0;
					}
					if (frame || !(spr->mobj->state->frame & FF_SPR2ENDSTATE))
						nextFrame = md2->model->spr2frames[spr2].frames[nextFrame];
					else
						nextFrame = -1;
				}
			}
			else if (HWR_CanInterpolateModel(spr->mobj, md2->model))
			{
				// frames are handled differently for states with FF_ANIMATE, so get the next frame differently for the interpolation
				if (spr->mobj->frame & FF_ANIMATE)
				{
					nextFrame = (spr->mobj->frame & FF_FRAMEMASK) + 1;
					if (nextFrame >= (INT32)(spr->mobj->state->var1 + (spr->mobj->state->frame & FF_FRAMEMASK)))
						nextFrame = (spr->mobj->state->frame & FF_FRAMEMASK) % mod;
				}
				else
				{
					if (spr->mobj->state->nextstate != S_NULL && states[spr->mobj->state->nextstate].sprite != SPR_NULL
					&& !(spr->mobj->player && (spr->mobj->state->nextstate == S_PLAY_WAIT) && spr->mobj->state == &states[S_PLAY_STND]))
						nextFrame = (states[spr->mobj->state->nextstate].frame & FF_FRAMEMASK) % mod;
				}
			}
		}
#undef INTERPOLERATION_LIMIT
#endif

		//Hurdler: it seems there is still a small problem with mobj angle
		p.x = FIXED_TO_FLOAT(interp.x);
		p.y = FIXED_TO_FLOAT(interp.y)+md2->offset;

		if (flip)
			p.z = FIXED_TO_FLOAT(interp.z + spr->mobj->height);
		else
			p.z = FIXED_TO_FLOAT(interp.z);

		if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY)
			sprdef = &((skin_t *)spr->mobj->skin)->sprites[spr->mobj->sprite2];
		else
			sprdef = &sprites[spr->mobj->sprite];

		sprframe = &sprdef->spriteframes[spr->mobj->frame & FF_FRAMEMASK];

		if (sprframe->rotate || papersprite)
		{
			fixed_t anglef = AngleFixed(interp.angle);

			p.angley = FIXED_TO_FLOAT(anglef);
		}
		else
		{
			const fixed_t anglef = AngleFixed((R_PointToAngle(interp.x, interp.y))-ANGLE_180);
			p.angley = FIXED_TO_FLOAT(anglef);
		}

		p.rollangle = 0.0f;
		p.rollflip = 1;
		p.rotaxis = 0;
		if (spr->mobj->rollangle)
		{
			fixed_t anglef = AngleFixed(spr->mobj->rollangle);
			p.rollangle = FIXED_TO_FLOAT(anglef);
			p.roll = true;

			// rotation pivot
			p.centerx = FIXED_TO_FLOAT(spr->mobj->radius/2);
			p.centery = FIXED_TO_FLOAT(spr->mobj->height/(flip ? -2 : 2));

			// rotation axis
			if (sprinfo->available)
				p.rotaxis = (UINT8)(sprinfo->pivot[(spr->mobj->frame & FF_FRAMEMASK)].rotaxis);

			// for NiGHTS specifically but should work everywhere else
			ang = R_PointToAngle (interp.x, interp.y) - interp.angle;
			if ((sprframe->rotate & SRF_RIGHT) && (ang < ANGLE_180)) // See from right
				p.rollflip = 1;
			else if ((sprframe->rotate & SRF_LEFT) && (ang >= ANGLE_180)) // See from left
				p.rollflip = -1;

			if (flip)
				p.rollflip *= -1;
		}

		p.anglex = 0.0f;

#ifdef USE_FTRANSFORM_ANGLEZ
		// Slope rotation from Kart
		p.anglez = 0.0f;
		if (spr->mobj->standingslope)
		{
			fixed_t tempz = spr->mobj->standingslope->normal.z;
			fixed_t tempy = spr->mobj->standingslope->normal.y;
			fixed_t tempx = spr->mobj->standingslope->normal.x;
			fixed_t tempangle = AngleFixed(R_PointToAngle2(0, 0, FixedSqrt(FixedMul(tempy, tempy) + FixedMul(tempz, tempz)), tempx));
			p.anglez = FIXED_TO_FLOAT(tempangle);
			tempangle = -AngleFixed(R_PointToAngle2(0, 0, tempz, tempy));
			p.anglex = FIXED_TO_FLOAT(tempangle);
		}
#endif

		// SRB2CBTODO: MD2 scaling support
		finalscale *= FIXED_TO_FLOAT(spr->mobj->scale);

		p.flip = atransform.flip;
#ifdef USE_FTRANSFORM_MIRROR
		p.mirror = atransform.mirror; // from Kart
#endif

		flags = (Surf.PolyFlags | PF_Modulated);
		if (Surf.PolyFlags & (PF_Additive|PF_Subtractive|PF_ReverseSubtract|PF_Multiplicative))
			flags |= PF_Occlude;
		else if (Surf.PolyColor.s.alpha == 0xFF)
			flags |= (PF_Occlude | PF_Masked);

		GPU->SetBlend(flags);
		GPU->SetShader(SHADER_MODEL);
		GPU->DrawModel(md2->model, frame, durs, tics, nextFrame, &p, finalscale, flip, hflip, &Surf);
	}

	GPU->SetShader(SHADER_DEFAULT);

	return true;
}

#endif //HWRENDER
