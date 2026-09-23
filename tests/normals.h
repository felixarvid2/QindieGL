#pragma once

#include <stdlib.h>
#include <search.h>
#include <stdint.h>
#include <math.h>
#include <iostream>
#include <string>
#include <fstream>
#include <intrin.h>

#define MAX_VERTEXES 1000
#define MAX_INDEXES (6*MAX_VERTEXES)

typedef float vec_t;
typedef float vec3_t[3];
typedef float vec4_t[4];
typedef unsigned int uint;

#define DotProduct(x,y)			(x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c)	(c[0]=a[0]-b[0],c[1]=a[1]-b[1],c[2]=a[2]-b[2])
#define VectorAdd(a,b,c)		(c[0]=a[0]+b[0],c[1]=a[1]+b[1],c[2]=a[2]+b[2])
#define VectorCopy(a,b)			(b[0]=a[0],b[1]=a[1],b[2]=a[2])
#define VectorScale(v,s,o)      ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
#define VectorClear(a)			(a[0]=a[1]=a[2]=0)
#define VectorNegate(a,b)		(b[0]=-a[0],b[1]=-a[1],b[2]=-a[2])
#define VectorSet(v, x, y, z)	(v[0]=(x), v[1]=(y), v[2]=(z))
#define VectorMA( v, s, b, o )  ( ( o )[0] = ( v )[0] + ( b )[0] * ( s ),( o )[1] = ( v )[1] + ( b )[1] * ( s ),( o )[2] = ( v )[2] + ( b )[2] * ( s ) )

static inline void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross) {
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

static inline void VectorNormalize(vec3_t v)
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];

	if (length)
	{
#if 1
		_mm_store_ss(&ilength, _mm_rsqrt_ss(_mm_set_ss(length)));
#else
		ilength = 1 / sqrtf(length);
#endif
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}
}

struct vertexData_s
{
	float xyz[3];
	float normal[3];
	//float uv[2];
};

struct drawbuff_s
{
	int numVertexes;
	int numIndexes;
	struct vertexData_s vertexes[MAX_VERTEXES];
	unsigned short indexes[MAX_INDEXES];
};

typedef void (*calculate_normals)();
void calculate_normals_original();
void calculate_normals_fixed();
void calculate_normals_remix();

typedef struct drawbuff_s* (*getdrawbuff)();
struct drawbuff_s* getdrawbuff_original();
struct drawbuff_s* getdrawbuff_fixed();
struct drawbuff_s* getdrawbuff_remix();