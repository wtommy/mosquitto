/*
Copyright (c) 2009,2010, Roger Light <roger@atchoo.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of mosquitto nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <sqlite3.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <mqtt3.h>

struct _memlist {
	struct _memlist *next;
	void *ptr;
	size_t size;
};

#ifdef WITH_MEMORY_TRACKING
static struct _memlist *memlist = NULL;
static uint32_t memcount = 0;
#endif

void *mqtt3_calloc(size_t nmemb, size_t size)
{
#ifdef WITH_MEMORY_TRACKING
	void *mem = calloc(nmemb, size);
	struct _memlist *list = NULL;

	if(mem){
		list = malloc(sizeof(struct _memlist));
		list->ptr = mem;
		list->size = size;
		list->next = memlist;
		memlist = list;

		memcount += size;
	}

	return mem;
#else
	return calloc(nmemb, size);
#endif
}

void mqtt3_free(void *mem)
{
#ifdef WITH_MEMORY_TRACKING
	struct _memlist *list;
	struct _memlist *prev = NULL;

	if(mem){
		for(list=memlist; list; list=list->next){
			if(list->ptr == mem){
				free(mem);
				memcount -= list->size;
				if(prev){
					prev->next = list->next;
				}else{
					memlist = list->next;
				}
				free(list);
				break;
			}
			prev = list;
		}
	}
#else
	free(mem);
#endif
}

void *mqtt3_malloc(size_t size)
{
#ifdef WITH_MEMORY_TRACKING
	void *mem = malloc(size);
	struct _memlist *list = NULL;

	if(mem){
		list = malloc(sizeof(struct _memlist));
		list->ptr = mem;
		list->size = size;
		list->next = memlist;
		memlist = list;

		memcount += size;
	}

	return mem;
#else
	return malloc(size);
#endif
}

#ifdef WITH_MEMORY_TRACKING
uint32_t mqtt3_memory_used(void)
{
	return memcount + sqlite3_memory_used();
}
#endif

void *mqtt3_realloc(void *ptr, size_t size)
{
#ifdef WITH_MEMORY_TRACKING
	void *mem = NULL;
	struct _memlist *list = NULL;

	if(ptr){
		for(list=memlist; list; list=list->next){
			if(list->ptr == ptr){
				mem = realloc(ptr, size);
				list->ptr = mem;
				memcount += size-list->size;
				list->size = size;
				break;
			}
		}
	}else{
		list = malloc(sizeof(struct _memlist));
		mem = realloc(ptr, size);
		list->ptr = mem;
		list->size = size;
		list->next = memlist;
		memlist = list;
		memcount += size;
	}
	return mem;
#else
	return realloc(ptr, size);
#endif
}

char *mqtt3_strdup(const char *s)
{
#ifdef WITH_MEMORY_TRACKING
	char *str;
	struct _memlist *list = NULL;

	if(!s) return NULL;

	str = strdup(s);
	if(str){
		list = malloc(sizeof(struct _memlist));
		list->ptr = str;
		list->size = strlen(str);
		list->next = memlist;
		memlist = list;

		memcount += strlen(str);
	}
	return str;
#else
	return strdup(s);
#endif
}

