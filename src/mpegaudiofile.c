/*

  mpegaudiofile.c

  rotter: Recording of Transmission / Audio Logger
  Copyright (C) 2006-2010  Nicholas J. Humfrey

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
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>

#include "rotter.h"
#include "config.h"


/*
  I don't especially like ID3v1 but it is really simple.
  Informal specification: http://www.id3.org/id3v1.html
*/


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



// ------- ID3v1.0 Structure -------

typedef struct id3v1_s
{
  char tag[3];
  char title[30];
  char artist[30];
  char album[30];
  char year[4];
  char comment[30];
  unsigned char genre;
} id3v1_t;


static char* gethostname_fqdn()
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


// Write an ID3v1 tag to a file handle
static void write_id3v1(FILE* file, time_t file_start)
{
  char *hostname;
  char year[5];
  struct tm tm;
  id3v1_t id3;

  if (file==NULL) return;

  // Zero the ID3 data structure
  bzero( &id3, sizeof( id3v1_t ));

  // Get a breakdown of the time recording started at
  localtime_r( &file_start, &tm );


  // Header
  id3.tag[0] = 'T';
  id3.tag[1] = 'A';
  id3.tag[2] = 'G';

  // Title
  snprintf( id3.title, sizeof(id3.title)-1, "Recorded %4.4d-%2.2d-%2.2d %2.2d:%2.2d",
        tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min );

  // Artist - hostname
  hostname = gethostname_fqdn();
  if (hostname) {
    strncpy( id3.artist, hostname, sizeof(id3.artist)-1 );
    free(hostname);
  }

  // Album - unused


  // Year
  sprintf( year, "%4.4d" , tm.tm_year+1900);
  memcpy( id3.year, year, 4 );

  // Comment
  snprintf( id3.comment, sizeof(id3.comment)-1, "Created by %s v%s",
          PACKAGE_NAME, PACKAGE_VERSION );

  // Deliberately invalid genre
  id3.genre = 255;

  // Now write it to file
  if (fwrite( &id3, sizeof(id3v1_t), 1, file) != 1) {
    rotter_error( "Warning: failed to write ID3v1 tag." );
  }
}


int close_mpegaudio_file(void* fh, time_t file_start)
{
  FILE *file = (FILE*)fh;

  if (file==NULL) return -1;

  // Write ID3v1 tags
  write_id3v1(file, file_start);

  rotter_debug("Closing MPEG Audio output file.");

  if (fclose(file)) {
    rotter_error( "Failed to close output file: %s", strerror(errno) );
    return -1;
  }

  // Success
  return 0;
}


void* open_mpegaudio_file( const char* filepath )
{
  FILE* file;

  rotter_debug("Opening MPEG Audio output file: %s", filepath);
  file = fopen( filepath, "ab" );
  if (file==NULL) {
    rotter_error( "Failed to open output file: %s", strerror(errno) );
    return 0;
  }

  return file;
}

int sync_mpegaudio_file(void *fh)
{
  FILE *file = (FILE*)fh;
  int fd = fileno(file);
  return fsync(fd);
}
