#include "GPU3D_TexcacheOpenGL.h"

namespace melonDS
{

static bool ClearErrors()
{
    for (int i = 0; i < 16; i++)
    {
        GLenum error = glGetError();
        if (error == GL_NO_ERROR)
            return true;
#ifdef GL_CONTEXT_LOST
        if (error == GL_CONTEXT_LOST)
            return false;
#endif
    }
    return false;
}

GLuint TexcacheOpenGLLoader::GenerateTexture(u32 width, u32 height, u32 layers)
{
    if (!ClearErrors())
        return 0;

    GLuint texarray;
    glGenTextures(1, &texarray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texarray);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (IsCompute)
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8UI, width, height, layers);
    else
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8UI, width, height, layers, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    if (glGetError() != GL_NO_ERROR)
    {
        glDeleteTextures(1, &texarray);
        return 0;
    }

    return texarray;
}

bool TexcacheOpenGLLoader::UploadTexture(GLuint handle, u32 width, u32 height,
                                         u32 layer, void* data)
{
    if (!handle)
        return false;
    if (!ClearErrors())
        return false;
    glBindTexture(GL_TEXTURE_2D_ARRAY, handle);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
        0, 0, 0, layer,
        width, height, 1,
        GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, data);
    return glGetError() == GL_NO_ERROR;
}

void TexcacheOpenGLLoader::DeleteTexture(GLuint handle)
{
    glDeleteTextures(1, &handle);
}

}
