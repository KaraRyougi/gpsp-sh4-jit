#ifndef CGBA_TEST_GINT_BFILE_H
#define CGBA_TEST_GINT_BFILE_H

#include <stdint.h>

enum {
	BFile_ReadOnly = 0,
};

struct BFile_FileInfo {
	uint32_t size;
};

#endif
