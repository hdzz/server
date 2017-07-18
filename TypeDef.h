#ifndef __TYPEDEF_H__
#define __TYPEDEF_H__

typedef int INT32;
typedef unsigned int UINT32;
typedef long long INT64;
typedef unsigned long long UINT64;

#define ASSERT_RETURN_VALUE(exp, ret) \
	if (!exp) \
	{\
		return ret;\
	}

#endif
