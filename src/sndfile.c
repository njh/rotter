/*

	sndfile.c
	
	rotter: Recording of Transmission / Audio Logger
	Copyright (C) 2007-2009  Nicholas J. Humfrey
	
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
#include <math.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>

#include "rotter.h"


#ifdef HAVE_SNDFILE

#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>

#include <sndfile.h>



// ------ Globals ---------
static SNDFILE *sndfile = NULL;
static SF_INFO sfinfo;
static jack_default_audio_sample_t *interleaved_buffer = NULL;
static jack_default_audio_sample_t *tmp_buffer[2] = {NULL,NULL};
static const jack_nframes_t read_size = 256;	// Write 256 samples to disk at a time 



/*
	Write some audio from the ring buffer to disk
*/
static int write_sndfile()
{
	size_t desired = read_size * sizeof( jack_default_audio_sample_t );
	sf_count_t frames_written = 0;
	int i,c, bytes_read=0;
	
	// Check that the output file is open
	if (sndfile==NULL) {
		rotter_error( "Warning: output file isn't open, while trying to encode.");
		// Try again later
		return 0;
	
	}
	
	// Is the enough in the ring buffer?
	if (jack_ringbuffer_read_space( ringbuffer[0] ) < desired) {
		// Try again later
		return 0;
	}
	
	// Get the audio out of the ring buffer
    for (c=0; c<channels; c++)
    {    
		// Ensure the temporary buffer is big enough
		tmp_buffer[c] = (jack_default_audio_sample_t*)realloc(tmp_buffer[c], desired );
		if (!tmp_buffer[c]) rotter_fatal( "realloc on tmp_buffer failed" );

		// Copy frames from ring buffer to temporary buffer
        bytes_read = jack_ringbuffer_read( ringbuffer[c], (char*)tmp_buffer[c], desired);
		if (bytes_read != desired) {
			rotter_fatal( "Failed to read desired number of bytes from ringbuffer." );
		}
    }

	// Interleave the audio into yet another buffer
	interleaved_buffer = (jack_default_audio_sample_t*)realloc(interleaved_buffer, desired*channels );
	for (c=0; c<channels; c++)
	{    
		for(i=0;i<read_size;i++) {
			interleaved_buffer[(i*channels)+c] = tmp_buffer[c][i];
		}
	}
		
	// Write it to disk
	frames_written = sf_writef_float(sndfile, interleaved_buffer, read_size) ;
	if (frames_written != read_size) {
		rotter_error( "Warning: failed to write audio to disk: %s", sf_strerror( sndfile ));
		return -1;
	}
	

	// Success
	return frames_written;
}




static void deinit_sndfile()
{
	int c;
	
	rotter_debug("Shutting down sndfile encoder.");

	for(c=0;c<2;c++) {
		if (tmp_buffer[c]) {
			free(tmp_buffer[c]);
			tmp_buffer[c]=NULL;
		}
	}
	
	if (interleaved_buffer) {
		free(interleaved_buffer);
		interleaved_buffer=NULL;
	}
}



static int close_sndfile()
{
	if (sndfile==NULL) return -1;
	
	rotter_debug("Closing libsndfile output file.");

	if (sf_close(sndfile)) {
		rotter_error( "Failed to close output file: %s", sf_strerror(sndfile) );
		return -1;
	}
	
	// File is now closed
	sndfile=NULL;

	// Success
	return 0;
}


static int open_sndfile( const char* filepath )
{

	rotter_debug("Opening libsndfile output file: %s", filepath);
	if (sndfile) {
		rotter_error("Warning: already open while opening output file."); 
		close_sndfile();
	}

	sndfile = sf_open( filepath, SFM_WRITE, &sfinfo );
	if (sndfile==NULL) {
		rotter_error( "Failed to open output file: %s", sf_strerror(NULL) );
		return -1;
	}

	// Success
	return 0;
}




encoder_funcs_t* init_sndfile( const char* fmt_str, int channels, int bitrate )
{
	encoder_funcs_t* funcs = NULL;
	char sndlibver[128];
	SF_FORMAT_INFO format_info;
	SF_FORMAT_INFO subformat_info;
	int i;

	// Zero the SF_INFO structure
	bzero( &sfinfo, sizeof( sfinfo ) );

	// Lookup the format parameters
	for(i=0; format_map[i].name; i++) {
		if (strcmp( format_map[i].name, fmt_str ) == 0) {
			sfinfo.format = format_map[i].param;
		}
	}
	
	// Check it found something
	if (sfinfo.format == 0x00) {
		rotter_error( "No libsndfile format flags defined for [%s]\n", fmt_str );
		return NULL;
	}
	
	// Get the version of libsndfile
	sf_command(NULL, SFC_GET_LIB_VERSION, sndlibver, sizeof(sndlibver));
	rotter_debug( "Encoding using libsndfile version %s.", sndlibver );

	// Lookup inforamtion about the format and subtype
	format_info.format = sfinfo.format & SF_FORMAT_TYPEMASK;
	sf_command (NULL, SFC_GET_FORMAT_INFO, &format_info, sizeof(format_info));
	subformat_info.format = sfinfo.format & SF_FORMAT_SUBMASK;
	sf_command (NULL, SFC_GET_FORMAT_INFO, &subformat_info, sizeof(subformat_info));


	// Fill in the rest of the SF_INFO data structure
	sfinfo.samplerate = jack_get_sample_rate( client );
	sfinfo.channels = channels;

	// Display info about input/output
	rotter_debug( "  Input: %d Hz, %d channels", sfinfo.samplerate, sfinfo.channels );
	rotter_debug( "  Output: %s, %s.", format_info.name, subformat_info.name );
	

	// Allocate memory for callback functions
	funcs = calloc( 1, sizeof(encoder_funcs_t) );
	if ( funcs==NULL ) {
		rotter_error( "Failed to allocate memery for encoder callback functions structure." );
		return NULL;
	}
	
	
	// Fill in the encoder callback functions
	funcs->file_suffix = format_info.extension;
	funcs->open = open_sndfile;
	funcs->close = close_sndfile;
	funcs->write = write_sndfile;
	funcs->deinit = deinit_sndfile;


	return funcs;
}

#endif   // HAVE_SNDFILE

