/*

  mpegaudiofile.c

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
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#include "rotter.h"
#include "config.h"


/*
  ID3v1.0 Structure
  Informal specification: http://www.id3.org/id3v1.html

  I don't especially like ID3v1 but it is really simple.
  Adding support for ID3v2 would be a lot more code.
*/


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


// Write an ID3v1 tag to a file handle
static void write_id3v1(FILE* file, struct timeval *file_start)
{
  char year[5];
  struct tm tm;
  id3v1_t id3;

  if (file==NULL) return;

  // Zero the ID3 data structure
  bzero( &id3, sizeof( id3v1_t ));

  // Get a breakdown of the time recording started at
  localtime_r( &file_start->tv_sec, &tm );

  // Header
  id3.tag[0] = 'T';
  id3.tag[1] = 'A';
  id3.tag[2] = 'G';

  // Title
  snprintf( id3.title, sizeof(id3.title), "Recorded %4.4d-%2.2d-%2.2d %2.2d:%2.2d",
        tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min );

  // Artist / Originator
  if (originator) {
    strncpy( id3.artist, originator, sizeof(id3.artist)-1 );
  }

  // UNUSED: Album

  // Year
  sprintf( year, "%4.4d" , tm.tm_year+1900);
  memcpy( id3.year, year, 4 );

  // Comment
  snprintf( id3.comment, sizeof(id3.comment), "Created by %s v%s",
          PACKAGE_NAME, PACKAGE_VERSION );

  // Deliberately invalid genre
  id3.genre = 255;

  // Now write it to file
  if (fwrite( &id3, sizeof(id3v1_t), 1, file) != 1) {
    rotter_error( "Warning: failed to write ID3v1 tag." );
  }
}


int close_mpegaudio_file(void* fh, struct timeval *file_start)
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


void* open_mpegaudio_file( const char* filepath, struct timeval *file_start )
{
  FILE* file;

  rotter_debug("Opening MPEG Audio output file: %s", filepath);
  file = fopen( filepath, "ab" );
  if (file==NULL) {
    rotter_error( "Failed to open output file: %s", strerror(errno) );
    return NULL;
  }

  return file;
}

int sync_mpegaudio_file(void *fh)
{
  FILE *file = (FILE*)fh;
  int fd = fileno(file);
  return fsync(fd);
}
