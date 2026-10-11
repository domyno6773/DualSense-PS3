/*
 * DS45PAD - minimal C runtime.
 *
 * The PRX is linked with -nodefaultlibs so it stays small and pulls nothing
 * unexpected into the VSH process. Only the handful of functions the plugin
 * actually uses live here, plus the ones GCC may emit on its own for struct
 * copies and initialisers.
 */

#include <stddef.h>

void *memset(void *dst, int c, size_t n)
{
	unsigned char *p = (unsigned char *)dst;

	while (n--)
		*p++ = (unsigned char)c;

	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	while (n--)
		*d++ = *s++;

	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;

	if (d == s || n == 0)
		return dst;

	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}

	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = (const unsigned char *)a;
	const unsigned char *q = (const unsigned char *)b;

	while (n--) {
		if (*p != *q)
			return (int)*p - (int)*q;
		p++;
		q++;
	}

	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;

	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}

	return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	if (n == 0)
		return 0;

	while (--n && *a && *a == *b) {
		a++;
		b++;
	}

	return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

char *strchr(const char *s, int c)
{
	while (*s) {
		if (*s == (char)c)
			return (char *)s;
		s++;
	}

	return (c == 0) ? (char *)s : NULL;
}
