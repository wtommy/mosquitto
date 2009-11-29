#include <sqlite3.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mqtt3.h>

static uint32_t memcount = 0;

void *mqtt3_calloc(size_t nmemb, size_t size)
{
	void *mem = calloc(nmemb, size);
	/* FIXME
	if(mem){
		memcount += size;
	}
	*/
	return mem;
}

void mqtt3_free(void *mem)
{
	if(mem){
		free(mem);
		/* FIXME
		memcount -= 
		*/
	}
}

void *mqtt3_malloc(size_t size)
{
	void *mem = malloc(size);
	/* FIXME
	if(mem){
		memcount += size;
	}
	*/
	return mem;
}

uint32_t mqtt3_memory_used(void)
{
	return memcount + sqlite3_memory_used();
}

void *mqtt3_realloc(void *ptr, size_t size)
{
	void *mem = realloc(ptr, size);
	/* FIXME
	if(mem){
		memcount += size;
	}
	*/
	return mem;
}

char *mqtt3_strdup(const char *s)
{
	char *str = strdup(s);
	/* FIXME
	if(str){
		memcount += strlen(str);
	}
	*/
	return str;
}

