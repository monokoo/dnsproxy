/*
 * Copyright 2014, Vietor Liu <vietor.liu at gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version. For full terms that can be
 * found in the LICENSE file.
 */

#include "dnsproxy.h"

static struct {
	unsigned int count;
	struct rbtree rb_name;
	struct rbtree rb_expire;
} g_cache;

static int name_search(const void* k, const struct rbnode* r)
{
	DOMAIN_CACHE *right;
	right = rbtree_entry(r, DOMAIN_CACHE, rb_name);
	return strcmp((const char*) k, right->domain);
}

static int name_compare(const struct rbnode* l, const struct rbnode* r)
{
	DOMAIN_CACHE *left, *right;
	left = rbtree_entry(l, DOMAIN_CACHE, rb_name);
	right = rbtree_entry(r, DOMAIN_CACHE, rb_name);
	return strcmp(left->domain, right->domain);
}

static int expire_compare(const struct rbnode* l, const struct rbnode* r)
{
	DOMAIN_CACHE *left, *right;
	left = rbtree_entry(l, DOMAIN_CACHE, rb_expire);
	right = rbtree_entry(r, DOMAIN_CACHE, rb_expire);
	return (int)(left->expire - right->expire);
}

static inline int is_space(char c)
{
	return c == '\t' || c == ' ' || c == '\n';
}

static char* skip_space(char* p)
{
	while(*p && is_space(*p))
		++p;
	return p;
}

static char* skip_to_space(char* p)
{
	while(*p && !is_space(*p))
		++p;
	return p;
}

void domain_cache_init(const char* hosts_file)
{
	FILE *fp;
	struct in_addr addr;
	DOMAIN_CACHE* cache;
	char line[8192];
	char *rear, *rlimit, *ip, *domain, *pos;

	g_cache.count = 0;
	rbtree_init(&g_cache.rb_name, name_search, name_compare);
	rbtree_init(&g_cache.rb_expire, NULL, expire_compare);

	if(hosts_file == NULL)
		return;

	memset(line, 0, sizeof(line));
	fp = fopen(hosts_file, "r");
	if(fp) {
		while(fgets(line, sizeof(line) - 1, fp)) {
			rlimit = line + strlen(line);

			ip = skip_space(line);
			if(is_space(*ip) || *ip == '#')
				continue;
			rear = skip_to_space(ip);
			if(!is_space(*rear))
				continue;
			*rear = '\0';
			addr.s_addr = inet_addr(ip);
			if(addr.s_addr == INADDR_NONE || addr.s_addr == INADDR_ANY)
				continue;

			while(rear + 1 < rlimit) {
				domain = skip_space(rear + 1);
				if(is_space(*domain))
					break;
				rear = skip_to_space(domain);
				if(*rear && is_space(*rear))
					*rear = '\0';

				pos = domain;
				while(*pos) {
					*pos = (char)tolower(*pos);
					++ pos;
				}

				cache = domain_cache_search(domain);
				if(cache == NULL)
					domain_cache_append(domain, rear - domain, 0, &addr);
			}
		}
		fclose(fp);
	}
}

DOMAIN_CACHE* domain_cache_search(char* domain)
{
	struct rbnode *node = rbtree_search(&g_cache.rb_name, domain);
	if(node == RBNODE_NULL)
		return NULL;
	return rbtree_entry(node, DOMAIN_CACHE, rb_name);
}

void domain_cache_append(char* domain, int dlen, unsigned int ttl, struct in_addr *addr)
{
	DOMAIN_CACHE *cache = (DOMAIN_CACHE*)calloc(1, sizeof(DOMAIN_CACHE) + dlen + 1);
	if(cache) {
		if(ttl > 0) {
			if(ttl > 3600)
				ttl = 3600;
			else if(ttl < 60)
				ttl = 60;
			cache->expire = time(NULL) + ttl;
		}
		memcpy(&cache->addr, addr, sizeof(struct in_addr));
		memcpy(cache->domain, domain, dlen);
		++g_cache.count;
		rbtree_insert(&g_cache.rb_name, &cache->rb_name);
		if(ttl > 0)
			rbtree_insert(&g_cache.rb_expire, &cache->rb_expire);
	}
}

void domain_cache_clean(time_t current)
{
	DOMAIN_CACHE* cache;
	struct rbnode *node;

	while(!rbtree_empty(&g_cache.rb_expire)) {
		node = rbtree_first(&g_cache.rb_expire);
		cache = rbtree_entry(node, DOMAIN_CACHE, rb_expire);
		if(cache->expire > current)
			break;
		--g_cache.count;
		rbtree_delete(&g_cache.rb_name, &cache->rb_name);
		rbtree_delete(&g_cache.rb_expire, &cache->rb_expire);
		free(cache);
	}
}
