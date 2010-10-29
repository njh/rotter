/*

  rotter.c

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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "config.h"
#include "rotter.h"



// ------- Globals -------
int quiet = 0;          // Only display error messages
int verbose = 0;        // Increase number of logging messages
char* file_layout = DEFAULT_FILE_LAYOUT;  // File layout: Flat files or folder hierarchy ?
char* archive_name = NULL;      // Archive file name
int running = 1;        // True while still running
int channels = DEFAULT_CHANNELS;    // Number of input channels
float rb_duration = DEFAULT_RB_LEN;   // Duration of ring buffer
char *root_directory = NULL;      // Root directory of archives
int delete_hours = DEFAULT_DELETE_HOURS;  // Delete files after this many hours
pid_t delete_child_pid = 0;     // PID of process deleting old files
encoder_funcs_t* encoder = NULL;

size_t samples_per_frame = 0;  // FIXME: should this be a global?
jack_default_audio_sample_t *tmp_buffer[2];
rotter_ringbuffer_t *ringbuffers[2] = {NULL,NULL};
rotter_ringbuffer_t *active_ringbuffer = NULL;

output_format_map_t format_map [] =
{

#ifdef HAVE_LAME
  { "mp3",  "MPEG Audio Layer 3", LAME_SAMPLES_PER_FRAME, 0, init_lame },
#endif

#ifdef HAVE_TWOLAME
  { "mp2",  "MPEG Audio Layer 2", TWOLAME_SAMPLES_PER_FRAME, 0, init_twolame },
#endif

#ifdef HAVE_SNDFILE
  { "aiff", "AIFF (Apple/SGI 16 bit PCM)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_AIFF | SF_FORMAT_PCM_16, init_sndfile },
  { "aiff32", "AIFF (Apple/SGI 32 bit float)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_AIFF | SF_FORMAT_FLOAT, init_sndfile },
  { "au",   "AU (Sun/Next 16 bit PCM)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_AU   | SF_FORMAT_PCM_16, init_sndfile },
  { "au32", "AU (Sun/Next 32 bit float)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_AU   | SF_FORMAT_FLOAT, init_sndfile },
  { "caf",  "CAF (Apple 16 bit PCM)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_CAF  | SF_FORMAT_PCM_16, init_sndfile },
  { "caf32",  "CAF (Apple 32 bit float)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_CAF  | SF_FORMAT_FLOAT, init_sndfile },
  { "flac", "FLAC 16 bit",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_FLAC | SF_FORMAT_PCM_16, init_sndfile },
  { "vorbis", "Ogg Vorbis",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_OGG  | SF_FORMAT_VORBIS, init_sndfile },
  { "wav",  "WAV (Microsoft 16 bit PCM)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_WAV  | SF_FORMAT_PCM_16, init_sndfile },
  { "wav32",  "WAV (Microsoft 32 bit float)",
    SNDFILE_SAMPLES_PER_FRAME, SF_FORMAT_WAV  | SF_FORMAT_FLOAT, init_sndfile },
#endif

  // End of list
  { NULL,   NULL,               0 },

} ; /* format_map */




static void
termination_handler (int signum)
{
  switch(signum) {
    case SIGHUP:  rotter_info("Got hangup signal."); break;
    case SIGTERM: rotter_info("Got termination signal."); break;
    case SIGINT:  rotter_info("Got interupt signal."); break;
  }

  signal(signum, termination_handler);

  // Signal the main thead to stop
  running = 0;
}



void rotter_log( RotterLogLevel level, const char* fmt, ... )
{
  time_t t=time(NULL);
  char time_str[32];
  va_list args;


  // Display the message level
  if (level == ROTTER_DEBUG ) {
    if (!verbose) return;
    printf( "[DEBUG]  " );
  } else if (level == ROTTER_INFO ) {
    if (quiet) return;
    printf( "[INFO]   " );
  } else if (level == ROTTER_ERROR ) {
    printf( "[ERROR]  " );
  } else if (level == ROTTER_FATAL ) {
    printf( "[FATAL]  " );
  } else {
    printf( "[UNKNOWN]" );
  }

  // Display timestamp
  ctime_r( &t, time_str );
  time_str[strlen(time_str)-1]=0; // remove \n
  printf( "%s  ", time_str );

  // Display the error message
  va_start( args, fmt );
  vprintf( fmt, args );
  printf( "\n" );
  va_end( args );

  // If fatal then stop
  if (level == ROTTER_FATAL) {
    if (running) {
      // Quit gracefully
      running = 0;
    } else {
      printf( "Fatal error while quiting; exiting immediately." );
      exit(-1);
    }
  }
}






static int directory_exists(const char * filepath)
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


static int mkdir_p( const char* dir )
{
  int result = 0;

  if (directory_exists( dir )) {
    return 0;
  }

  if (mkdir(dir, DEFAULT_DIR_MODE) < 0) {
    if (errno == ENOENT) {
      // ENOENT (a parent directory doesn't exist)
      char* parent = strdup( dir );
      int i;

      // Create parent directories recursively
      for(i=strlen(parent); i>0; i--) {
        if (parent[i]=='/') {
          parent[i]=0;
          result = mkdir_p( parent );
          break;
        }
      }

      free(parent);

      // Try again to create the directory
      if (result==0) {
        result = mkdir(dir, DEFAULT_DIR_MODE);
      }

    } else {
      result = -1;
    }
  }

  return result;
}


static char * time_to_filepath_flat( time_t clock, const char* suffix )
{
  struct tm tm;
  char* filepath = malloc( MAX_FILEPATH_LEN );

  localtime_r( &clock, &tm );

  if (archive_name) {
    // Create the full file path
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%s-%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, archive_name, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );
  } else {
    // Create the full file path
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );
  }

  return filepath;
}


static char * time_to_filepath_hierarchy( time_t clock, const char* suffix )
{
  struct tm tm;
  char* filepath = malloc( MAX_FILEPATH_LEN );


  localtime_r( &clock, &tm );

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d",
        root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour );

  if (mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  if (archive_name) {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%s.%s",
        root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, archive_name, suffix );
  } else {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%s.%s",
        root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, DEFAULT_ARCHIVE_NAME, suffix );
  }


  return filepath;
}


static char * time_to_filepath_combo( time_t clock, const char* suffix )
{
  struct tm tm;
  char* filepath = malloc( MAX_FILEPATH_LEN );


  localtime_r( &clock, &tm );

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d",
        root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour );

  if (mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  if (archive_name) {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%s-%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, archive_name, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );
  } else {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );
  }

  return filepath;
}

static char * time_to_filepath_dailydir( time_t clock, const char* suffix )
{
  struct tm tm;
  char* filepath = malloc( MAX_FILEPATH_LEN );


  localtime_r( &clock, &tm );

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d",
        root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday );

  if (mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  if (archive_name) {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d/%s-%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, archive_name, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );
  } else {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d/%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );
  }

  return filepath;
}

static size_t read_from_ringbuffer(rotter_ringbuffer_t *ringbuffer, size_t desired_frames)
{
  size_t desired_bytes = desired_frames * sizeof(jack_default_audio_sample_t);
  int c, bytes_read=0;

  // Is there enough in the ring buffers?
  for (c=0; c<channels; c++) {
    if (jack_ringbuffer_read_space( ringbuffer->buffer[c] ) < desired_bytes) {
      // Try again later
      return 0;
    }
  }

  // Get the audio out of the ring buffer
  for (c=0; c<channels; c++) {
    // FIXME: check size is enough before trying to realloc

    // Ensure the temporary buffer is big enough
    tmp_buffer[c] = (jack_default_audio_sample_t*)realloc(tmp_buffer[c], desired_bytes);
    if (!tmp_buffer[c])
      rotter_fatal( "realloc on tmp_buffer failed" );

    // Copy frames from ring buffer to temporary buffer
    bytes_read = jack_ringbuffer_read( ringbuffer->buffer[c], (char*)tmp_buffer[c], desired_bytes);
    if (bytes_read != desired_bytes) {
      rotter_fatal( "Failed to read desired number of bytes from ringbuffer channel %d.", c);
    }
  }

  return bytes_read / sizeof(jack_default_audio_sample_t);
}



// FIXME: think of better function name
static int open_file(rotter_ringbuffer_t *ringbuffer)
{
    char* filepath = NULL;
    if (file_layout[0] == 'h' || file_layout[0] == 'H') {
      filepath = time_to_filepath_hierarchy( ringbuffer->hour_start, encoder->file_suffix );
    } else if (file_layout[0] == 'f' || file_layout[0] == 'F') {
      filepath = time_to_filepath_flat( ringbuffer->hour_start, encoder->file_suffix );
    } else if (file_layout[0] == 'c' || file_layout[0] == 'C') {
      filepath = time_to_filepath_combo( ringbuffer->hour_start, encoder->file_suffix );
    } else if (file_layout[0] == 'd' || file_layout[0] == 'D') {
      filepath = time_to_filepath_dailydir( ringbuffer->hour_start, encoder->file_suffix );
    } else {
      rotter_fatal("Unknown file layout: %s", file_layout);
    }

    // Open the new file
    rotter_info( "Opening new archive file: %s", filepath );
    ringbuffer->file_handle = encoder->open(filepath);
    if (ringbuffer->file_handle == NULL) {
      // FIXME: don't keep trying to open same file after an error?
      return 1;
    }

    free(filepath);

    return 0;
}


// FIXME: think of better function name
static void process_audio()
{
  int total_samples = 0;
  int b;

  for(b=0; b<2; b++) {
    rotter_ringbuffer_t *ringbuffer = ringbuffers[b];
    int samples = 0;

    // Has there been a ringbuffer overflow?
    if (ringbuffer->overflow) {
      rotter_error( "Ringbuffer %c overflowed while writing audio.", b==0 ? 'A':'B');
      ringbuffer->overflow = 0;
    }

    // Read some audio from the buffer
    samples = read_from_ringbuffer( ringbuffer, samples_per_frame );
    if (samples < 1) {
      continue;
    } else {
      total_samples += samples;
    }

    // Open a new file?
    if (ringbuffer->file_handle == NULL) {
      rotter_debug("Going to open new file for ringbuffer %c.", b==0 ? 'A':'B');
      open_file(ringbuffer);
    }

    // Write some audio to disk
    int result = encoder->write(ringbuffer->file_handle, samples, tmp_buffer);
    if (result) {
      rotter_fatal("Shutting down, due to encoding error.");
      break;
    }

    // Close the old file
    if (ringbuffer->close_file) {
      encoder->close(ringbuffer->file_handle);
      // FIXME: check for error closing file
      ringbuffer->close_file = 0;
      ringbuffer->file_handle = NULL;
    }


/*
      // Delete files older delete_hours
      if (delete_hours>0) {
        if (delete_child_pid) {
          rotter_error( "Not deleting files: the last deletion process has not finished." );
        } else {
          delete_child_pid = delete_files( root_directory, delete_hours );
        }
      }
    }

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
*/


  }

  if (total_samples == 0) {
    // FIXME: caculate this once at the start
    float sleep_time = ((float)samples_per_frame / jack_get_sample_rate( client ));
    usleep( sleep_time * 1000000 );
  }

}

static int init_ringbuffers()
{
  size_t ringbuffer_size = 0;
  int b,c;

  ringbuffer_size = jack_get_sample_rate( client ) * rb_duration * sizeof(float);
  rotter_debug("Size of the ring buffers is %2.2f seconds (%d bytes).", rb_duration, (int)ringbuffer_size );

  for(b=0; b<2; b++) {
    ringbuffers[b] = malloc(sizeof(rotter_ringbuffer_t));
    if (!ringbuffers[b]) {
      rotter_fatal("Cannot allocate memory for ringbuffer structure %d.", b);
    }

    ringbuffers[b]->hour_start = 0;
    ringbuffers[b]->file_handle = NULL;
    ringbuffers[b]->overflow = 0;
    ringbuffers[b]->close_file = 0;
    ringbuffers[b]->buffer[0] = NULL;
    ringbuffers[b]->buffer[1] = NULL;

    for(c=0; c<channels; c++) {
      ringbuffers[b]->buffer[c] = jack_ringbuffer_create( ringbuffer_size );
      if (!ringbuffers[b]->buffer[c]) {
        rotter_fatal("Cannot create ringbuffer buffer %d,%d.", b,c);
      }
    }
  }

  return 0;
}

static int deinit_ringbuffers()
{
  int b,c;

  for(b=0; b<2; b++) {
    if (ringbuffers[b]) {
      for(c=0;c<2;c++) {
        if (ringbuffers[b]->buffer[c]) {
          jack_ringbuffer_free(ringbuffers[b]->buffer[c]);
        }
      }

      if (ringbuffers[b]->file_handle) {
        encoder->close(ringbuffers[b]->file_handle);
        // FIXME: check for error closing file
        ringbuffers[b]->file_handle = NULL;
      }

      free(ringbuffers[b]);
    }
  }

  return 0;
}


static char* str_tolower( char* str )
{
  int i=0;

  for(i=0; i< strlen( str ); i++) {
    str[i] = tolower( str[i] );
  }

  return str;
}


// Display how to use this program
static void usage()
{
  int i;

  printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
  printf("Usage: %s [options] <root_directory>\n", PACKAGE_NAME);
  printf("   -a            Automatically connect JACK ports\n");
  printf("   -l <port>     Connect the left input to this port\n");
  printf("   -r <port>     Connect the right input to this port\n");
  printf("   -f <format>   Format of recording (see list below)\n");
  printf("   -b <bitrate>  Bitrate of recording (bitstream formats only)\n");
  printf("   -c <channels> Number of channels\n");
  printf("   -n <name>     Name for this JACK client\n");
  printf("   -N <filename> Name for archive files (default 'archive')\n");
  printf("   -d <hours>    Delete files in directory older than this\n");
  printf("   -R <secs>     Length of the ring buffer (in seconds)\n");
  printf("   -L <layout>   File layout (default 'hierarchy')\n");
  printf("   -j            Don't automatically start jackd\n");
  printf("   -v            Enable verbose mode\n");
  printf("   -q            Enable quiet mode\n");

  printf("\nSupported file layouts:\n");
  printf("   flat          /root_directory/YYYY-MM-DD-HH.suffix\n");
  printf("   hierarchy     /root_directory/YYYY/MM/DD/HH/archive.suffix\n");
  printf("   combo         /root_directory/YYYY/MM/DD/HH/YYYY-MM-DD-HH.suffix\n");
  printf("   dailydir      /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH.suffix\n");

  // Display the available audio output formats
  printf("\nSupported audio output formats:\n");
  for(i=0; format_map[i].name; i++) {
    printf("   %-6s        %s", format_map[i].name, format_map[i].desc );
    if (i==0) printf("   [Default]");
    printf("\n");
  }
  printf("\n");


  exit(1);
}


int main(int argc, char *argv[])
{
  int autoconnect = 0;
  jack_options_t jack_opt = JackNullOption;
  char *client_name = DEFAULT_CLIENT_NAME;
  char *connect_left = NULL;
  char *connect_right = NULL;
  const char *format = format_map[0].name;
  int bitrate = DEFAULT_BITRATE;
  int i,opt;

  // Make STDOUT unbuffered
  setbuf(stdout, NULL);

  // Parse Switches
  while ((opt = getopt(argc, argv, "al:r:n:N:jf:b:d:c:R:L:vqh")) != -1) {
    switch (opt) {
      case 'a':  autoconnect = 1; break;
      case 'l':  connect_left = optarg; break;
      case 'r':  connect_right = optarg; break;
      case 'n':  client_name = optarg; break;
      case 'N':  archive_name = optarg; break;
      case 'j':  jack_opt |= JackNoStartServer; break;
      case 'f':  format = str_tolower(optarg); break;
      case 'b':  bitrate = atoi(optarg); break;
      case 'd':  delete_hours = atoi(optarg); break;
      case 'c':  channels = atoi(optarg); break;
      case 'R':  rb_duration = atof(optarg); break;
      case 'L':  file_layout = optarg; break;
      case 'v':  verbose = 1; break;
      case 'q':  quiet = 1; break;
      default:  usage(); break;
    }
  }

  // Validate parameters
  if (quiet && verbose) {
      rotter_error("Can't be quiet and verbose at the same time.");
      usage();
  }

  // Check the number of channels
  if (channels!=1 && channels!=2) {
    rotter_error("Number of channels should be either 1 or 2.");
    usage();
  }

  // Check remaining arguments
    argc -= optind;
    argv += optind;
    if (argc!=1) {
      rotter_error("%s requires a root directory argument.", PACKAGE_NAME);
      usage();
  } else {
    root_directory = argv[0];
    if (root_directory[strlen(root_directory)-1] == '/')
      root_directory[strlen(root_directory)-1] = 0;

    if (directory_exists(root_directory)) {
      rotter_debug("Root directory: %s", root_directory);
    } else {
      rotter_fatal("Root directory does not exist: %s", root_directory);
    }
  }


  // Initialise JACK
  init_jack( client_name, jack_opt );

  // Create ring buffers
  init_ringbuffers();

  // Initialise encoder
  for(i=0; format_map[i].name; i++) {
    if (strcmp( format_map[i].name, format ) == 0) {
      // Display information about the format
      rotter_debug("User selected [%s] '%s'.",  format_map[i].name,  format_map[i].desc);

      // Call the init function
      if (format_map[i].initfunc == NULL) {
        rotter_error("Error: no init function defined for format [%s].", format );
      } else {
        encoder = format_map[i].initfunc( format, channels, bitrate );
      }

      // FIXME:
      samples_per_frame = format_map[i].samples_per_frame;

      // Found encoder
      break;
    }
  }

  // Failed to find match?
  if (encoder==NULL && format_map[i].name==NULL) {
    rotter_fatal("Failed to find format [%s], please check the supported format list.", format);
  }

  // Other failure?
  if (encoder==NULL) {
    rotter_debug("Failed to initialise encoder.");
    deinit_jack();
    exit(-1);
  }

  // Activate JACK
  if (jack_activate(client))
    rotter_fatal("Cannot activate JACK client.");

  // Setup signal handlers
  signal(SIGTERM, termination_handler);
  signal(SIGINT, termination_handler);
  signal(SIGHUP, termination_handler);

  // Auto-connect our input ports ?
  if (autoconnect) autoconnect_jack_ports( client );
  if (connect_left) connect_jack_port( connect_left, inport[0] );
  if (connect_right && channels == 2) connect_jack_port( connect_right, inport[1] );


  while( running ) {
    process_audio(encoder);
  }

  // Shut down encoder
  if (encoder)
    encoder->deinit();

  // Clean up JACK
  deinit_jack();

  // FIXME: free tmp_buffer

  deinit_ringbuffers();

  return 0;
}

