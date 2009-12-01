/*
Copyright (c) 2009, Roger Light <roger@atchoo.org>
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

