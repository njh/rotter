/*

  hostname.c

  rotter: Recording of Transmission / Audio Logger
  Copyright (C) 2006-2012  Nicholas J. Humfrey

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

#include "rotter.h"
#include "config.h"


#ifndef HOST_NAME_MAX
#ifdef _POSIX_HOST_NAME_MAX
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#else
#define HOST_NAME_MAX (256)
#endif
#endif

#ifndef DOMAIN_NAME_MAX
#define DOMAIN_NAME_MAX (1024)
#endif



char* rotter_get_hostname()
{
  char hostname[ HOST_NAME_MAX ];
  char domainname[ DOMAIN_NAME_MAX ];
  struct hostent  *hp;

  if (gethostname( hostname, HOST_NAME_MAX ) < 0) {
    // Failed
    return NULL;
  }

  // If it contains a dot, then assume it is a FQDN
  if (strchr(hostname, '.') != NULL)
    return strdup( hostname );

  // Try resolving the hostname into a FQDN
  if ( (hp = gethostbyname( hostname )) != NULL ) {
    if (strchr(hp->h_name, '.') != NULL)
      return strdup( hp->h_name );
  }

  // Try appending our domain name
  if ( getdomainname( domainname, DOMAIN_NAME_MAX) == 0
       && strlen( domainname ) )
  {
    int fqdn_len = strlen( hostname ) + strlen( domainname ) + 2;
    char *fqdn = malloc( fqdn_len );
    snprintf( fqdn, fqdn_len, "%s.%s", hostname, domainname );
    return fqdn;
  }

  // What else can we try?
  return NULL;
}

