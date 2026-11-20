#ifndef CGBA_TEST_GINT_BFILE_H
#define CGBA_TEST_GINT_BFILE_H

#include <stdint.h>

enum {
	BFile_ReadOnly = 0,
};

struct BFile_FileInfo {
	uint16_t index;
	uint16_t type;
	uint32_t file_size;
	uint32_t size;
};

#endif
