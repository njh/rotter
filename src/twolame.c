/*

  twolame.c

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
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "rotter.h"


#ifdef HAVE_TWOLAME

#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>

#include <twolame.h>



// ------ Globals ---------
static twolame_options *twolame_opts = NULL;
static unsigned char *mpeg_buffer = NULL;




/*
  Encode and write some audio from the ring buffer to disk
*/
static int write_twolame(void *fh, size_t sample_count, jack_default_audio_sample_t *buffer[])
{
  int bytes_encoded=0, bytes_written=0;
  FILE *file = (FILE*)fh;

  // Encode it
  bytes_encoded = twolame_encode_buffer_float32(
            twolame_opts, buffer[0], buffer[1],
            sample_count, mpeg_buffer, WRITE_BUFFER_SIZE
  );

  if (bytes_encoded<0) {
    rotter_fatal( "Error: while encoding audio.");
    return -1;
  } else if (bytes_encoded>0) {
    // Write it to disk
    bytes_written = fwrite(mpeg_buffer, 1, bytes_encoded, file);
    if (bytes_written != bytes_encoded) {
      rotter_error( "Warning: failed to write encoded audio to disk.");
      return -1;
    }
  }

  // Success
  return 0;
}

static int sync_twolame(void *fh)
{
  FILE *file = (FILE*)fh;
  int fd = fileno(file);
  return fsync(fd);
}

static void deinit_twolame()
{
  rotter_debug("Shutting down TwoLAME encoder.");
  if (twolame_opts) {
    twolame_close( &twolame_opts );
    twolame_opts=NULL;
  }

  if (mpeg_buffer) {
    free(mpeg_buffer);
    mpeg_buffer=NULL;
  }
}


encoder_funcs_t* init_twolame( output_format_t* format, int channels, int bitrate )
{
  encoder_funcs_t* funcs = NULL;

  twolame_opts = twolame_init();
  if (twolame_opts==NULL) {
    rotter_error("TwoLAME error: failed to initialise.");
    return NULL;
  }

  if ( 0 > twolame_set_num_channels( twolame_opts, channels ) ) {
    rotter_error("TwoLAME error: failed to set number of channels.");
    deinit_twolame();
    return NULL;
  }

  if ( 0 > twolame_set_in_samplerate( twolame_opts, jack_get_sample_rate( client ) )) {
    rotter_error("TwoLAME error: failed to set input samplerate.");
    deinit_twolame();
    return NULL;
  }

  if ( 0 > twolame_set_out_samplerate( twolame_opts, jack_get_sample_rate( client ) )) {
    rotter_error("TwoLAME error: failed to set output samplerate.");
    deinit_twolame();
    return NULL;
  }

  if ( 0 > twolame_set_brate( twolame_opts, bitrate) ) {
    rotter_error("TwoLAME error: failed to set bitrate.");
    deinit_twolame();
    return NULL;
  }

  if ( 0 > twolame_init_params( twolame_opts ) ) {
    rotter_error("TwoLAME error: failed to initialize parameters.");
    deinit_twolame();
    return NULL;
  }


  rotter_debug( "Encoding using libtwolame version %s.", get_twolame_version() );
  rotter_debug( "  Input: %d Hz, %d channels",
            twolame_get_in_samplerate(twolame_opts),
            twolame_get_num_channels(twolame_opts));
  rotter_debug( "  Output: %s Layer 2, %d kbps, %s",
            twolame_get_version_name(twolame_opts),
            twolame_get_bitrate(twolame_opts),
            twolame_get_mode_name(twolame_opts));

  // Allocate memory for encoded audio
  mpeg_buffer = malloc( 1.25*TWOLAME_SAMPLES_PER_FRAME + 7200 );
  if ( mpeg_buffer==NULL ) {
    rotter_error( "Failed to allocate memory for encoded audio." );
    deinit_twolame();
    return NULL;
  }

  // Allocate memory for callback functions
  funcs = calloc( 1, sizeof(encoder_funcs_t) );
  if ( funcs==NULL ) {
    rotter_error( "Failed to allocate memory for encoder callback functions structure." );
    deinit_twolame();
    return NULL;
  }

  funcs->file_suffix = "mp2";
  funcs->open = open_mpegaudio_file;
  funcs->close = close_mpegaudio_file;
  funcs->write = write_twolame;
  funcs->sync = sync_twolame;
  funcs->deinit = deinit_twolame;

  return funcs;
}

#endif   // HAVE_TWOLAME
