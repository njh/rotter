/*

  sndfile.c

  rotter: Recording of Transmission / Audio Logger
  Copyright (C) 2007-2010  Nicholas J. Humfrey

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
static jack_default_audio_sample_t *interleaved_buffer = NULL;
static SF_INFO sfinfo;



/*
  Write some audio from the ring buffer to disk
*/
static int write_sndfile(void *fh, size_t sample_count, jack_default_audio_sample_t *buffer[])
{
  SNDFILE *sndfile = (SNDFILE *)fh;
  sf_count_t frames_written = 0;
  int i,c;

  // Interleave the audio into another buffer
  interleaved_buffer = (jack_default_audio_sample_t*)realloc(interleaved_buffer, sample_count * channels * sizeof(jack_default_audio_sample_t));
  if (!interleaved_buffer) rotter_fatal( "realloc on interleaved_buffer failed" );
  for (c=0; c<channels; c++)
  {
    for(i=0;i<sample_count;i++) {
      interleaved_buffer[(i*channels)+c] = buffer[c][i];
    }
  }

  // Write it to disk
  frames_written = sf_writef_float(sndfile, interleaved_buffer, sample_count);
  if (frames_written != sample_count) {
    rotter_error( "Warning: failed to write audio to disk: %s", sf_strerror( sndfile ));
    return -1;
  }

  // Success
  return 0;
}


static int sync_sndfile(void *fh)
{
  SNDFILE *sndfile = (SNDFILE *)fh;
  sf_write_sync(sndfile);
  return 0;
}


static void deinit_sndfile()
{
  rotter_debug("Shutting down sndfile encoder.");

  if (interleaved_buffer) {
    free(interleaved_buffer);
    interleaved_buffer=NULL;
  }
}


static int close_sndfile(void *fh, time_t file_start)
{
  SNDFILE *sndfile = (SNDFILE *)fh;
  if (sndfile==NULL) return -1;

  rotter_debug("Closing libsndfile output file.");

  if (sf_close(sndfile)) {
    rotter_error( "Failed to close output file: %s", sf_strerror(sndfile) );
    return -1;
  }

  // Success
  return 0;
}


static void* open_sndfile(const char* filepath)
{
  SNDFILE *sndfile = NULL;
  int read_write_mode = 1;
  int result = 0;

  rotter_debug("Opening libsndfile output file: %s", filepath);
  sndfile = sf_open( filepath, SFM_RDWR, &sfinfo );
  
  // Some output formats, like flac and vorbis, do not support read/write mode
  // There is no stable way to trap this specific error in the libsndfile public API
  // so if we fail to open the file, try once more in write-only mode. 
  //
  // Using fallback, rather than hard-coding the current capabilities of each format, so that
  // we will automatically benefit if libsndfile is extended to provide read/write mode for
  // more formats.
  //
  // In write-only mode, we cannot append to existing files, so their contents, if they exist
  // will be clobbered, but this is the original behaviour of rotter, so isn't a regression.
  if (sndfile==NULL) {
    rotter_info( "Failed to open output file in read/write mode, so trying write-only" );
    read_write_mode = 0;
    sndfile = sf_open( filepath, SFM_WRITE, &sfinfo );
  }
 
  if (sndfile==NULL) {
    rotter_error( "Failed to open output file: %s", sf_strerror(NULL) );
    return NULL;
  }

  // Seek to the end of the file, so that we don't overwrite any existing audio
  // We can only do this if the file is opened in read/write mode. Not all formats
  // support this.
  if (read_write_mode) {
    result = sf_seek(sndfile, 0, SEEK_END);
    if (result < 0) {
      rotter_error( "Failed to seek to end of file before writing: %s", sf_strerror(sndfile) );
    }
  }

  return (void*)sndfile;
}


encoder_funcs_t* init_sndfile( output_format_t* format, int channels, int bitrate )
{
  encoder_funcs_t* funcs = NULL;
  SF_FORMAT_INFO format_info;
  SF_FORMAT_INFO subformat_info;
  char sndlibver[128];

  // Zero the SF_INFO structures
  bzero( &sfinfo, sizeof( SF_INFO ) );
  bzero( &format_info, sizeof( SF_FORMAT_INFO ) );
  bzero( &subformat_info, sizeof( SF_FORMAT_INFO ) );

  // Check the format parameter flags
  sfinfo.format = format->param;
  if (sfinfo.format == 0x00) {
    rotter_error( "No libsndfile format flags defined for [%s]", format->name );
    return NULL;
  }

  // Get the version of libsndfile
  if (sf_command(NULL, SFC_GET_LIB_VERSION, sndlibver, sizeof(sndlibver))>0) {
    rotter_debug( "Encoding using libsndfile version %s.", sndlibver );
  } else {
    rotter_debug( "Failed to get libsndfile version.");
  }

  // Lookup inforamtion about the format and subtype
  format_info.format = sfinfo.format & SF_FORMAT_TYPEMASK;
  if (sf_command(NULL, SFC_GET_FORMAT_INFO, &format_info, sizeof(format_info))) {
    rotter_error( "Failed to get format information for: 0x%4.4x", format_info.format);
    return NULL;
  }

  subformat_info.format = sfinfo.format & SF_FORMAT_SUBMASK;
  if (sf_command (NULL, SFC_GET_FORMAT_INFO, &subformat_info, sizeof(subformat_info))) {
    rotter_error( "Failed to get sub-format information for: 0x%4.4x", subformat_info.format);
    return NULL;
  }


  // Fill in the rest of the SF_INFO data structure
  sfinfo.samplerate = jack_get_sample_rate( client );
  sfinfo.channels = channels;

  // Display info about input/output
  rotter_debug( "  Input: %d Hz, %d channels", sfinfo.samplerate, sfinfo.channels );
  rotter_debug( "  Output: %s, %s.", format_info.name, subformat_info.name );

  // Check that the format is valid
  if (!sf_format_check(&sfinfo)) {
    rotter_error( "Output format is not valid." );
    return NULL;
  }

  // Allocate memory for callback functions
  funcs = calloc( 1, sizeof(encoder_funcs_t) );
  if ( funcs==NULL ) {
    rotter_error( "Failed to allocate memory for encoder callback functions structure." );
    return NULL;
  }


  // Fill in the encoder callback functions
  funcs->file_suffix = format_info.extension;
  funcs->open = open_sndfile;
  funcs->close = close_sndfile;
  funcs->write = write_sndfile;
  funcs->sync = sync_sndfile;
  funcs->deinit = deinit_sndfile;

  return funcs;
}

#endif   // HAVE_SNDFILE

