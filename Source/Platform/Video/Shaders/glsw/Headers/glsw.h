#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct glswContextRec glswContext;

glswContext* glswGetCurrentContext();
void glswClearCurrentContext();
int  glswInit();
int  glswShutdown();
int  glswSetPath(const char* pathPrefix, const char* pathSuffix);
const char* glswGetShader(const char* effectKey);
const char* glswGetError();
int glswAddDirectiveToken(const char* token, const char* directive);

#ifdef __cplusplus
}
#endif
