/*

  dir.c

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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

#include <sys/stat.h>
#include <dirent.h>

#include "rotter.h"
#include "config.h"


int rotter_directory_exists(const char * filepath)
{
  struct stat s;
  int i = stat ( filepath, &s );
  if ( i == 0 )
  {
    if (s.st_mode & S_IFDIR) {
      // Yes, a directory
      return 1;
    } else {
      // Not a directory
      rotter_error( "Not a directory: %s", filepath );
      return 0;
    }
  }

  return 0;
}


int rotter_mkdir_p( const char* dir )
{
  int result;

  result = mkdir(dir, DEFAULT_DIR_MODE);
  if (result && errno == ENOENT) {
    // ENOENT (a parent directory doesn't exist)
    char* parent = strdup( dir );
    int i;

    // Create parent directories recursively
    for(i=strlen(parent); i>0; i--) {
      if (parent[i]=='/') {
        parent[i]=0;
        result = rotter_mkdir_p( parent );
        break;
      }
    }

    free(parent);

    // Try again to create the directory
    if (result == 0) {
      result = mkdir(dir, DEFAULT_DIR_MODE);
    }
  }

  if (result && errno == EEXIST) {
    // Directory already exists - good
    result = 0;
  }

  return result;
}


int rotter_mkdir_for_file( const char* filepath )
{
  char *dir, *buf;
  int result = 0;

  // Duplicate the filepath, as dirname may modify the string
  buf = strdup(filepath);
  if (buf == NULL) {
    return -1;
  }

  // Remove the filename from end of the filepath
  dir = dirname(buf);
  if (dir == NULL) {
    free(buf);
    return -1;
  }

  // Create directory if it didn't exist
  if (!rotter_directory_exists(dir)) {
    rotter_debug("Creating directory: %s", dir);
    if (rotter_mkdir_p(dir)) {
      result = -1;
    }
  }

  free(buf);
  return result;
}
