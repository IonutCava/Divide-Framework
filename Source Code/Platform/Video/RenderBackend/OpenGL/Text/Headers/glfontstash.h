//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// Modified by Ionut Cava: Updated rendering for compatibility with OpenGL 3.3+
// Core Context

#ifndef GL3COREFONTSTASH_H
#define GL3COREFONTSTASH_H

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#ifdef __cplusplus
extern "C" {
#endif

FONS_DEF FONScontext* glfonsCreate(int width, int height, int flags);
FONS_DEF void glfonsDelete(FONScontext* ctx);

FONS_DEF unsigned int glfonsRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

#ifdef __cplusplus
}
#endif

#endif // GL3COREFONTSTASH_H

#ifdef GLFONTSTASH_IMPLEMENTATION

constexpr GLuint GLFONS_VB_SIZE_FACTOR = 120;
constexpr size_t GLFONS_VB_BUFFER_SIZE = GLFONS_VB_SIZE_FACTOR * sizeof(FONSvert) * FONS_VERTEX_COUNT;
constexpr GLuint GLFONS_VERTEX_ATTRIB = (GLuint)Divide::AttribLocation::POSITION;
constexpr GLuint GLFONS_TCOORD_ATTRIB = (GLuint)Divide::AttribLocation::TEXCOORD;
constexpr GLuint GLFONS_COLOR_ATTRIB = (GLuint)Divide::AttribLocation::COLOR;

struct GLFONScontext {
    GLuint tex;
    int width, height;
	GLuint glfons_vaoID;
    GLuint glfons_vboID;
    FONSvert* glfons_vboData;
};

using GLFONScontext = struct GLFONScontext;

namespace {
    Divide::glLockManager* g_lockManager = nullptr;
};

static int glfons__renderCreate(void* userPtr, int width, int height)
{
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    g_lockManager = MemoryManager_NEW Divide::glLockManager();
	// Create may be called multiple times, delete existing texture.
	if (gl->tex != 0) {
		glDeleteTextures(1, &gl->tex);
		gl->tex = 0;
	}

    glCreateTextures(GL_TEXTURE_2D, 1, &gl->tex);
	if (!gl->tex) return 0;

    if (!gl->glfons_vaoID) glCreateVertexArrays(1, &gl->glfons_vaoID);
	if (!gl->glfons_vaoID) return 0;

    if (Divide::GL_API::GetStateTracker()->setActiveVAO(gl->glfons_vaoID) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    {
        if (!gl->glfons_vboID) {
            glCreateBuffers(1, &gl->glfons_vboID);
        }

        if (!gl->glfons_vboID) {
            return 0;
        }

        const BufferStorageMask storageMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;
        const MapBufferAccessMask accessMask = GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;

        glNamedBufferStorage(gl->glfons_vboID, GLFONS_VB_BUFFER_SIZE, nullptr, storageMask);
        gl->glfons_vboData = (FONSvert*)glMapNamedBufferRange(gl->glfons_vboID, 0, GLFONS_VB_BUFFER_SIZE, accessMask);

         Divide::U32 prevOffset = 0;
        glEnableVertexAttribArray(GLFONS_VERTEX_ATTRIB);
        glVertexAttribFormat(GLFONS_VERTEX_ATTRIB, 2, GL_FLOAT, GL_FALSE, prevOffset);

        prevOffset += Divide::to_U32(sizeof(float) * 2);
        glEnableVertexAttribArray(GLFONS_TCOORD_ATTRIB);
        glVertexAttribFormat(GLFONS_TCOORD_ATTRIB, 2, GL_FLOAT, GL_FALSE, Divide::to_U32(prevOffset));

        prevOffset += Divide::to_U32(sizeof(float) * 2);
        glEnableVertexAttribArray(GLFONS_COLOR_ATTRIB);
        glVertexAttribFormat(GLFONS_COLOR_ATTRIB, 4, GL_UNSIGNED_BYTE, GL_TRUE, prevOffset);

        glVertexAttribBinding(GLFONS_VERTEX_ATTRIB, 0);
        glVertexAttribBinding(GLFONS_TCOORD_ATTRIB, 0);
        glVertexAttribBinding(GLFONS_COLOR_ATTRIB, 0);

        glVertexArrayVertexBuffer(gl->glfons_vaoID, 0, gl->glfons_vboID, 0, sizeof(FONSvert));
    }

    if (Divide::Config::ENABLE_GPU_VALIDATION) {
        glObjectLabel(GL_BUFFER,
                      gl->glfons_vaoID,
                      -1,
                      Divide::Util::StringFormat("DVD_FONT_VAO_%d", gl->glfons_vaoID).c_str());

        glObjectLabel(GL_BUFFER,
                        gl->glfons_vboID,
                        -1,
                        Divide::Util::StringFormat("DVD_FONT_VB_%d", gl->glfons_vboID).c_str());
    }

    if (!gl->tex || !gl->glfons_vaoID || !gl->glfons_vboID) {
        return 0;
    }

    gl->width = width;
    gl->height = height;

    glTextureStorage2D(gl->tex, 1, GL_R8, gl->width, gl->height);
    glTextureParameteri(gl->tex, GL_TEXTURE_MIN_FILTER, Divide::to_base(GL_LINEAR));

    return 1;
}

static int glfons__renderResize(void* userPtr, int width, int height)
{
	// Reuse create to resize too.
	return glfons__renderCreate(userPtr, width, height);
}

static void glfons__renderUpdate(void* userPtr, int* rect, const unsigned char* data)
{
	const GLFONScontext* gl = (GLFONScontext*)userPtr;

    if (gl->tex != 0) {
        const int w = rect[2] - rect[0];
        const int h = rect[3] - rect[1];
        Divide::GL_API::GetStateTracker()->setPixelUnpackAlignment(1, gl->width, rect[1], rect[0]);
        glTextureSubImage2D(gl->tex, 0, rect[0], rect[1], w, h, GL_RED, GL_UNSIGNED_BYTE, (Divide::bufferPtr)data);
    }
}

static void glfons__renderDraw(void* userPtr, const FONSvert* verts, int nverts)
{
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    if (gl->tex == 0 || gl->glfons_vaoID == 0)
        return;

    static size_t writeOffsetBytes = 0;
    static size_t prevWriteOffsetBytes = 0;

    const size_t dataSize = nverts * sizeof(FONSvert);

    // Not enough space left in the buffer. Start from the beginning
    if (prevWriteOffsetBytes > writeOffsetBytes) {
        g_lockManager->wait(true);
    }

    if (writeOffsetBytes + dataSize > GLFONS_VB_BUFFER_SIZE) {
        prevWriteOffsetBytes = writeOffsetBytes;
        writeOffsetBytes = 0;
    }

    { //Prep
        if (Divide::GL_API::GetStateTracker()->bindTexture(0, Divide::TextureType::TEXTURE_2D, gl->tex) == Divide::GLStateTracker::BindResult::FAILED) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
        if (Divide::GL_API::GetStateTracker()->setActiveVAO(gl->glfons_vaoID) == Divide::GLStateTracker::BindResult::FAILED) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
    }

    { //Update
        memcpy((Divide::Byte*)gl->glfons_vboData + writeOffsetBytes, verts, dataSize);
    }
    { //Draw
        const GLint startIndex = (GLint)(writeOffsetBytes / sizeof(FONSvert));
        glDrawArrays(GL_TRIANGLES, startIndex, nverts);
    }
    { //Lock
        prevWriteOffsetBytes = writeOffsetBytes;
        writeOffsetBytes = (writeOffsetBytes + dataSize) % GLFONS_VB_BUFFER_SIZE;
        if (prevWriteOffsetBytes > writeOffsetBytes) {
            g_lockManager->lock();
        }
    }
}

static void glfons__renderDelete(void* userPtr) {
    struct GLFONScontext* gl = (struct GLFONScontext*)userPtr;
    if (gl->tex)
        Divide::GL_API::DeleteTextures(1, &gl->tex, Divide::TextureType::TEXTURE_2D);
    if (gl->glfons_vaoID)
        Divide::GL_API::DeleteVAOs(1, &gl->glfons_vaoID);
    gl->tex = 0;
    gl->glfons_vaoID = 0;
    Divide::GLUtil::freeBuffer(gl->glfons_vboID, gl->glfons_vboData);
    Divide::MemoryManager::SAFE_DELETE(g_lockManager);
    free(gl);
}

FONS_DEF FONScontext* glfonsCreate(int width, int height, int flags)
{
	FONSparams params;

    GLFONScontext* gl = (GLFONScontext*)malloc(sizeof(GLFONScontext));
	if (gl == nullptr) goto error;
	memset(gl, 0, sizeof(GLFONScontext));

    memset(&params, 0, sizeof params);
    params.width = width;
    params.height = height;
	params.flags = (unsigned char)flags;
    params.renderCreate = glfons__renderCreate;
	params.renderResize = glfons__renderResize;
    params.renderUpdate = glfons__renderUpdate;
    params.renderDraw = glfons__renderDraw;
    params.renderDelete = glfons__renderDelete;
    params.userPtr = gl;

    return fonsCreateInternal(&params);

error:
	if (gl != nullptr) free(gl);
	return nullptr;
}

FONS_DEF void glfonsDelete(FONScontext* ctx)
{
    fonsDeleteInternal(ctx);
}

FONS_DEF unsigned int glfonsRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	return r | g << 8 | b << 16 | a << 24;
}

#endif // GLFONTSTASH_IMPLEMENTATION
