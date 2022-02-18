///////////////////////////////////////////////////////////////////////////////
// Written by Philip Rideout in April 2010
// Covered by the MIT License
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "Headers/bstrlib.h"
#include "Headers/glsw.h"

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma warning(disable:4996) // allow "fopen"
#endif

///////////////////////////////////////////////////////////////////////////////
// PRIVATE TYPES

typedef struct glswListRec
{
    bstring Key;
    bstring Value;
    struct glswListRec* Next;
} glswList;

struct glswContextRec
{
    bstring PathPrefix;
    bstring PathSuffix;
    bstring ErrorMessage;
    glswList* TokenMap;
    glswList* ShaderMap;
    glswList* LoadedEffects;
};

///////////////////////////////////////////////////////////////////////////////
// PRIVATE GLOBALS

thread_local glswContext* __glsw__Context = nullptr;

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS

static int __glsw__Alphanumeric(char c)
{
    return
        c >= 'A' && c <= 'Z' ||
        c >= 'a' && c <= 'z' ||
        c >= '0' && c <= '9' ||
        c == '_' || c == '.';
}

static void __glsw__FreeList(glswList* pNode)
{
    while (pNode)
    {
        glswList* pNext = pNode->Next;
        bdestroy(pNode->Key);
        bdestroy(pNode->Value);
        free(pNode);
        pNode = pNext;
    }
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

static int glswClear(glswContext* gc)
{
    __glsw__FreeList(gc->ShaderMap);
    __glsw__FreeList(gc->LoadedEffects);
    gc->ShaderMap = nullptr;
    gc->LoadedEffects = nullptr;
    return 0;
}

glswContext* glswGetCurrentContext()
{
    return __glsw__Context;
}

void glswSetCurrentContext(glswContext* gc) {
    __glsw__Context = gc;
}

void glswClearCurrentContext()
{
    glswClear(glswGetCurrentContext());
}

int glswInit()
{
    if (glswGetCurrentContext())
    {
        bdestroy(glswGetCurrentContext()->ErrorMessage);
        glswGetCurrentContext()->ErrorMessage = bfromcstr("Already initialized.");
        return 0;
    }

    glswSetCurrentContext((glswContext*) calloc(sizeof(glswContext), 1));

    return 1;
}

int glswShutdown()
{
    glswContext* gc = glswGetCurrentContext();

    if (!gc)
    {
        return 0;
    }

    bdestroy(gc->PathPrefix);
    bdestroy(gc->PathSuffix);
    bdestroy(gc->ErrorMessage);
    glswClear(gc);
    __glsw__FreeList(gc->TokenMap);
    gc->TokenMap = nullptr;
    free(gc);
    glswSetCurrentContext(nullptr);

    return 1;
}

int glswSetPath(const char* pathPrefix, const char* pathSuffix)
{
    glswContext* gc = glswGetCurrentContext();

    if (!gc)
    {
        return 0;
    }

    if (!gc->PathPrefix) {
        gc->PathPrefix = bfromcstr(pathPrefix);
    } else {
        const bstring prefix = bfromcstr(pathPrefix);
        if (bstrcmp(gc->PathPrefix, prefix) != 0) {
            bassign(gc->PathPrefix, prefix);
        }
        else {
            bdestroy(prefix);
        }
    }

    if (!gc->PathSuffix) {
        gc->PathSuffix = bfromcstr(pathSuffix);
    } else {
        const bstring suffix = bfromcstr(pathSuffix);
        if (bstrcmp(gc->PathSuffix, suffix) != 0) {
            bassign(gc->PathSuffix, suffix);
        }
        else {
            bdestroy(suffix);
        }
    }

    return 1;
}

const char* glswGetShader(const char* pEffectKey)
{
    glswContext* gc = glswGetCurrentContext();

    bstring effectKey = nullptr;
    glswList* closestMatch = nullptr;
    struct bstrList* tokens = nullptr;
    bstring effectName = nullptr;
    glswList* pLoadedEffect = nullptr;
    glswList* pShaderEntry = nullptr;
    bstring shaderKey = nullptr;

    if (!gc) {
        return nullptr;
    }

    // Extract the effect name from the effect key
    effectKey = bfromcstr(pEffectKey);
    tokens = bsplit(effectKey, '.');
    if (!tokens || !tokens->qty)
    {
        bdestroy(gc->ErrorMessage);
        gc->ErrorMessage = bformat("Malformed effect key key '%s'.", pEffectKey);
        bstrListDestroy(tokens);
        bdestroy(effectKey);
        return nullptr;
    }
    effectName = tokens->entry[0];

    // Check if we already loaded this effect file
    pLoadedEffect = gc->LoadedEffects;
    while (pLoadedEffect) {
        if (biseq(pLoadedEffect->Key, effectName) == 1)  {
            break;
        }
        pLoadedEffect = pLoadedEffect->Next;
    }

    // If we haven't loaded this file yet, load it in
    if (!pLoadedEffect)
    {
        bstring effectContents = nullptr;
        struct bstrList* lines = nullptr;
        int lineNo = 0;
        {
            FILE* fp = nullptr;
            bstring effectFile = nullptr;

            // Decorate the effect name to form the fullpath
            effectFile = bstrcpy(effectName);
            binsert(effectFile, 0, gc->PathPrefix, '?');
            bconcat(effectFile, gc->PathSuffix);

            // Attempt to open the file
            fp = fopen((const char*) effectFile->data, "rb");
            if (!fp)
            {
                bdestroy(gc->ErrorMessage);
                gc->ErrorMessage = bformat("Unable to open effect file '%s'.", effectFile->data);
                bdestroy(effectFile);
                bdestroy(effectKey);
                bstrListDestroy(tokens);
                return nullptr;
            }

            // Add a new entry to the front of gc->LoadedEffects
            {
                glswList* temp = gc->LoadedEffects;
                gc->LoadedEffects = (glswList*) calloc(sizeof(glswList), 1);
                gc->LoadedEffects->Key = bstrcpy(effectName);
                gc->LoadedEffects->Next = temp;
            }

            // Read in the effect file
            effectContents = bread_gl((bNread) fread, fp);
            fclose(fp);
            bdestroy(effectFile);
            effectFile = nullptr;
        }

        lines = bsplit(effectContents, '\n');
        bdestroy(effectContents);
        effectContents = nullptr;

        for (lineNo = 0; lineNo < lines->qty; lineNo++)
        {
            const bstring line = lines->entry[lineNo];

            // If the line starts with "--", then it marks a new section
            if (blength(line) >= 2 && line->data[0] == '-' && line->data[1] == '-')
            {
                // Find the first character in [A-Za-z0-9_].
                int colNo = 0;
                for (colNo = 2; colNo < blength(line); colNo++)
                {
                    const char c = line->data[colNo];
                    if (__glsw__Alphanumeric(c))
                    {
                        break;
                    }
                }

                // If there's no alphanumeric character,
                // then this marks the start of a new comment block.
                if (colNo >= blength(line))
                {
                    bdestroy(shaderKey);
                    shaderKey = nullptr;
                }
                else
                {
                    // Keep reading until a non-alphanumeric character is found.
                    int endCol = 0;
                    for (endCol = colNo; endCol < blength(line); endCol++)
                    {
                        const char c = line->data[endCol];
                        if (!__glsw__Alphanumeric(c))
                        {
                            break;
                        }
                    }

                    bdestroy(shaderKey);
                    shaderKey = bmidstr(line, colNo, endCol - colNo);

                    // Add a new entry to the shader map.
                    {
                        glswList* temp = gc->ShaderMap;
                        gc->ShaderMap = (glswList*) calloc(sizeof(glswList), 1);
                        gc->ShaderMap->Key = bstrcpy(shaderKey);
                        gc->ShaderMap->Next = temp;
                        gc->ShaderMap->Value = bformat("#pragma glsw_entry_break\n");

                        binsertch(gc->ShaderMap->Key, 0, 1, '.');
                        binsert(gc->ShaderMap->Key, 0, effectName, '?');
                    }

                    // Check for a version mapping.
                    if (gc->TokenMap)
                    {
                        tokens = bsplit(shaderKey, '.');
                        assert(tokens);

                        glswList* pTokenMapping = gc->TokenMap;

                        while (pTokenMapping)
                        {
                            bstring directive = nullptr;
                            int tokenIndex = 0;

                            // An empty key in the token mapping means "always prepend this directive".
                            // The effect name itself is also checked against the token mapping.
                            if (blength(pTokenMapping->Key) == 0 ||
                                biseq(pTokenMapping->Key, effectName) == 1)
                            {
                                directive = pTokenMapping->Value;
                                binsert(gc->ShaderMap->Value, 0, directive, '?');
                            }

                            // Check all tokens in the current section divider for a mapped token.
                            for (tokenIndex = 0; tokenIndex < tokens->qty && !directive; tokenIndex++)
                            {
                                const bstring token = tokens->entry[tokenIndex];
                                if (biseq(pTokenMapping->Key, token) == 1)
                                {
                                    directive = pTokenMapping->Value;
                                    binsert(gc->ShaderMap->Value, 0, directive, '?');
                                }
                            }

                            pTokenMapping = pTokenMapping->Next;
                        }

                        bstrListDestroy(tokens);
                        tokens = nullptr;
                    }
                }
                continue;
            }
            if (shaderKey)
            {
                bconcat(gc->ShaderMap->Value, line);
                bconchar(gc->ShaderMap->Value, '\n');
            }
        }

        // Cleanup
        bstrListDestroy(lines);
        lines = nullptr;
        bdestroy(shaderKey);
        shaderKey = nullptr;
    }

    // Find the longest matching shader key
    pShaderEntry = gc->ShaderMap;

    while (pShaderEntry)
    {
        if (binstr(effectKey, 0, pShaderEntry->Key) == 0 &&
            (!closestMatch || blength(pShaderEntry->Key) > blength(closestMatch->Key)))
        {
            closestMatch = pShaderEntry;
        }

        pShaderEntry = pShaderEntry->Next;
    }

    bstrListDestroy(tokens);
    tokens = nullptr;
    bdestroy(effectKey);
    effectKey = nullptr;

    if (!closestMatch)
    {
        bdestroy(gc->ErrorMessage);
        gc->ErrorMessage = bformat("Could not find shader with key '%s'.", pEffectKey);
        return nullptr;
    }

    return (const char*) closestMatch->Value->data;
}

const char* glswGetError()
{
    glswContext* gc = glswGetCurrentContext();

    if (!gc)
    {
        return "The glsw API has not been initialized.";
    }

    return (const char*) (gc->ErrorMessage ? gc->ErrorMessage->data : nullptr);
}

int glswAddDirectiveToken(const char* token, const char* directive)
{
    glswContext* gc = glswGetCurrentContext();

    if (!gc)
    {
        return 0;
    }

    glswList* temp = gc->TokenMap;
    gc->TokenMap = (glswList*) calloc(sizeof(glswContext), 1);
    if (!gc->TokenMap) {
        return 0;
    }
    gc->TokenMap->Key = bfromcstr(token);
    gc->TokenMap->Value = bfromcstr(directive);
    gc->TokenMap->Next = temp;

    bconchar(gc->TokenMap->Value, '\n');

    return 1;
}
#ifdef __cplusplus
}
#endif