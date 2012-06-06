/*

  lame.c

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


#ifdef HAVE_LAME

#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>

#include <lame/lame.h>


// ------ Globals ---------
static lame_global_flags *lame_opts = NULL;
static short int *i16_buffer[2]={NULL,NULL};
static unsigned char *mpeg_buffer=NULL;


#define SAMPLES_PER_FRAME     (1152)


static void float32_to_short(
  const jack_default_audio_sample_t in[],
  short out[],
  int num_samples)
{
  int n;

  for(n=0; n<num_samples; n++) {
    int tmp = lrintf(in[n] * 32768.0f);
    if (tmp > SHRT_MAX) {
      out[n] = SHRT_MAX;
    } else if (tmp < SHRT_MIN) {
      out[n] = SHRT_MIN;
    } else {
      out[n] = (short) tmp;
    }
  }
}


/*
  Encode and write some audio from the ring buffer to disk
*/
static int write_lame(void *fh, size_t sample_count, jack_default_audio_sample_t *buffer[])
{
  FILE* file = (FILE*)fh;
  size_t i16_desired = sample_count * sizeof( short int );
  int bytes_encoded=0, bytes_written=0;
  int c=0;

  // Convert to 16-bit integer samples
  for (c=0; c<channels; c++) {
    i16_buffer[c] = (short int*)realloc(i16_buffer[c], i16_desired );
    if (!i16_buffer[c]) rotter_fatal( "realloc on i16_buffer failed" );
    float32_to_short( buffer[c], i16_buffer[c], sample_count );
  }

  // Encode it
  bytes_encoded = lame_encode_buffer( lame_opts,
            i16_buffer[0], i16_buffer[1],
            sample_count, mpeg_buffer, WRITE_BUFFER_SIZE );

  if (bytes_encoded<0) {
    rotter_fatal( "Error: while encoding audio.");
    return -1;
  } else if (bytes_encoded>0) {
    // Write it to disk
    bytes_written = fwrite(mpeg_buffer, 1, bytes_encoded, file);
    if (bytes_written != bytes_encoded) {
      rotter_error( "Warning: failed to write encoded audio to disk: %s", strerror(errno) );
      return -1;
    }
  }

  // Success
  return 0;
}




static void deinit_lame()
{
  int c;

  rotter_debug("Shutting down LAME encoder.");
  lame_close( lame_opts );

  for( c=0; c<2; c++) {
    if (i16_buffer[c]) {
      free(i16_buffer[c]);
      i16_buffer[c]=NULL;
    }
  }

  if (mpeg_buffer) {
    free(mpeg_buffer);
    mpeg_buffer=NULL;
  }

}


static const char* lame_get_version_name( lame_global_flags *glopts )
{
  int version = lame_get_version( glopts );
  if (version==0) { return "MPEG-2"; }
  else if (version==1) { return "MPEG-1"; }
  else if (version==2) { return "MPEG-2.5"; }
  else { return "MPEG-?"; }

}

static const char* lame_get_mode_name( lame_global_flags *glopts )
{
  int mode = lame_get_mode( glopts );
  if (mode==STEREO) { return "Stereo"; }
  else if (mode==JOINT_STEREO) { return "Joint Stereo"; }
  else if (mode==DUAL_CHANNEL) { return "Dual Channel"; }
  else if (mode==MONO) { return "Mono"; }
  else { return "Unknown Mode"; }
}

encoder_funcs_t* init_lame( output_format_t* format, int channels, int bitrate )
{
  encoder_funcs_t* funcs = NULL;

  lame_opts = lame_init();
  if (lame_opts==NULL) {
    rotter_error("lame error: failed to initialise.");
    return NULL;
  }

  if ( 0 > lame_set_num_channels( lame_opts, channels ) ) {
    rotter_error("lame error: failed to set number of channels.");
    return NULL;
  }

  if ( 0 > lame_set_in_samplerate( lame_opts, jack_get_sample_rate( client ) )) {
    rotter_error("lame error: failed to set input samplerate.");
    return NULL;
  }

  if ( 0 > lame_set_out_samplerate( lame_opts, jack_get_sample_rate( client ) )) {
    rotter_error("lame error: failed to set output samplerate.");
    return NULL;
  }

  if ( 0 > lame_set_brate( lame_opts, bitrate) ) {
    rotter_error("lame error: failed to set bitrate.");
    return NULL;
  }

  if ( 0 > lame_init_params( lame_opts ) ) {
    rotter_error("lame error: failed to initialize parameters.");
    return NULL;
  }


  rotter_info( "Encoding using liblame version %s.", get_lame_version() );
  rotter_debug( "  Input: %d Hz, %d channels",
            lame_get_in_samplerate(lame_opts),
            lame_get_num_channels(lame_opts));
  rotter_debug( "  Output: %s Layer 3, %d kbps, %s",
            lame_get_version_name(lame_opts),
            lame_get_brate(lame_opts),
            lame_get_mode_name(lame_opts));

  // Allocate memory for encoded audio
  mpeg_buffer = malloc( 1.25*SAMPLES_PER_FRAME + 7200 );
  if ( mpeg_buffer==NULL ) {
    rotter_error( "Failed to allocate memory for encoded audio." );
    return NULL;
  }

  // Allocate memory for callback functions
  funcs = calloc( 1, sizeof(encoder_funcs_t) );
  if ( funcs==NULL ) {
    rotter_error( "Failed to allocate memory for encoder callback functions structure." );
    return NULL;
  }


  funcs->file_suffix = "mp3";
  funcs->open = open_mpegaudio_file;
  funcs->close = close_mpegaudio_file;
  funcs->write = write_lame;
  funcs->deinit = deinit_lame;


  return funcs;
}

#endif   // HAVE_LAME


