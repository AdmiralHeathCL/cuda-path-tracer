#ifndef QUALIFIERSH
#define QUALIFIERSH

#ifdef __CUDACC__
#define RT_HD __host__ __device__
#else
#define RT_HD
#endif

#endif
