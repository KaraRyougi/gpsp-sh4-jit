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

/* Both fx-CG50 and fx-CG100 link gint's real BFile_* implementations from
 * libgint's syscalls.S (which dispatches by OS version on fx-CG100 and via the
 * syscall table on fx-CG50), so BFile_* must not be stubbed here. */

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
