/*
 * Copyright (c) 1997-2002 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgment:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * $Id: info_ldap.c,v 1.1.1.2 2002/07/15 19:42:37 zarzycki Exp $
 *
 */


/*
 * Get info from LDAP (Lightweight Directory Access Protocol)
 * LDAP Home Page: http://www.umich.edu/~rsug/ldap/
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>


/*
 * MACROS:
 */
#define AMD_LDAP_TYPE		"ldap"
/* Time to live for an LDAP cached in an mnt_map */
#define AMD_LDAP_TTL		3600
#define AMD_LDAP_RETRIES	5
#define AMD_LDAP_HOST		"ldap"
#ifndef LDAP_PORT
# define LDAP_PORT		389
#endif /* LDAP_PORT */

/* How timestamps are searched */
#define AMD_LDAP_TSFILTER "(&(objectClass=amdmapTimestamp)(amdmapName=%s))"
/* How maps are searched */
#define AMD_LDAP_FILTER "(&(objectClass=amdmap)(amdmapName=%s)(amdmapKey=%s))"
/* How timestamps are stored */
#define AMD_LDAP_TSATTR "amdmaptimestamp"
/* How maps are stored */
#define AMD_LDAP_ATTR "amdmapvalue"

/*
 * TYPEDEFS:
 */
typedef struct ald_ent ALD;
typedef struct cr_ent CR;
typedef struct he_ent HE_ENT;

/*
 * STRUCTURES:
 */
struct ald_ent {
  LDAP *ldap;
  HE_ENT *hostent;
  CR *credentials;
  time_t timestamp;
};

struct cr_ent {
  char *who;
  char *pw;
  int method;
};

struct he_ent {
  char *host;
  int port;
  struct he_ent *next;
};

/*
 * FORWARD DECLARATIONS:
 */
static int amu_ldap_rebind(ALD *a);
static int get_ldap_timestamp(ALD *a, char *map, time_t *ts);


/*
 * FUNCTIONS:
 */

static void
he_free(HE_ENT *h)
{
  XFREE(h->host);
  if (h->next != NULL)
    he_free(h->next);
  XFREE(h);
}


static HE_ENT *
string2he(char *s_orig)
{
  char *c, *p;
  char *s;
  HE_ENT *new, *old = NULL;

  if (NULL == s_orig || NULL == (s = strdup(s_orig)))
    return NULL;
  for (p = s; p; p = strchr(p, ',')) {
    if (old != NULL) {
      new = ALLOC(HE_ENT);
      old->next = new;
      old = new;
    } else {
      old = ALLOC(HE_ENT);
      old->next = NULL;
    }
    c = strchr(p, ':');
    if (c) {            /* Host and port */
      *c++ = '\0';
      old->host = strdup(p);
      old->port = atoi(c);
    } else
      old->host = strdup(p);

  }
  XFREE(s);
  return (old);
}


static void
cr_free(CR *c)
{
  XFREE(c->who);
  XFREE(c->pw);
  XFREE(c);
}


static void
ald_free(ALD *a)
{
  he_free(a->hostent);
  cr_free(a->credentials);
  if (a->ldap != NULL)
    ldap_unbind(a->ldap);
  XFREE(a);
}


int
amu_ldap_init(mnt_map *m, char *map, time_t *ts)
{
  ALD *aldh;
  CR *creds;

  dlog("-> amu_ldap_init: map <%s>\n", map);

  /*
   * XXX: by checking that map_type must be defined, aren't we
   * excluding the possibility of automatic searches through all
   * map types?
   */
  if (!gopt.map_type || !STREQ(gopt.map_type, AMD_LDAP_TYPE)) {
    plog(XLOG_WARNING, "amu_ldap_init called with map_type <%s>\n",
	 (gopt.map_type ? gopt.map_type : "null"));
  } else {
    dlog("Map %s is ldap\n", map);
  }

  aldh = ALLOC(ALD);
  creds = ALLOC(CR);
  aldh->ldap = NULL ;
  aldh->hostent = string2he(gopt.ldap_hostports);
  if (aldh->hostent == NULL) {
    plog(XLOG_USER, "Unable to parse hostport %s for ldap map %s",
	 gopt.ldap_hostports, map);
    return (ENOENT);
  }
  creds->who = "";
  creds->pw = "";
  creds->method = LDAP_AUTH_SIMPLE;
  aldh->credentials = creds;
  aldh->timestamp = 0;
  aldh->ldap = NULL;
  dlog("Trying for %s:%d\n", aldh->hostent->host, aldh->hostent->port);
  if (amu_ldap_rebind(aldh)) {
    ald_free(aldh);
    return (ENOENT);
  }
  m->map_data = (void *) aldh;
  dlog("Bound to %s:%d\n", aldh->hostent->host, aldh->hostent->port);
  if (get_ldap_timestamp(aldh, map, ts))
    return (ENOENT);
  dlog("Got timestamp for map %s: %ld\n", map, *ts);

  return (0);
}


static int
amu_ldap_rebind(ALD *a)
{
  LDAP *ld;
  HE_ENT *h;
  CR *c = a->credentials;
  time_t now = clocktime();
  int try;

  dlog("-> amu_ldap_rebind\n");

  if (a->ldap != NULL) {
    if ((a->timestamp - now) > AMD_LDAP_TTL) {
      dlog("Re-establishing ldap connection\n");
      ldap_unbind(a->ldap);
      a->timestamp = now;
      a->ldap = NULL;
    } else {
      /* Assume all is OK.  If it wasn't we'll be back! */
      dlog("amu_ldap_rebind: timestamp OK\n");
      return (0);
    }
  }

  for (try=0; try<10; try++) {	/* XXX: try up to 10 times (makes sense?) */
    for (h = a->hostent; h != NULL; h = h->next) {
      if ((ld = ldap_open(h->host, h->port)) == NULL) {
	plog(XLOG_WARNING, "Unable to ldap_open to %s:%d\n", h->host, h->port);
	break;
      }
      if (ldap_bind_s(ld, c->who, c->pw, c->method) != LDAP_SUCCESS) {
	plog(XLOG_WARNING, "Unable to ldap_bind to %s:%d as %s\n",
	     h->host, h->port, c->who);
	break;
      }
      if (gopt.ldap_cache_seconds > 0) {
#ifdef HAVE_LDAP_ENABLE_CACHE
	ldap_enable_cache(ld, gopt.ldap_cache_seconds, gopt.ldap_cache_maxmem);
#else /* HAVE_LDAP_ENABLE_CACHE */
	plog(XLOG_WARNING, "ldap_enable_cache(%d) does not exist on this system!\n", gopt.ldap_cache_seconds);
#endif /* HAVE_LDAP_ENABLE_CACHE */
      }
      a->ldap = ld;
      a->timestamp = now;
      return (0);
    }
    plog(XLOG_WARNING, "Exhausted list of ldap servers, looping.\n");
  }

  plog(XLOG_USER, "Unable to (re)bind to any ldap hosts\n");
  return (ENOENT);
}


static int
get_ldap_timestamp(ALD *a, char *map, time_t *ts)
{
  struct timeval tv;
  char **vals, *end;
  char filter[MAXPATHLEN];
  int i, err = 0, nentries = 0;
  LDAPMessage *res = NULL, *entry;

  dlog("-> get_ldap_timestamp: map <%s>\n", map);

  tv.tv_sec = 3;
  tv.tv_usec = 0;
  sprintf(filter, AMD_LDAP_TSFILTER, map);
  dlog("Getting timestamp for map %s\n", map);
  dlog("Filter is: %s\n", filter);
  dlog("Base is: %s\n", gopt.ldap_base);
  for (i = 0; i < AMD_LDAP_RETRIES; i++) {
    err = ldap_search_st(a->ldap,
			 gopt.ldap_base,
			 LDAP_SCOPE_SUBTREE,
			 filter,
			 0,
			 0,
			 &tv,
			 &res);
    if (err == LDAP_SUCCESS)
      break;
    if (res) {
      ldap_msgfree(res);
      res = NULL;
    }
    plog(XLOG_USER, "Timestamp LDAP search attempt %d failed: %s\n",
	 i + 1, ldap_err2string(err));
    if (err != LDAP_TIMEOUT) {
      dlog("get_ldap_timestamp: unbinding...\n");
      ldap_unbind(a->ldap);
      a->ldap = NULL;
      if (amu_ldap_rebind(a))
        return (ENOENT);
    }
    dlog("Timestamp search failed, trying again...\n");
  }

  if (err != LDAP_SUCCESS) {
    *ts = 0;
    plog(XLOG_USER, "LDAP timestamp search failed: %s\n",
	 ldap_err2string(err));
    if (res)
      ldap_msgfree(res);
    return (ENOENT);
  }

  nentries = ldap_count_entries(a->ldap, res);
  if (nentries == 0) {
    plog(XLOG_USER, "No timestamp entry for map %s\n", map);
    *ts = 0;
    ldap_msgfree(res);
    return (ENOENT);
  }

  entry = ldap_first_entry(a->ldap, res);
  vals = ldap_get_values(a->ldap, entry, AMD_LDAP_TSATTR);
  if (ldap_count_values(vals) == 0) {
    plog(XLOG_USER, "Missing timestamp value for map %s\n", map);
    *ts = 0;
    ldap_value_free(vals);
    ldap_msgfree(res);
    return (ENOENT);
  }
  dlog("TS value is:%s:\n", vals[0]);

  if (vals[0]) {
    *ts = (time_t) strtol(vals[0], &end, 10);
    if (end == vals[0]) {
      plog(XLOG_USER, "Unable to decode ldap timestamp %s for map %s\n",
	   vals[0], map);
      err = ENOENT;
    }
    if (!*ts > 0) {
      plog(XLOG_USER, "Nonpositive timestamp %ld for map %s\n",
	   *ts, map);
      err = ENOENT;
    }
  } else {
    plog(XLOG_USER, "Empty timestamp value for map %s\n", map);
    *ts = 0;
    err = ENOENT;
  }

  ldap_value_free(vals);
  ldap_msgfree(res);
  dlog("The timestamp for %s is %ld (err=%d)\n", map, *ts, err);
  return (err);
}


int
amu_ldap_search(mnt_map *m, char *map, char *key, char **pval, time_t *ts)
{
  char **vals, filter[MAXPATHLEN], filter2[2 * MAXPATHLEN];
  char *f1, *f2;
  struct timeval tv;
  int i, err = 0, nvals = 0, nentries = 0;
  LDAPMessage *entry, *res = NULL;
  ALD *a = (ALD *) (m->map_data);

  dlog("-> amu_ldap_search: map <%s>, key <%s>\n", map, key);

  tv.tv_sec = 2;
  tv.tv_usec = 0;
  if (a == NULL) {
    plog(XLOG_USER, "LDAP panic: no map data\n");
    return (EIO);
  }
  if (amu_ldap_rebind(a))	/* Check that's the handle is still valid */
    return (ENOENT);

  sprintf(filter, AMD_LDAP_FILTER, map, key);
  /* "*" is special to ldap_search(); run through the filter escaping it. */
  f1 = filter; f2 = filter2;
  while (*f1) {
    if (*f1 == '*') {
      *f2++ = '\\'; *f2++ = '2'; *f2++ = 'a';
      f1++;
    } else {
      *f2++ = *f1++;
    }
  }
  *f2 = '\0';
  dlog("Search with filter: <%s>\n", filter2);
  for (i = 0; i < AMD_LDAP_RETRIES; i++) {
    err = ldap_search_st(a->ldap,
			 gopt.ldap_base,
			 LDAP_SCOPE_SUBTREE,
			 filter2,
			 0,
			 0,
			 &tv,
			 &res);
    if (err == LDAP_SUCCESS)
      break;
    if (res) {
      ldap_msgfree(res);
      res = NULL;
    }
    plog(XLOG_USER, "LDAP search attempt %d failed: %s\n",
        i + 1, ldap_err2string(err));
    if (err != LDAP_TIMEOUT) {
      dlog("amu_ldap_search: unbinding...\n");
      ldap_unbind(a->ldap);
      a->ldap = NULL;
      if (amu_ldap_rebind(a))
        return (ENOENT);
    }
  }

  switch (err) {
  case LDAP_SUCCESS:
    break;
  case LDAP_NO_SUCH_OBJECT:
    dlog("No object\n");
    if (res)
      ldap_msgfree(res);
    return (ENOENT);
  default:
    plog(XLOG_USER, "LDAP search failed: %s\n",
	 ldap_err2string(err));
    if (res)
      ldap_msgfree(res);
    return (EIO);
  }

  nentries = ldap_count_entries(a->ldap, res);
  dlog("Search found %d entries\n", nentries);
  if (nentries == 0) {
    ldap_msgfree(res);
    return (ENOENT);
  }
  entry = ldap_first_entry(a->ldap, res);
  vals = ldap_get_values(a->ldap, entry, AMD_LDAP_ATTR);
  nvals = ldap_count_values(vals);
  if (nvals == 0) {
    plog(XLOG_USER, "Missing value for %s in map %s\n", key, map);
    ldap_value_free(vals);
    ldap_msgfree(res);
    return (EIO);
  }
  dlog("Map %s, %s => %s\n", map, key, vals[0]);
  if (vals[0]) {
    *pval = strdup(vals[0]);
    err = 0;
  } else {
    plog(XLOG_USER, "Empty value for %s in map %s\n", key, map);
    err = ENOENT;
  }
  ldap_msgfree(res);
  ldap_value_free(vals);

  return (err);
}


int
amu_ldap_mtime(mnt_map *m, char *map, time_t *ts)
{
  ALD *aldh = (ALD *) (m->map_data);

  if (aldh == NULL) {
    dlog("LDAP panic: unable to find map data\n");
    return (ENOENT);
  }
  if (amu_ldap_rebind(aldh)) {
    return (ENOENT);
  }
  if (get_ldap_timestamp(aldh, map, ts)) {
    return (ENOENT);
  }
  return (0);
}
