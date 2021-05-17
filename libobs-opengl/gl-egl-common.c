/******************************************************************************
    Copyright (C) 2020 by Georges Basile Stavracas Neto <georges.stavracas@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "gl-egl-common.h"

#include <stdio.h>
#include <stdlib.h>

#include <glad/glad_egl.h>

#if defined(__linux__)

#include <linux/types.h>
#include <asm/ioctl.h>
typedef unsigned int drm_handle_t;

#else

#include <stdint.h>
#include <sys/ioccom.h>
#include <sys/types.h>
typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;
typedef size_t __kernel_size_t;
typedef unsigned long drm_handle_t;

#endif

#define DRM_FORMAT_RESERVED	      ((1ULL << 56) - 1)

#define DRM_FORMAT_MOD_VENDOR_NONE    0

#define fourcc_mod_code(vendor, val) \
	((((__u64)DRM_FORMAT_MOD_VENDOR_## vendor) << 56) | ((val) & 0x00ffffffffffffffULL))

#define DRM_FORMAT_MOD_INVALID	fourcc_mod_code(NONE, DRM_FORMAT_RESERVED)

typedef void(APIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(
	GLenum target, GLeglImageOES image);
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

typedef EGLBoolean (APIENTRYP PFNEGLQUERYDMABUFFORMATSEXTPROC)(
	EGLDisplay dpy, EGLint max_formats, EGLint *formats, EGLint *num_formats);
static PFNEGLQUERYDMABUFFORMATSEXTPROC glEGLQueryDmaBufFormats;
typedef EGLBoolean (APIENTRYP PFNEGLQUERYDMABUFMODIFIERSEXTPROC)(
	EGLDisplay dpy, EGLint format, EGLint max_modifiers, EGLuint64KHR *modifiers,
	EGLBoolean *external_only, EGLint *num_modifiers);
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC glEGLQueryDmaBufModifiers;

static bool find_gl_extension(const char *extension)
{
	GLint n, i;

	glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	for (i = 0; i < n; i++) {
		const char *e = (char *)glGetStringi(GL_EXTENSIONS, i);
		if (extension && strcmp(e, extension) == 0)
			return true;
	}
	return false;
}

static bool init_egl_image_target_texture_2d_ext(void)
{
	static bool initialized = false;

	if (!initialized) {
		initialized = true;

		if (!find_gl_extension("GL_OES_EGL_image")) {
			blog(LOG_ERROR, "No GL_OES_EGL_image");
			return false;
		}

		glEGLImageTargetTexture2DOES =
			(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress(
				"glEGLImageTargetTexture2DOES");
	}

	if (!glEGLImageTargetTexture2DOES)
		return false;

	return true;
}

static bool init_egl_query_dmabuf_formats_ext(void)
{
	static bool initialized = false;

	if (!initialized) {
		initialized = true;

		if (!find_gl_extension("GL_OES_EGL_image")) {
			blog(LOG_ERROR, "No GL_OES_EGL_image");
			return false;
		}

		glEGLQueryDmaBufFormats =
			(PFNEGLQUERYDMABUFFORMATSEXTPROC)eglGetProcAddress(
				"eglQueryDmaBufFormatsEXT");
		if (!glEGLQueryDmaBufFormats)
			blog(LOG_ERROR, "No eglQueryDmaBufFormatsEXT");
	}

	if (!glEGLQueryDmaBufFormats)
		return false;

	return true;
}

static bool init_egl_query_dmabuf_modifiers_ext(void)
{
	static bool initialized = false;

	if (!initialized) {
		initialized = true;

		if (!find_gl_extension("GL_OES_EGL_image")) {
			blog(LOG_ERROR, "No GL_OES_EGL_image");
			return false;
		}

		glEGLQueryDmaBufModifiers =
			(PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress(
				"eglQueryDmaBufModifiersEXT");
		if (!glEGLQueryDmaBufModifiers)
			blog(LOG_ERROR, "No eglQueryDmaBufFormatsEXT");
	}

	if (!glEGLQueryDmaBufModifiers)
		return false;

	return true;
}

static EGLImageKHR
create_dmabuf_egl_image(EGLDisplay egl_display, unsigned int width,
			unsigned int height, uint32_t drm_format,
			uint32_t n_planes, const int *fds,
			const uint32_t *strides, const uint32_t *offsets,
			const uint64_t *modifiers)
{
	EGLAttrib attribs[47];
	int atti = 0;

	/* This requires the Mesa commit in
	 * Mesa 10.3 (08264e5dad4df448e7718e782ad9077902089a07) or
	 * Mesa 10.2.7 (55d28925e6109a4afd61f109e845a8a51bd17652).
	 * Otherwise Mesa closes the fd behind our back and re-importing
	 * will fail.
	 * https://bugs.freedesktop.org/show_bug.cgi?id=76188
	 * */

	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = drm_format;

	if (n_planes > 0) {
		attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[atti++] = fds[0];
		attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[atti++] = offsets[0];
		attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[atti++] = strides[0];
		if (modifiers && modifiers[0] != DRM_FORMAT_MOD_INVALID) {
			attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[0] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[0] >> 32;
		}
	}

	if (n_planes > 1) {
		attribs[atti++] = EGL_DMA_BUF_PLANE1_FD_EXT;
		attribs[atti++] = fds[1];
		attribs[atti++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
		attribs[atti++] = offsets[1];
		attribs[atti++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
		attribs[atti++] = strides[1];
		if (modifiers && modifiers[1] != DRM_FORMAT_MOD_INVALID) {
			attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[1] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[1] >> 32;
		}
	}

	if (n_planes > 2) {
		attribs[atti++] = EGL_DMA_BUF_PLANE2_FD_EXT;
		attribs[atti++] = fds[2];
		attribs[atti++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
		attribs[atti++] = offsets[2];
		attribs[atti++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
		attribs[atti++] = strides[2];
		if (modifiers && modifiers[2] != DRM_FORMAT_MOD_INVALID) {
			attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[2] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[2] >> 32;
		}
	}

	if (n_planes > 3) {
		attribs[atti++] = EGL_DMA_BUF_PLANE3_FD_EXT;
		attribs[atti++] = fds[3];
		attribs[atti++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
		attribs[atti++] = offsets[3];
		attribs[atti++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
		attribs[atti++] = strides[3];
		if (modifiers && modifiers[3] != DRM_FORMAT_MOD_INVALID) {
			attribs[atti++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[3] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[3] >> 32;
		}
	}

	attribs[atti++] = EGL_NONE;

	return eglCreateImage(egl_display, EGL_NO_CONTEXT,
			      EGL_LINUX_DMA_BUF_EXT, 0, attribs);
}

struct gs_texture *
gl_egl_create_dmabuf_image(EGLDisplay egl_display, unsigned int width,
			   unsigned int height, uint32_t drm_format,
			   enum gs_color_format color_format, uint32_t n_planes,
			   const int *fds, const uint32_t *strides,
			   const uint32_t *offsets, const uint64_t *modifiers)
{
	struct gs_texture *texture = NULL;
	EGLImage egl_image;

	if (!init_egl_image_target_texture_2d_ext())
		return NULL;

	egl_image = create_dmabuf_egl_image(egl_display, width, height,
					    drm_format, n_planes, fds, strides,
					    offsets, modifiers);
	if (egl_image == EGL_NO_IMAGE) {
		blog(LOG_ERROR, "Cannot create EGLImage: %s",
		     gl_egl_error_to_string(eglGetError()));
		return NULL;
	}

	texture = gs_texture_create(width, height, color_format, 1, NULL,
				    GS_DYNAMIC);
	const GLuint gltex = *(GLuint *)gs_texture_get_obj(texture);

	glBindTexture(GL_TEXTURE_2D, gltex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);

	glBindTexture(GL_TEXTURE_2D, 0);
	eglDestroyImage(egl_display, egl_image);

	return texture;
}

int
gl_egl_query_dmabuf_formats(EGLDisplay egl_display, uint32_t **formats)
{
	EGLint max_formats = 0;
	EGLint *format_list = NULL;

	if (!init_egl_query_dmabuf_formats_ext())
		blog(LOG_ERROR, "Unable to load eglQueryDmaBufFormatsEXT");
		return 0;

	if (!glEGLQueryDmaBufFormats(egl_display, 0, NULL, &max_formats)) {
		blog(LOG_ERROR, "Cannot query the number of formats: %s",
		     gl_egl_error_to_string(eglGetError()));
		return 0;
	}

	format_list = calloc(max_formats, sizeof(EGLint));
	if (!format_list) {
		blog(LOG_ERROR, "Unable to allocate memory");
		return 0;
	}

	if (!glEGLQueryDmaBufFormats(egl_display, max_formats, format_list, &max_formats)) {
		blog(LOG_ERROR, "Cannot query a list of modifiers: %s",
		     gl_egl_error_to_string(eglGetError()));
		free(format_list);
		return 0;
	}

	*formats = (uint32_t*)format_list;
	return max_formats;
}

int
gl_egl_query_dmabuf_modifiers(EGLDisplay egl_display, uint32_t drm_format,
			      uint64_t **modifiers)
{
	EGLint max_modifiers = 0;
	EGLuint64KHR *modifier_list = NULL;
	EGLBoolean *external_only = NULL;

	EGLint num_formats;
	uint32_t *formats;
	EGLBoolean format_available = false;

	num_formats = gl_egl_query_dmabuf_formats(egl_display, &formats);
	if (num_formats == 0) {
		blog(LOG_ERROR, "No formats supported by dmabuf");
		goto error_format;
	}

	for (int i = 0; i < num_formats; i++) {
		if (drm_format == formats[i]) {
			format_available = true;
			break;
		}
	}
	free(formats);

	if (!format_available) {
		blog(LOG_ERROR, "Format %u not supported for modifiers", drm_format);
		goto error_format;
	}

	if (!init_egl_query_dmabuf_modifiers_ext()) {
		blog(LOG_ERROR, "Unable to load eglQueryDmaBufModifiersEXT");
		goto error_extensions;
	}

	if (!glEGLQueryDmaBufModifiers(egl_display, drm_format, 0, NULL, NULL, &max_modifiers)) {
		blog(LOG_ERROR, "Cannot query the number of modifiers: %s",
		     gl_egl_error_to_string(eglGetError()));
		goto error_modnum;
	}

	modifier_list = calloc(max_modifiers + 1, sizeof(EGLuint64KHR)); // We want to add DRM_FORMAT_MOD_INVALID manually
	external_only = calloc(max_modifiers, sizeof(EGLBoolean));
	if (!modifier_list || !external_only) {
		blog(LOG_ERROR, "Unable to allocate memory");
		goto error_alloc;
	}

	if (!glEGLQueryDmaBufModifiers(egl_display, drm_format, max_modifiers, modifier_list, external_only, &max_modifiers)) {
		blog(LOG_ERROR, "Cannot query a list of modifiers: %s",
		     gl_egl_error_to_string(eglGetError()));
		goto error_modlist;
	}

	modifier_list[max_modifiers] = DRM_FORMAT_MOD_INVALID;
	free(external_only);
	*modifiers = modifier_list;
	return max_modifiers++;

error_modlist:
error_alloc:
	free(external_only);
	free(modifier_list);
error_modnum:
error_extensions:
	max_modifiers = 1;
	*modifiers = (uint64_t*)calloc(max_modifiers, sizeof(uint64_t));
	*modifiers[0] = DRM_FORMAT_MOD_INVALID;
	return max_modifiers;

error_format:
	return 0;
}

const char *gl_egl_error_to_string(EGLint error_number)
{
	switch (error_number) {
	case EGL_SUCCESS:
		return "The last function succeeded without error.";
		break;
	case EGL_NOT_INITIALIZED:
		return "EGL is not initialized, or could not be initialized, for the specified EGL display connection.";
		break;
	case EGL_BAD_ACCESS:
		return "EGL cannot access a requested resource (for example a context is bound in another thread).";
		break;
	case EGL_BAD_ALLOC:
		return "EGL failed to allocate resources for the requested operation.";
		break;
	case EGL_BAD_ATTRIBUTE:
		return "An unrecognized attribute or attribute value was passed in the attribute list.";
		break;
	case EGL_BAD_CONTEXT:
		return "An EGLContext argument does not name a valid EGL rendering context.";
		break;
	case EGL_BAD_CONFIG:
		return "An EGLConfig argument does not name a valid EGL frame buffer configuration.";
		break;
	case EGL_BAD_CURRENT_SURFACE:
		return "The current surface of the calling thread is a window, pixel buffer or pixmap that is no longer valid.";
		break;
	case EGL_BAD_DISPLAY:
		return "An EGLDisplay argument does not name a valid EGL display connection.";
		break;
	case EGL_BAD_SURFACE:
		return "An EGLSurface argument does not name a valid surface (window, pixel buffer or pixmap) configured for GL rendering.";
		break;
	case EGL_BAD_MATCH:
		return "Arguments are inconsistent (for example, a valid context requires buffers not supplied by a valid surface).";
		break;
	case EGL_BAD_PARAMETER:
		return "One or more argument values are invalid.";
		break;
	case EGL_BAD_NATIVE_PIXMAP:
		return "A NativePixmapType argument does not refer to a valid native pixmap.";
		break;
	case EGL_BAD_NATIVE_WINDOW:
		return "A NativeWindowType argument does not refer to a valid native window.";
		break;
	case EGL_CONTEXT_LOST:
		return "A power management event has occurred. The application must destroy all contexts and reinitialise OpenGL ES state and objects to continue rendering. ";
		break;
	default:
		return "Unknown error";
		break;
	}
}
