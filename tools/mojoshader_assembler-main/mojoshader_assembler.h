/**
 * MojoShader; generate shader programs from bytecode of compiled
 *  Direct3D shaders.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_MOJOSHADER_ASSEMBLER_H_
#define _INCL_MOJOSHADER_ASSEMBLER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <mojoshader.h>

/* Preprocessor interface... */

/*
 * Structure used to pass predefined macros. Maps to D3DXMACRO.
 *  You can have macro arguments: set identifier to "a(b, c)" or whatever.
 */
typedef struct MOJOSHADER_preprocessorDefine
{
    const char *identifier;
    const char *definition;
} MOJOSHADER_preprocessorDefine;

/*
 * Used with the MOJOSHADER_includeOpen callback. Maps to D3DXINCLUDE_TYPE.
 */
typedef enum
{
    MOJOSHADER_INCLUDETYPE_LOCAL,   /* local header: #include "blah.h" */
    MOJOSHADER_INCLUDETYPE_SYSTEM   /* system header: #include <blah.h> */
} MOJOSHADER_includeType;


/*
 * Structure used to return data from preprocessing of a shader...
 */
/* !!! FIXME: most of these ints should be unsigned. */
typedef struct MOJOSHADER_preprocessData
{
    /*
     * The number of elements pointed to by (errors).
     */
    int error_count;

    /*
     * (error_count) elements of data that specify errors that were generated
     *  by parsing this shader.
     * This can be NULL if there were no errors or if (error_count) is zero.
     */
    MOJOSHADER_error *errors;

    /*
     * Bytes of output from preprocessing. This is a UTF-8 string. We
     *  guarantee it to be NULL-terminated. Will be NULL on error.
     */
    const char *output;

    /*
     * Byte count for output, not counting any null terminator.
     *  Will be 0 on error.
     */
    int output_len;

    /*
     * This is the malloc implementation you passed to MOJOSHADER_parse().
     */
    MOJOSHADER_malloc malloc;

    /*
     * This is the free implementation you passed to MOJOSHADER_parse().
     */
    MOJOSHADER_free free;

    /*
     * This is the pointer you passed as opaque data for your allocator.
     */
    void *malloc_data;
} MOJOSHADER_preprocessData;


/*
 * This callback allows an app to handle #include statements for the
 *  preprocessor. When the preprocessor sees an #include, it will call this
 *  function to obtain the contents of the requested file. This is optional;
 *  the preprocessor will open files directly if no callback is supplied, but
 *  this allows an app to retrieve data from something other than the
 *  traditional filesystem (for example, headers packed in a .zip file or
 *  headers generated on-the-fly).
 *
 * This function maps to ID3DXInclude::Open()
 *
 * (inctype) specifies the type of header we wish to include.
 * (fname) specifies the name of the file specified on the #include line.
 * (parent) is a string of the entire source file containing the include, in
 *  its original, not-yet-preprocessed state. Note that this is just the
 *  contents of the specific file, not all source code that the preprocessor
 *  has seen through other includes, etc.
 * (outdata) will be set by the callback to a pointer to the included file's
 *  contents. The callback is responsible for allocating this however they
 *  see fit (we provide allocator functions, but you may ignore them). This
 *  pointer must remain valid until the includeClose callback runs. This
 *  string does not need to be NULL-terminated.
 * (outbytes) will be set by the callback to the number of bytes pointed to
 *  by (outdata).
 * (m),(f), and (d) are the allocator details that the application passed to
 *  MojoShader. If these were NULL, MojoShader may have replaced them with its
 *  own internal allocators.
 *
 * The callback returns zero on error, non-zero on success.
 *
 * If you supply an includeOpen callback, you must supply includeClose, too.
 */
typedef int (MOJOSHADERCALL *MOJOSHADER_includeOpen)(MOJOSHADER_includeType inctype,
                            const char *fname, const char *parent,
                            const char **outdata, unsigned int *outbytes,
                            MOJOSHADER_malloc m, MOJOSHADER_free f, void *d);

/*
 * This callback allows an app to clean up the results of a previous
 *  includeOpen callback.
 *
 * This function maps to ID3DXInclude::Close()
 *
 * (data) is the data that was returned from a previous call to includeOpen.
 *  It is now safe to deallocate this data.
 * (m),(f), and (d) are the same allocator details that were passed to your
 *  includeOpen callback.
 *
 * If you supply an includeClose callback, you must supply includeOpen, too.
 */
typedef void (MOJOSHADERCALL *MOJOSHADER_includeClose)(const char *data,
                            MOJOSHADER_malloc m, MOJOSHADER_free f, void *d);


/*
 * This function is optional. Even if you are dealing with shader source
 *  code, you don't need to explicitly use the preprocessor, as the compiler
 *  and assembler will use it behind the scenes. In fact, you probably never
 *  need this function unless you are debugging a custom tool (or debugging
 *  MojoShader itself).
 *
 * Preprocessing roughly follows the syntax of an ANSI C preprocessor, as
 *  Microsoft's Direct3D assembler and HLSL compiler use this syntax. Please
 *  note that we try to match the output you'd get from Direct3D's
 *  preprocessor, which has some quirks if you're expecting output that matches
 *  a generic C preprocessor.
 *
 * This function maps to D3DXPreprocessShader().
 *
 * (filename) is a NULL-terminated UTF-8 filename. It can be NULL. We do not
 *  actually access this file, as we obtain our data from (source). This
 *  string is copied when we need to report errors while processing (source),
 *  as opposed to errors in a file referenced via the #include directive in
 *  (source). If this is NULL, then errors will report the filename as NULL,
 *  too.
 *
 * (source) is an string of UTF-8 text to preprocess. It does not need to be
 *  NULL-terminated.
 *
 * (sourcelen) is the length of the string pointed to by (source), in bytes.
 *
 * (defines) points to (define_count) preprocessor definitions, and can be
 *  NULL. These are treated by the preprocessor as if the source code started
 *  with one #define for each entry you pass in here.
 *
 * (include_open) and (include_close) let the app control the preprocessor's
 *  behaviour for #include statements. Both are optional and can be NULL, but
 *  both must be specified if either is specified.
 *
 * This will return a MOJOSHADER_preprocessorData. You should pass this
 *  return value to MOJOSHADER_freePreprocessData() when you are done with
 *  it.
 *
 * This function will never return NULL, even if the system is completely
 *  out of memory upon entry (in which case, this function returns a static
 *  MOJOSHADER_preprocessData object, which is still safe to pass to
 *  MOJOSHADER_freePreprocessData()).
 *
 * As preprocessing requires some memory to be allocated, you may provide a
 *  custom allocator to this function, which will be used to allocate/free
 *  memory. They function just like malloc() and free(). We do not use
 *  realloc(). If you don't care, pass NULL in for the allocator functions.
 *  If your allocator needs instance-specific data, you may supply it with the
 *  (d) parameter. This pointer is passed as-is to your (m) and (f) functions.
 *
 * This function is thread safe, so long as the various callback functions
 *  are, too, and that the parameters remains intact for the duration of the
 *  call. This allows you to preprocess several shaders on separate CPU cores
 *  at the same time.
 */
DECLSPEC const MOJOSHADER_preprocessData *MOJOSHADER_preprocess(const char *filename,
                             const char *source, unsigned int sourcelen,
                             const MOJOSHADER_preprocessorDefine *defines,
                             unsigned int define_count,
                             MOJOSHADER_includeOpen include_open,
                             MOJOSHADER_includeClose include_close,
                             MOJOSHADER_malloc m, MOJOSHADER_free f, void *d);


/*
 * Call this to dispose of preprocessing results when you are done with them.
 *  This will call the MOJOSHADER_free function you provided to
 *  MOJOSHADER_preprocess() multiple times, if you provided one.
 *  Passing a NULL here is a safe no-op.
 *
 * This function is thread safe, so long as any allocator you passed into
 *  MOJOSHADER_preprocess() is, too.
 */
DECLSPEC void MOJOSHADER_freePreprocessData(const MOJOSHADER_preprocessData *data);


/* Assembler interface... */

/*
 * This function is optional. Use this to convert Direct3D shader assembly
 *  language into bytecode, which can be handled by MOJOSHADER_parse().
 *
 * (filename) is a NULL-terminated UTF-8 filename. It can be NULL. We do not
 *  actually access this file, as we obtain our data from (source). This
 *  string is copied when we need to report errors while processing (source),
 *  as opposed to errors in a file referenced via the #include directive in
 *  (source). If this is NULL, then errors will report the filename as NULL,
 *  too.
 *
 * (source) is an UTF-8 string of valid Direct3D shader assembly source code.
 *  It does not need to be NULL-terminated.
 *
 * (sourcelen) is the length of the string pointed to by (source), in bytes.
 *
 * (comments) points to (comment_count) NULL-terminated UTF-8 strings, and
 *  can be NULL. These strings are inserted as comments in the bytecode.
 *
 * (symbols) points to (symbol_count) symbol structs, and can be NULL. These
 *  become a CTAB field in the bytecode. This is optional, but
 *  MOJOSHADER_parse() needs CTAB data for all arrays used in a program, or
 *  relative addressing will not be permitted, so you'll want to at least
 *  provide symbol information for those. The symbol data is 100% trusted
 *  at this time; it will not be checked to see if it matches what was
 *  assembled in any way whatsoever.
 *
 * (defines) points to (define_count) preprocessor definitions, and can be
 *  NULL. These are treated by the preprocessor as if the source code started
 *  with one #define for each entry you pass in here.
 *
 * (include_open) and (include_close) let the app control the preprocessor's
 *  behaviour for #include statements. Both are optional and can be NULL, but
 *  both must be specified if either is specified.
 *
 * This will return a MOJOSHADER_parseData, like MOJOSHADER_parse() would,
 *  except the profile will be MOJOSHADER_PROFILE_BYTECODE and the output
 *  will be the assembled bytecode instead of some other language. This output
 *  can be pushed back through MOJOSHADER_parseData() with a different profile.
 *
 * This function will never return NULL, even if the system is completely
 *  out of memory upon entry (in which case, this function returns a static
 *  MOJOSHADER_parseData object, which is still safe to pass to
 *  MOJOSHADER_freeParseData()).
 *
 * As assembling requires some memory to be allocated, you may provide a
 *  custom allocator to this function, which will be used to allocate/free
 *  memory. They function just like malloc() and free(). We do not use
 *  realloc(). If you don't care, pass NULL in for the allocator functions.
 *  If your allocator needs instance-specific data, you may supply it with the
 *  (d) parameter. This pointer is passed as-is to your (m) and (f) functions.
 *
 * This function is thread safe, so long as the various callback functions
 *  are, too, and that the parameters remains intact for the duration of the
 *  call. This allows you to assemble several shaders on separate CPU cores
 *  at the same time.
 */
DECLSPEC const MOJOSHADER_parseData *MOJOSHADER_assemble(const char *filename,
                             const char *source, unsigned int sourcelen,
                             const char **comments, unsigned int comment_count,
                             const MOJOSHADER_symbol *symbols,
                             unsigned int symbol_count,
                             const MOJOSHADER_preprocessorDefine *defines,
                             unsigned int define_count,
                             MOJOSHADER_includeOpen include_open,
                             MOJOSHADER_includeClose include_close,
                             MOJOSHADER_malloc m, MOJOSHADER_free f, void *d);

#ifdef __cplusplus
}
#endif

#endif  /* include-once blocker. */

/* end of mojoshader_asm.h ... */

