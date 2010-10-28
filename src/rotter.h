/*

  rotter.h

  rotter: Recording of Transmission / Audio Logger
  Copyright (C) 2006-2009  Nicholas J. Humfrey

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


#include "config.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#ifdef HAVE_SNDFILE
#include <sndfile.h>
#endif

#ifdef HAVE_LAME
#include <lame/lame.h>
#endif

#ifdef HAVE_TWOLAME
#include <twolame.h>
#endif


#ifndef _ROTTER_H_
#define _ROTTER_H_


// ------- Constants -------
#define DEFAULT_RB_LEN        (2.0)
#define WRITE_BUFFER_SIZE     (2048)
#define MAX_FILEPATH_LEN      (1024)
#define DEFAULT_DIR_MODE      (0755)
#define DEFAULT_CLIENT_NAME   "rotter"
#define DEFAULT_ARCHIVE_NAME  "archive"
#define DEFAULT_FILE_LAYOUT   "hierarchy"
#define DEFAULT_BITRATE       (160)
#define DEFAULT_CHANNELS      (2)
#define DEFAULT_DELETE_HOURS  (0)


#ifndef LAME_SAMPLES_PER_FRAME
#define LAME_SAMPLES_PER_FRAME (1152)
#endif

#ifndef TWOLAME_SAMPLES_PER_FRAME
#define TWOLAME_SAMPLES_PER_FRAME (1152)
#endif

#ifndef SNDFILE_SAMPLES_PER_FRAME
#define SNDFILE_SAMPLES_PER_FRAME (512)
#endif


// ------- Logging ---------
typedef enum {
  ROTTER_DEBUG=1,   // Only display debug if verbose
  ROTTER_INFO,      // Don't show info when quiet
  ROTTER_ERROR,     // Always display warnings
  ROTTER_FATAL      // Quit if fatal
} RotterLogLevel;


#define rotter_debug( ... ) \
    rotter_log( ROTTER_DEBUG, __VA_ARGS__ )

#define rotter_info( ... ) \
    rotter_log( ROTTER_INFO, __VA_ARGS__ )

#define rotter_error( ... ) \
    rotter_log( ROTTER_ERROR, __VA_ARGS__ )

#define rotter_fatal( ... ) \
    rotter_log( ROTTER_FATAL, __VA_ARGS__ )



// ------- Structures -------
typedef struct rotter_ringbuffer_s
{
    time_t hour_start;
    void* file_handle;
    jack_ringbuffer_t *buffer[2];
    int close_file;                  // Flag to indigate that file should be closed
    int overflow;                    // Flag to indigate that a ringbuffer overflowed
} rotter_ringbuffer_t;

typedef struct encoder_funcs_s
{
  const char* file_suffix;            // Suffix for archive files

  void* (*open)(const char * filepath); // Result: pointer to file handle
  int (*close)(void *fh);               // Result: 0=success

  int (*write)(void *fh, size_t sample_count, jack_default_audio_sample_t *buffer[]);
                                      // Result: 0=success

  void (*deinit)();

} encoder_funcs_t;


typedef struct
{
  const char  *name ;
  const char  *desc ;
  size_t      samples_per_frame;
  int         param ;

  encoder_funcs_t* (*initfunc)(const char* format, int channels, int bitrate);
} output_format_map_t;




// ------- Globals ---------
extern jack_port_t *inport[2];
extern jack_client_t *client;
extern output_format_map_t format_map[];
extern int running;     // True while still running
extern int channels;      // Number of input channels
extern float rb_duration;   // Duration of ring buffer


extern rotter_ringbuffer_t *ringbuffers[2];
extern rotter_ringbuffer_t *active_ringbuffer;



// ------- Prototypes -------

// In rotter.c
void rotter_log( RotterLogLevel level, const char* fmt, ... );

// In jack.c
void init_jack( const char* client_name, jack_options_t jack_opt );
void connect_jack_port( const char* out, jack_port_t *port );
void autoconnect_jack_ports( jack_client_t* client );
void deinit_jack();

// In twolame.c
encoder_funcs_t* init_twolame( const char* format, int channels, int bitrate );

// In lame.c
encoder_funcs_t* init_lame( const char* format, int channels, int bitrate );

// In sndfile.c
encoder_funcs_t* init_sndfile( const char* format, int channels, int bitrate );

// In mpegaudiofile.c
int close_mpegaudio_file(void* fh);
void* open_mpegaudio_file(const char* filepath);

// In deletefiles.c
int delete_files( const char* dir, int hours );


#endif
