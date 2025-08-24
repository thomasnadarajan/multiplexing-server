#ifndef BYTESWAP_COMPAT_H
#define BYTESWAP_COMPAT_H

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#else
#include <byteswap.h>
#endif

#endif