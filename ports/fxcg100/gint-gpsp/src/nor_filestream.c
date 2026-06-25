/*
 * gpSP filestream backend for the fx-CG100 NOR ROM.
 *
 * gpSP pages 32KB ROM blocks on demand through filestream_*(); we back that with
 * cgba_nor_rom_read(), which gathers the bytes from the per-4KB-block direct NOR
 * address table via memcpy (memory-mapped flash) -- NOT the slow BFile read API.
 * Contiguous pages are direct-mapped (zero-copy) by load_gamepak_from_pages and
 * never reach this path; only fragmented pages page-fault here.
 *
 * These strong definitions override the weak stubs in fxcg100_gpsp_stubs.c.
 */

#include <streams/file_stream.h>
#include <stdio.h>   /* SEEK_SET / SEEK_CUR / SEEK_END */

#include "nor_rom.h"

struct RFILE {
	cgba_nor_rom *rom;
	int64_t offset;
};

static cgba_nor_rom *cgba_fs_rom;
static struct RFILE cgba_fs_handle;

void cgba_gpsp_filestream_bind(cgba_nor_rom *rom)
{
	cgba_fs_rom = rom;
}

RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
	(void)path;
	(void)mode;
	(void)hints;
	if(!cgba_fs_rom)
		return NULL;
	cgba_fs_handle.rom = cgba_fs_rom;
	cgba_fs_handle.offset = 0;
	return &cgba_fs_handle;
}

int64_t filestream_get_size(RFILE *stream)
{
	return stream ? (int64_t)stream->rom->size : -1;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
	if(!stream)
		return -1;
	switch(seek_position) {
	case SEEK_SET:
		stream->offset = offset;
		break;
	case SEEK_CUR:
		stream->offset += offset;
		break;
	case SEEK_END:
		stream->offset = (int64_t)stream->rom->size + offset;
		break;
	default:
		return -1;
	}
	return stream->offset;
}

int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
	uint32_t avail;
	uint32_t n;

	if(!stream || !data || len <= 0)
		return 0;
	if(stream->offset < 0 || (uint64_t)stream->offset >= stream->rom->size)
		return 0;
	avail = stream->rom->size - (uint32_t)stream->offset;
	n = (len < (int64_t)avail) ? (uint32_t)len : avail;
	cgba_nor_rom_read(stream->rom, data, (uint32_t)stream->offset, n);
	stream->offset += n;
	return (int64_t)n;
}

int filestream_close(RFILE *stream)
{
	(void)stream;
	return 0;
}
