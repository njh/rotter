/*

	mpegaudiofile.c
	Recording of Transmission / Audio Logger
	Copyright (C) 2006  Nicholas J. Humfrey
	
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

// ------- Globals -------
FILE *mpegaudio_file = NULL;


/* 
	I don't especially like ID3v1 but it is really simple.
	Informal specification: http://www.id3.org/id3v1.html
*/


#ifndef HOST_NAME_MAX
#ifdef _POSIX_HOST_NAME_MAX
#define HOST_NAME_MAX	_POSIX_HOST_NAME_MAX
#else
#define HOST_NAME_MAX	(256)
#endif
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
	char genre;
} id3v1_t;




// Write an ID3v1 tag to a file handle
static void write_id3v1()
{
	char hostname[HOST_NAME_MAX];
	char year[5];
	struct tm tm;
	id3v1_t id3;
	
	if (mpegaudio_file==NULL) return;
	
	// Zero the ID3 data structure
	bzero( &id3, sizeof( id3v1_t )); 

	// Get a breakdown of the time recording started at
	localtime_r( &file_start, &tm );

	
	// Header
	id3.tag[0] = 'T';
	id3.tag[1] = 'A';
	id3.tag[2] = 'G';
	
	// Title
	snprintf( id3.title, sizeof(id3.title), "Recorded %4.4d-%2.2d-%2.2d %2.2d:%2.2d",
				tm.tm_year+1900, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min );

	// Artist - hostname
	bzero( hostname, HOST_NAME_MAX );
	gethostname( hostname, HOST_NAME_MAX );
	memcpy( id3.artist, hostname, sizeof(id3.artist) );


	// Album - unused
	
	
	// Year
	sprintf( year, "%4.4d" , tm.tm_year+1900);
	memcpy( id3.year, year, 4 );

	// Comment
	snprintf( id3.comment, sizeof(id3.comment), "Created by %s v%s",
					PACKAGE_NAME, PACKAGE_VERSION );
	
	// Deliberately invalid genre
	id3.genre = 255;

	// Now write it to file
	if (fwrite( &id3, sizeof(id3v1_t), 1, mpegaudio_file) != 1) {
		rotter_error( "Warning: failed to write ID3v1 tag." );
	}
}




int close_mpegaudio_file()
{
	if (mpegaudio_file==NULL) return -1;
	
	// Write ID3v1 tags
	write_id3v1( );


	rotter_debug("Closing MPEG Audio output file.");

	if (fclose(mpegaudio_file)) {
		rotter_error( "Failed to close output file: %s", strerror(errno) );
		return -1;
	}
	
	// File is now closed
	mpegaudio_file=NULL;

	// Success
	return 0;
}


int open_mpegaudio_file( char* filepath )
{

	rotter_debug("Opening MPEG Audio output file: %s", filepath);
	if (mpegaudio_file) {
		rotter_error("Warning: already open while opening output file."); 
		close_mpegaudio_file();
	}

	mpegaudio_file = fopen( filepath, "ab" );
	if (mpegaudio_file==NULL) {
		rotter_error( "Failed to open output file: %s", strerror(errno) );
		return -1;
	}

	// Success
	return 0;
}



