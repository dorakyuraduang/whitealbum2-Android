#pragma once
#include <mojoshader.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef const char* (*wa2_mojo_source_transform)(const MOJOSHADER_parseData*,int* length,void* userdata);
// Optional source transform and read-only native program lookup. These extend
// the existing generic GL backend; source game CPU code is never decoded here.
int wa2_mojo_set_source_transform(wa2_mojo_source_transform,void* userdata);
unsigned int wa2_mojo_program_handle(const MOJOSHADER_glProgram*);
#ifdef __cplusplus
}
#endif
