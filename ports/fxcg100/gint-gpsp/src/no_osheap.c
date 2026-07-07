#include <gint/bfile.h>
#include <gint/kmalloc.h>

#include <stddef.h>
#include <stdint.h>

static void *null_malloc(size_t size, void *data)
{
	(void)size;
	(void)data;
	return NULL;
}

static void *null_realloc(void *ptr, size_t size, void *data)
{
	(void)ptr;
	(void)size;
	(void)data;
	return NULL;
}

static void null_free(void *ptr, void *data)
{
	(void)ptr;
	(void)data;
}

kmalloc_arena_t kmalloc_arena_osheap = {
	.malloc      = null_malloc,
	.realloc     = null_realloc,
	.free        = null_free,
	.malloc_max  = NULL,
	.name        = "_os",
	.start       = NULL,
	.end         = NULL,
	.data        = NULL,
	.is_default  = 1,
	.stats       = { 0 },
};

void *__malloc(size_t size)
{
	(void)size;
	return NULL;
}

void *__realloc(void *ptr, size_t size)
{
	(void)ptr;
	(void)size;
	return NULL;
}

void *__calloc(size_t nmemb, size_t size)
{
	(void)nmemb;
	(void)size;
	return NULL;
}

void __free(void *ptr)
{
	(void)ptr;
}

/* fx-CG100 links no OS filesystem, so BFile_* are stubbed and storage goes
 * through hardcoded firmware addresses instead. fx-CG50 uses gint's real
 * BFile_* (Fugue), so the stubs must NOT shadow them there. */
#ifndef CGBA_FXCG50
int BFile_Remove(const uint16_t *path) { (void)path; return -1; }
int BFile_Rename(const uint16_t *oldpath, const uint16_t *newpath)
{
	(void)oldpath;
	(void)newpath;
	return -1;
}
int BFile_Create(const uint16_t *path, int type, int *size)
{
	(void)path;
	(void)type;
	(void)size;
	return -1;
}
int BFile_Open(const uint16_t *path, int mode)
{
	(void)path;
	(void)mode;
	return -1;
}
int BFile_Close(int fd) { (void)fd; return -1; }
int BFile_Size(int fd) { (void)fd; return -1; }
int BFile_Seek(int fd, int offset)
{
	(void)fd;
	(void)offset;
	return -1;
}
int BFile_GetPos(int fd) { (void)fd; return -1; }
int BFile_Write(int fd, const void *data, int size)
{
	(void)fd;
	(void)data;
	(void)size;
	return -1;
}
int BFile_Read(int fd, void *data, int size, int whence)
{
	(void)fd;
	(void)data;
	(void)size;
	(void)whence;
	return -1;
}
int BFile_FindFirst(const uint16_t *pattern, int *shandle,
	uint16_t *foundfile, struct BFile_FileInfo *fileinfo)
{
	(void)pattern;
	(void)shandle;
	(void)foundfile;
	(void)fileinfo;
	return -1;
}
int BFile_FindNext(int shandle, uint16_t *foundfile,
	struct BFile_FileInfo *fileinfo)
{
	(void)shandle;
	(void)foundfile;
	(void)fileinfo;
	return -1;
}
int BFile_FindClose(int shandle) { (void)shandle; return -1; }
#endif /* !CGBA_FXCG50 */

int __Timer_Install(int id, void (*handler)(void), int delay)
{
	(void)id;
	(void)handler;
	(void)delay;
	return -1;
}
int __Timer_Start(int id) { (void)id; return -1; }
int __Timer_Stop(int id) { (void)id; return -1; }
int __Timer_Deinstall(int id) { (void)id; return -1; }

int __PutKeyCode(int key, int mode, int flags)
{
	(void)key;
	(void)mode;
	(void)flags;
	return 0;
}
int __GetKeyWait(int *col, int *row, int type, int time, int menu,
	uint16_t *key)
{
	(void)col;
	(void)row;
	(void)type;
	(void)time;
	(void)menu;
	(void)key;
	return 0;
}
void __ClearKeyBuffer(void) {}
void __ConfigureStatusArea(int mode) { (void)mode; }
void __SetQuitHandler(void (*handler)(void)) { (void)handler; }
void *__GetVRAMAddress(void) { return NULL; }
int __SpecialMatrixCodeProcessing(int *row, int *col)
{
	(void)row;
	(void)col;
	return 0;
}
void __VRAMBackup(void) {}
void __VRAMRestore(void) {}
void __PowerOff(int show_logo) { (void)show_logo; }
void __Reset(void) {}
