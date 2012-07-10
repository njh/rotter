/*

  deletefiles.c

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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>

#include "rotter.h"
#include "config.h"

pid_t delete_child_pid = 0;     // PID of process deleting old files


static void delete_file( const char* filepath, dev_t device, time_t timestamp )
{
  struct stat sb;

  if (stat( filepath, &sb )) {
    rotter_error( "Warning: failed to stat file: %s", filepath );
    return;
  }

  if (sb.st_dev != device) {
    rotter_debug( "Warning: %s isn't on same device as root dir.", filepath );
    return;
  }

  if (sb.st_mtime < timestamp) {
    rotter_debug( "Deleting file: %s", filepath );

    if (unlink(filepath)) {
      rotter_error( "Warning: failed to delete file: %s (%s)", filepath, strerror(errno) );
      return;
    }
  }

}


static dev_t get_file_device( const char* filepath )
{
  struct stat sb;

  if (stat( filepath, &sb )) {
    rotter_error( "Warning: failed to stat file: %s", filepath );
    return -1;
  }

  return sb.st_dev;
}


static void deletefiles_in_dir( const char* dirpath, dev_t device, time_t timestamp )
{
  DIR *dirp = opendir(dirpath);
  struct dirent *dp;

  if (dirp==NULL) {
    rotter_fatal( "Failed to open directory: %s.", dirpath );
    return;
  }

  // Check each item in the directory
  while( (dp = readdir( dirp )) != NULL ) {
    int newpath_len;
    char* newpath;

    if (strcmp( ".", dp->d_name )==0) continue;
    if (strcmp( "..", dp->d_name )==0) continue;

    newpath_len = strlen(dirpath) + strlen(dp->d_name) + 2;
    newpath = malloc( newpath_len );
    snprintf( newpath, newpath_len, "%s/%s", dirpath, dp->d_name );

    if (dp->d_type == DT_DIR) {

      // Check we are on the same device
      if (get_file_device(newpath) != device) {
        rotter_debug( "Warning: %s isn't on same device as root dir.", dirpath );
      } else {
        // Delete files in the directory
        deletefiles_in_dir( newpath, device, timestamp );

        // Try and delete the directory itself
        if (rmdir(newpath) && errno != ENOTEMPTY) {
          rotter_error( "Warning: failed to delete directory: %s (%s)",
                        newpath, strerror(errno) );
        }
      }

    } else if (dp->d_type == DT_REG) {

      delete_file( newpath, device, timestamp );

    } else {
      rotter_error( "Warning: not a file or a directory: %s" );
    }
    free( newpath );

  }

  closedir( dirp );
}



// Delete files older than 'hours'
int deletefiles( const char* dirpath, int hours )
{
  int old_niceness, new_niceness = 15;
  time_t now = time(NULL);
  dev_t device = get_file_device( dirpath );

  if (hours<=0)
    return 0;

  if (delete_child_pid) {
    rotter_error( "Not deleting files: the last deletion process has not finished." );
    return delete_child_pid;
  }

  rotter_info( "Deleting files older than %d hours in %s.", hours, dirpath );

  // Fork a new process
  delete_child_pid = fork();
  if (delete_child_pid>0) {
    // Parent is here
    rotter_debug( "Forked new proccess to delete files (pid=%d).", delete_child_pid );
    return delete_child_pid;
  } else if (delete_child_pid<0) {
    rotter_error( "Warning: fork failed: %s", strerror(errno) );
    return 0;
  }

  // Make this process nicer
  // (deleting files is pretty unimportant)
  old_niceness = nice( new_niceness );
  rotter_debug( "Changed child proccess niceless from %d to %d.", old_niceness, new_niceness );
  
  // Sleep for 10 seconds, so we don't use up CPU while a new new files 
  // are just starting to be encoded, and so that we don't delete empty directories
  // just as they are being created.
  sleep(10);

  // Recursively process directories
  deletefiles_in_dir( dirpath, device, now-(hours*3600) );

  // End of child process
  exit(0);
}


// Harvest zombie processes
void deletefiles_cleanup_child()
{
  // Has a child process finished?
  if (delete_child_pid) {
    int status = 0;
    pid_t pid = waitpid( delete_child_pid, &status, WNOHANG );
    if (pid) {
      delete_child_pid = 0;
      if (status) {
        rotter_error( "File deletion child-process exited with status %d", status );
      } else {
        rotter_debug( "File deletion child-process has finished." );
      }
    }
  }
}
