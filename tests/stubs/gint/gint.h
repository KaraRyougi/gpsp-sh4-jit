#ifndef CGBA_TEST_GINT_GINT_H
#define CGBA_TEST_GINT_GINT_H

#include <stdint.h>

typedef struct gint_call {
	void *function;
	uintptr_t args[4];
} gint_call_t;

#define CGBA_TEST_GINT_CALL1(function_, a1) \
	((gint_call_t){ (void *)(function_), { (uintptr_t)(a1) } })
#define CGBA_TEST_GINT_CALL2(function_, a1, a2) \
	((gint_call_t){ (void *)(function_), \
		{ (uintptr_t)(a1), (uintptr_t)(a2) } })
#define CGBA_TEST_GINT_CALL3(function_, a1, a2, a3) \
	((gint_call_t){ (void *)(function_), \
		{ (uintptr_t)(a1), (uintptr_t)(a2), (uintptr_t)(a3) } })
#define CGBA_TEST_GINT_CALL4(function_, a1, a2, a3, a4) \
	((gint_call_t){ (void *)(function_), \
		{ (uintptr_t)(a1), (uintptr_t)(a2), \
			(uintptr_t)(a3), (uintptr_t)(a4) } })
#define CGBA_TEST_GINT_SELECT(_1, _2, _3, _4, selected, ...) selected
#define GINT_CALL(function_, ...) \
	CGBA_TEST_GINT_SELECT(__VA_ARGS__, CGBA_TEST_GINT_CALL4, \
		CGBA_TEST_GINT_CALL3, CGBA_TEST_GINT_CALL2, \
		CGBA_TEST_GINT_CALL1)(function_, __VA_ARGS__)

int gint_world_switch(gint_call_t call);

#endif
