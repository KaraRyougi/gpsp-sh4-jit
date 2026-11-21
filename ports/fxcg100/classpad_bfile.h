#ifndef CGBA_CLASSPAD_BFILE_H
#define CGBA_CLASSPAD_BFILE_H

/* ClassPad II file access with the calling convention the storage code already
 * uses on the fx-CG models.
 *
 * The ClassPad OS file API is POSIX-like (char paths, a file position, create
 * as an open flag) where Fugue on the fx-CG takes FONTCHARACTER paths and an
 * explicit offset per read. gint links the OS entry points under the BFile_*
 * names but keeps their prototypes private, so they are declared here. These
 * adapters let fxcg100_config.c and nor_rom.c keep one code path:
 *
 *   paths    "\\\\fls0\\NAME" (u16, fx-CG form) -> "\\fls0\\NAME" (char)
 *   errors   "no such file" -> -1 (what Fugue returns), anything else -> -2
 *
 * Like every OS file call these must run inside gint_world_switch(). */

#include <stdint.h>

#define BFile_ReadOnly  0x01
#define BFile_WriteOnly 0x02
#define BFile_ReadWrite 0x03

#define BFile_File      1
#define BFile_Folder    5

#define CP_BFILE_CREATE   0x04
#define CP_BFILE_SEEK_SET 0
#define CP_BFILE_ENOPATH  (-8)
#define CP_BFILE_EEXIST   (-9)
#define CP_BFILE_ENOENT   (-14)
#define CP_BFILE_PATH_MAX 160

struct BFile_FileInfo {
	uint32_t _1;
	uint16_t type;       /* 1 = file, 5 = directory */
	uint16_t _2;
	uint32_t file_size;
	uint32_t _3, _4;
};

struct cp_bfile_stat {
	uint32_t _1;
	uint32_t size;
	uint16_t dates[6];
};

extern int BFile_Open(const char *path, int flags);
extern int BFile_Read(int fd, void *buf, int size);
extern int BFile_Write(int fd, void const *buf, int size);
extern int BFile_Seek(int fd, int offset, int whence);
extern int BFile_Close(int fd);
extern int BFile_Remove(char const *path);
extern int BFile_Mkdir(char const *path);
extern int BFile_FStat(int fd, struct cp_bfile_stat *statbuf);
extern int BFile_FindFirst(uint16_t const *pattern, int *shandle,
	uint16_t *foundfile, struct BFile_FileInfo *fileinfo);
extern int BFile_FindNext(int shandle, uint16_t *foundfile,
	struct BFile_FileInfo *fileinfo);
extern int BFile_FindClose(int shandle);

/* The fx-CG device prefix is "\\\\fls0\\"; the ClassPad's has one backslash. */
static inline const uint16_t *cp_bfile_fc_path(const uint16_t *path)
{
	return (path && path[0] == '\\' && path[1] == '\\') ? path + 1 : path;
}

static inline int cp_bfile_char_path(char *dst, const uint16_t *path)
{
	unsigned i;

	path = cp_bfile_fc_path(path);
	if(!path)
		return 0;
	for(i = 0; path[i]; i++) {
		if(i + 1 >= CP_BFILE_PATH_MAX || path[i] >= 0x7f)
			return 0;
		dst[i] = (char)path[i];
	}
	dst[i] = 0;
	return i > 0;
}

static inline int cp_bfile_open_error(int rc)
{
	return (rc == CP_BFILE_ENOENT || rc == CP_BFILE_ENOPATH) ? -1 : -2;
}

static inline int cp_bfile_open(const uint16_t *path, int mode)
{
	char cpath[CP_BFILE_PATH_MAX];
	int fd;

	if(!cp_bfile_char_path(cpath, path))
		return -2;
	fd = BFile_Open(cpath, mode);
	return fd >= 0 ? fd : cp_bfile_open_error(fd);
}

static inline int cp_bfile_size(int fd)
{
	struct cp_bfile_stat st;
	int rc = BFile_FStat(fd, &st);

	return rc >= 0 ? (int)st.size : -2;
}

/* Fugue pre-sizes a file at creation; here creating it empty is enough. */
static inline int cp_bfile_create(const uint16_t *path, int type, int *size)
{
	char cpath[CP_BFILE_PATH_MAX];
	int fd;

	(void)size;
	if(!cp_bfile_char_path(cpath, path))
		return -2;
	if(type == BFile_Folder) {
		int rc = BFile_Mkdir(cpath);
		return (rc >= 0 || rc == CP_BFILE_EEXIST) ? 0 : -2;
	}
	fd = BFile_Open(cpath, BFile_WriteOnly | CP_BFILE_CREATE);
	if(fd < 0)
		return -2;
	return BFile_Close(fd) >= 0 ? 0 : -2;
}

static inline int cp_bfile_remove(const uint16_t *path)
{
	char cpath[CP_BFILE_PATH_MAX];

	if(!cp_bfile_char_path(cpath, path))
		return -2;
	return BFile_Remove(cpath) >= 0 ? 0 : -2;
}

static inline int cp_bfile_seek(int fd, int offset)
{
	return BFile_Seek(fd, offset, CP_BFILE_SEEK_SET) >= 0 ? 0 : -2;
}

/* Returns the number of bytes read, as Fugue does. A negative offset reads
 * from the current position. */
static inline int cp_bfile_read(int fd, void *dst, int size, int offset)
{
	if(offset >= 0 && cp_bfile_seek(fd, offset) < 0)
		return -2;
	return BFile_Read(fd, dst, size);
}

static inline int cp_bfile_write(int fd, const void *src, int size)
{
	return BFile_Write(fd, src, size);
}

static inline int cp_bfile_close(int fd)
{
	return BFile_Close(fd) >= 0 ? 0 : -2;
}

static inline int cp_bfile_find_first(const uint16_t *pattern, int *handle,
	uint16_t *found, struct BFile_FileInfo *info)
{
	return BFile_FindFirst(cp_bfile_fc_path(pattern), handle, found, info)
		>= 0 ? 0 : -1;
}

static inline int cp_bfile_find_next(int handle, uint16_t *found,
	struct BFile_FileInfo *info)
{
	return BFile_FindNext(handle, found, info) >= 0 ? 0 : -1;
}

static inline int cp_bfile_find_close(int handle)
{
	return BFile_FindClose(handle);
}

#endif /* CGBA_CLASSPAD_BFILE_H */
