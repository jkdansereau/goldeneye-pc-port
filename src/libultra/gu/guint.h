/**************************************************************************
 *									  *
 *		 Copyright (C) 1994, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

#include "mbi.h"
#include "gu.h"

typedef union
{
	struct
	{
		unsigned int hi;
		unsigned int lo;
	} word;

	double	d;
} du;

#ifdef PORT
/* The {hi,lo} initializers of du constants are written in big-endian word
 * order (as on the N64). On a little-endian host the .d member would read
 * the two words swapped; re-pack them to recover the same double value. */
static inline double duD(const du u)
{
	union { unsigned long long b; double d; } t;
	t.b = ((unsigned long long)u.word.hi << 32) | (unsigned long long)u.word.lo;
	return t.d;
}
#define DVAL(x)	duD((x))
#else
#define DVAL(x)	((x).d)
#endif

typedef union
{
	unsigned int	i;
	float		f;
} fu;

#ifndef __GL_GL_H__

typedef	float	Matrix[4][4];

#endif

#define ROUND(d)	(int)(((d) >= 0.0) ? ((d) + 0.5) : ((d) - 0.5))
#define	ABS(d)		((d) > 0) ? (d) : -(d)

extern float	__libm_qnan_f;
