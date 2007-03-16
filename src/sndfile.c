/*

	sndfile.c
	
	rotter: Recording of Transmission / Audio Logger
	Copyright (C) 2007  Nicholas J. Humfrey
	
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
static jack_default_audio_sample_t *pcm_buffer[2]= {NULL,NULL};


// Encode a frame of audio
static int encode_sndfile()
{
	//jack_nframes_t samples = 1152;
	//size_t desired = samples * sizeof( jack_default_audio_sample_t );
	//int bytes_read=0, bytes_encoded=0, bytes_written=0;
	//int c=0;
	
	// Check that the output file is open
	if (sndfile==NULL) {
		rotter_error( "Warning: output file isn't open, while trying to encode.");
		// Try again later
		return 0;
	
	}
	
	// Is the enough in the ring buffer?
	//if (jack_ringbuffer_read_space( ringbuffer[0] ) < desired) {
		// Try again later
	//	return 0;
	//}
	

	// Take audio out of the ring buffer
    //for (c=0; c<sfinfo.channels; c++)
    //{    
 	//	// Ensure the temporary buffer is big enough
	//	pcm_buffer[c] = (jack_default_audio_sample_t*)realloc(pcm_buffer[c], desired );
	//	if (!pcm_buffer[c]) rotter_fatal( "realloc on tmp_buffer failed" );
	//
	//	// Copy frames from ring buffer to temporary buffer
    //   bytes_read = jack_ringbuffer_read( ringbuffer[c], (char*)pcm_buffer[c], desired);
	//	if (bytes_read != desired) {
	//		rotter_fatal( "Failed to read desired number of bytes from ringbuffer." );
	//	}
    //}



	// Write it to disk
	//sf_count_t  sf_write_float   (sndfile, float *ptr, sf_count_t items) ;
	//bytes_written = fwrite( mpeg_buffer, 1, bytes_encoded, mpegaudio_file);
	//if (bytes_written != bytes_encoded) {
	//	rotter_error( "Warning: failed to write encoded audio to disk.");
	//	return -1;
	//}
	

	// Success
	//return bytes_written;
	return 0;

}




static void shutdown_sndfile()
{

	rotter_debug("Closing down sndfile encoder.");

	if (pcm_buffer[0]) {
		free(pcm_buffer[0]);
		pcm_buffer[0]=NULL;
	}
	
	if (pcm_buffer[1]) {
		free(pcm_buffer[1]);
		pcm_buffer[1]=NULL;
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

	//sndfile = fopen( filepath, "ab" );
	//if (mpegaudio_file==NULL) {
	//	rotter_error( "Failed to open output file: %s", strerror(errno) );
		return -1;
	//}

	// Success
	return 0;
}




encoder_funcs_t* init_sndfile( const char* format, int channels, int bitrate )
{
	encoder_funcs_t* funcs = NULL;
	char sndlibver[128];
	int k,format_count=0;
	SF_FORMAT_INFO	format_info;
	
	// Get the version of libsndfile
	sf_command(NULL, SFC_GET_LIB_VERSION, sndlibver, sizeof(sndlibver));


	
	rotter_info( "Encoding using libsndfile version %s.", sndlibver );
/*	rotter_debug( "  Input: %d Hz, %d channels",
						twolame_get_in_samplerate(twolame_opts),
						twolame_get_num_channels(twolame_opts));
	rotter_debug( "  Output: %s Layer 2, %d kbps, %s",
						twolame_get_version_name(twolame_opts),
						twolame_get_bitrate(twolame_opts),
						twolame_get_mode_name(twolame_opts));
*/

	// Get the number of supported formats
	sf_command (NULL, SFC_GET_SIMPLE_FORMAT_COUNT, &format_count, sizeof(format_count)) ;
	printf("Number of supported formats: %d\n", format_count );
	
	for (k = 0 ; k < format_count ; k++)
	{   format_info.format = k ;
            sf_command (sndfile, SFC_GET_SIMPLE_FORMAT, &format_info, sizeof (format_info)) ;
            printf ("0x%08x  %s %s\n", format_info.format, format_info.name, format_info.extension) ;
	} ;
	
	exit(-1);

	// Allocate memory for callback functions
	funcs = calloc( 1, sizeof(encoder_funcs_t) );
	if ( funcs==NULL ) {
		rotter_error( "Failed to allocate memery for encoder callback functions structure." );
		return NULL;
    }
	

	funcs->file_suffix = ".mp2";
	funcs->open = open_sndfile;
	funcs->close = close_sndfile;
	funcs->encode = encode_sndfile;
	funcs->shutdown = shutdown_sndfile;


	return funcs;
}

#endif   // HAVE_SNDFILE

