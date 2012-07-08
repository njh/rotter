/*

  rotter.c

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
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"
#include "rotter.h"



// ------- Globals -------
int quiet = 0;          // Only display error messages
int verbose = 0;        // Increase number of logging messages
int utc = 0;            // Use UTC rather than local time for filenames
char* file_layout = DEFAULT_FILE_LAYOUT;  // File layout: Flat files or folder hierarchy ?
char* archive_name = NULL;      // Archive file name
int channels = DEFAULT_CHANNELS;    // Number of input channels
float rb_duration = DEFAULT_RB_LEN;   // Duration of ring buffer
char *root_directory = NULL;      // Root directory of archives
int delete_hours = DEFAULT_DELETE_HOURS;  // Delete files after this many hours
long archive_period_seconds = DEFAULT_ARCHIVE_PERIOD_SECONDS;  // Duration of each archive file

RotterRunState rotter_run_state = ROTTER_STATE_RUNNING;
encoder_funcs_t* encoder = NULL;

jack_default_audio_sample_t *tmp_buffer[2] = {NULL,NULL};
rotter_ringbuffer_t *ringbuffers[2] = {NULL,NULL};

output_format_t *output_format = NULL;
output_format_t format_list [] =
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

} ; /* format_list */




static
void rotter_termination_handler (int signum)
{
  switch(signum) {
    case SIGHUP:  rotter_info("Got hangup signal."); break;
    case SIGTERM: rotter_info("Got termination signal."); break;
    case SIGINT:  rotter_info("Got interupt signal."); break;
  }

  // Signal the main thead to stop
  rotter_run_state = ROTTER_STATE_QUITING;
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
    if (rotter_run_state == ROTTER_STATE_RUNNING) {
      rotter_run_state = ROTTER_STATE_ERROR;
    } else {
      printf( "Fatal error while quiting; exiting immediately." );
      exit(-1);
    }
  }
}






static int rotter_directory_exists(const char * filepath)
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


static int rotter_mkdir_p( const char* dir )
{
  int result = 0;

  if (rotter_directory_exists( dir )) {
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
          result = rotter_mkdir_p( parent );
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


static char * time_to_filepath_flat( struct tm *tm, const char* suffix )
{
  char* filepath = malloc( MAX_FILEPATH_LEN );

  if (archive_name) {
    // Create the full file path
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%s-%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, archive_name, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, suffix );
  } else {
    // Create the full file path
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, suffix );
  }

  return filepath;
}


static char * time_to_filepath_hierarchy( struct tm *tm, const char* suffix )
{
  char* filepath = malloc( MAX_FILEPATH_LEN );
  if (!filepath)
      return NULL;

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour );

  if (rotter_mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  if (archive_name) {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%s.%s",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, archive_name, suffix );
  } else {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%s.%s",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, DEFAULT_ARCHIVE_NAME, suffix );
  }


  return filepath;
}


static char * time_to_filepath_combo( struct tm *tm, const char* suffix )
{
  char* filepath = malloc( MAX_FILEPATH_LEN );
  if (!filepath)
      return NULL;

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour );

  if (rotter_mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  if (archive_name) {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%s-%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, archive_name, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, suffix );
  } else {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, suffix );
  }

  return filepath;
}

static char * time_to_filepath_dailydir( struct tm *tm, const char* suffix )
{
  char* filepath = malloc( MAX_FILEPATH_LEN );
  if (!filepath)
      return NULL;

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday );

  if (rotter_mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  if (archive_name) {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d/%s-%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, archive_name, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, suffix );
  } else {
    snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d/%4.4d-%2.2d-%2.2d-%2.2d.%s",
          root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, suffix );
  }

  return filepath;
}

static char * time_to_filepath_accurate( struct tm *tm, unsigned int usec, const char* suffix )
{
  char* filepath = malloc( MAX_FILEPATH_LEN );
  if (!filepath)
      return NULL;

  // Make sure the parent directories exists
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday );

  if (rotter_mkdir_p( filepath ))
    rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );


  // Create the full file path
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d/%4.4d-%2.2d-%2.2d-%2.2d-%2.2d-%2.2d-%2.2d.%s",
        root_directory, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour,
        tm->tm_min, tm->tm_sec, (int)(usec / 10000), suffix );

  return filepath;
}

static char * time_to_filepath_custom( struct tm *tm, char * file_layout )
{
  int i;
  char* filepath = malloc( MAX_FILEPATH_LEN );
  if (!filepath)
      return NULL;

  // Copy root directory path and separator into new filepath
  snprintf( filepath, MAX_FILEPATH_LEN, "%s/", root_directory );

  // Append custom filepath to end of the root directory path
  // Ensure custom filepath is constructed OK by checking number of characters appended
  // This also catches the possible error that an empty format string has been supplied
  if(!strftime( (filepath + strlen(filepath)), (MAX_FILEPATH_LEN - strlen(filepath)), file_layout, tm ))
      return NULL;

  // Work backwards to locate separator between directory/file part
  // Only works on systems using '/' as a directory separator, but this is also assumed elsewhere
  for(i=strlen(filepath); i>0; i--) {
    if (filepath[i]=='/') {
      filepath[i]=0; // Temporarily replace with string terminator

      // Make sure directory exists
      if (rotter_mkdir_p( filepath ))
        rotter_fatal( "Failed to create directory (%s): %s", filepath, strerror(errno) );

      filepath[i]='/'; // Replace directory/file separator
      break;
    }
  }

  return filepath;
}


static size_t rotter_read_from_ringbuffer(rotter_ringbuffer_t *ringbuffer, size_t desired_frames)
{
  size_t desired_bytes = desired_frames * sizeof(jack_default_audio_sample_t);
  size_t available_bytes = 0;
  int c, bytes_read = 0;

  // Is there enough in the ring buffers?
  for (c=0; c<channels; c++) {
    available_bytes = jack_ringbuffer_read_space( ringbuffer->buffer[c] );
    if (available_bytes <= 0) {
      // Try again later
      return 0;
    }
  }

  if (available_bytes > desired_bytes)
    available_bytes = desired_bytes;

  // Get the audio out of the ring buffer
  for (c=0; c<channels; c++) {
    // Copy frames from ring buffer to temporary buffer
    bytes_read = jack_ringbuffer_read( ringbuffer->buffer[c], (char*)tmp_buffer[c], available_bytes);
    if (bytes_read != available_bytes) {
      rotter_fatal( "Failed to read from ringbuffer %c channel %d.", ringbuffer->label, c);
      return 0;
    }
  }

  return bytes_read / sizeof(jack_default_audio_sample_t);
}


static int rotter_open_file(rotter_ringbuffer_t *ringbuffer)
{
  char* filepath = NULL;
  struct tm tm;

  if (utc) {
    gmtime_r( &ringbuffer->file_start.tv_sec, &tm );
  } else {
    localtime_r( &ringbuffer->file_start.tv_sec, &tm );
  }

  if (!strcasecmp(file_layout, "hierarchy")) {
    filepath = time_to_filepath_hierarchy( &tm, encoder->file_suffix );
  } else if (!strcasecmp(file_layout, "flat")) {
    filepath = time_to_filepath_flat( &tm, encoder->file_suffix );
  } else if (!strcasecmp(file_layout, "combo")) {
    filepath = time_to_filepath_combo( &tm, encoder->file_suffix );
  } else if (!strcasecmp(file_layout, "dailydir")) {
    filepath = time_to_filepath_dailydir( &tm, encoder->file_suffix );
  } else if (!strcasecmp(file_layout, "accurate")) {
    filepath = time_to_filepath_accurate( &tm, ringbuffer->file_start.tv_usec, encoder->file_suffix );
  } else {
    filepath = time_to_filepath_custom( &tm, file_layout );
  }

  if (!filepath)
    return 1;

  // Open the new file
  rotter_info( "Opening new archive file for ringbuffer %c: %s", ringbuffer->label, filepath );
  ringbuffer->file_handle = encoder->open(filepath);
  free(filepath);

  if (ringbuffer->file_handle) {
    // Success
    return 0;
  } else {
    return 1;
  }
}

static int rotter_close_file(rotter_ringbuffer_t *ringbuffer)
{
  rotter_info( "Closing file for ringbuffer %c.", ringbuffer->label);
  encoder->close(ringbuffer->file_handle, ringbuffer->period_start);
  ringbuffer->close_file = 0;
  ringbuffer->file_handle = NULL;
  return 0;
}

static int rotter_process_audio()
{
  int total_samples = 0;
  int result;
  int b;

  for(b=0; b<2; b++) {
    rotter_ringbuffer_t *ringbuffer = ringbuffers[b];
    int samples = 0;

    // Has there been a ringbuffer overflow?
    if (ringbuffer->overflow) {
      rotter_error( "Ringbuffer %c overflowed while writing audio.", ringbuffer->label);
      ringbuffer->overflow = 0;
    }

    // Read some audio from the buffer
    samples = rotter_read_from_ringbuffer( ringbuffer, output_format->samples_per_frame );
    if (samples > 0) {
      total_samples += samples;

      // Open a new file?
      if (ringbuffer->file_handle == NULL) {
        result = rotter_open_file(ringbuffer);
        if (result) {
          rotter_error("Failed to open file.");
          break;
        }
      }

      // Write some audio to disk
      result = encoder->write(ringbuffer->file_handle, samples, tmp_buffer);
      if (result) {
        rotter_error("An error occured while trying to write audio to disk.");
        break;
      }
    }

    // Close the old file
    if (samples <= 0 && ringbuffer->close_file) {
      rotter_close_file(ringbuffer);

      // Delete files older delete_hours
      if (delete_hours>0)
        deletefiles( root_directory, delete_hours );
    }

  } // for(b=0..2)

  return total_samples;
}

static int init_ringbuffers()
{
  size_t ringbuffer_size = 0;
  int b,c;

  ringbuffer_size = jack_get_sample_rate( client ) * rb_duration * sizeof(jack_default_audio_sample_t);
  rotter_debug("Size of the ring buffers is %2.2f seconds (%d bytes).", rb_duration, (int)ringbuffer_size );

  for(b=0; b<2; b++) {
    char label = ('A' + b);
    ringbuffers[b] = malloc(sizeof(rotter_ringbuffer_t));
    if (!ringbuffers[b]) {
      rotter_fatal("Cannot allocate memory for ringbuffer %c structure.", label);
      return -1;
    }

    ringbuffers[b]->label = label;
    ringbuffers[b]->period_start = 0;
    ringbuffers[b]->file_handle = NULL;
    ringbuffers[b]->overflow = 0;
    ringbuffers[b]->close_file = 0;
    ringbuffers[b]->buffer[0] = NULL;
    ringbuffers[b]->buffer[1] = NULL;

    for(c=0; c<channels; c++) {
      ringbuffers[b]->buffer[c] = jack_ringbuffer_create( ringbuffer_size );
      if (!ringbuffers[b]->buffer[c]) {
        rotter_fatal("Cannot create ringbuffer buffer %c%d.", label, c);
        return -1;
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
        rotter_close_file(ringbuffers[b]);
        ringbuffers[b]->file_handle = NULL;
      }

      free(ringbuffers[b]);
    }
  }

  return 0;
}

static int init_tmpbuffers(int sample_count)
{
  size_t buffer_size = sample_count * sizeof(jack_default_audio_sample_t);
  int c;

  for(c=0; c<2; c++) {
    tmp_buffer[c] = (jack_default_audio_sample_t*)malloc(buffer_size);
    if (!tmp_buffer[c]) {
      rotter_fatal( "Failed to allocate memory for temporary buffer %d", c);
      return -1;
    }
  }

  return 0;
}

static int deinit_tmpbuffers()
{
  int c;

  for(c=0; c<2; c++) {
    if (tmp_buffer[c])
      free(tmp_buffer[c]);
  }

  return 0;
}

static char* rotter_str_tolower( char* str )
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
  printf("   -p <secs>     Period of each archive file (in seconds, default %d)\n", DEFAULT_ARCHIVE_PERIOD_SECONDS);
  printf("   -d <hours>    Delete files in directory older than this\n");
  printf("   -R <secs>     Length of the ring buffer (in seconds)\n");
  printf("   -L <layout>   File layout (default 'hierarchy')\n");
  printf("   -j            Don't automatically start jackd\n");
  printf("   -u            Use UTC rather than local time in filenames\n");
  printf("   -v            Enable verbose mode\n");
  printf("   -q            Enable quiet mode\n");

  printf("\nSupported file layouts:\n");
  printf("   flat          /root_directory/YYYY-MM-DD-HH.suffix\n");
  printf("   hierarchy     /root_directory/YYYY/MM/DD/HH/archive.suffix\n");
  printf("   combo         /root_directory/YYYY/MM/DD/HH/YYYY-MM-DD-HH.suffix\n");
  printf("   dailydir      /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH.suffix\n");
  printf("   accurate      /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-uu.suffix\n");
  printf("\n");
  printf("A custom file layout may be specified using a strftime-style format string,\n");
  printf("for example: -L \"%%Y-%%m-%%d/studio-1/%%H%%M.flac\"\n");

  // Display the available audio output formats
  printf("\nSupported audio output formats:\n");
  for(i=0; format_list[i].name; i++) {
    printf("   %-6s        %s", format_list[i].name, format_list[i].desc );
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
  const char *format_name = NULL;
  int bitrate = DEFAULT_BITRATE;
  float sleep_time = 0;
  int i,opt;

  // Make STDOUT unbuffered
  setbuf(stdout, NULL);

  // Parse Switches
  while ((opt = getopt(argc, argv, "al:r:n:N:p:jf:b:d:c:R:L:uvqh")) != -1) {
    switch (opt) {
      case 'a':  autoconnect = 1; break;
      case 'l':  connect_left = optarg; break;
      case 'r':  connect_right = optarg; break;
      case 'n':  client_name = optarg; break;
      case 'N':  archive_name = optarg; break;
      case 'p':  archive_period_seconds = atol(optarg); break;
      case 'j':  jack_opt |= JackNoStartServer; break;
      case 'f':  format_name = rotter_str_tolower(optarg); break;
      case 'b':  bitrate = atoi(optarg); break;
      case 'd':  delete_hours = atoi(optarg); break;
      case 'c':  channels = atoi(optarg); break;
      case 'R':  rb_duration = atof(optarg); break;
      case 'L':  file_layout = optarg; break;
      case 'u':  utc = 1; break;
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

    if (rotter_directory_exists(root_directory)) {
      rotter_debug("Root directory: %s", root_directory);
    } else {
      rotter_fatal("Root directory does not exist: %s", root_directory);
      goto cleanup;
    }
  }

  // Search for the selected output format
  if (format_name) {
    for(i=0; format_list[i].name; i++) {
      if (strcmp( format_list[i].name, format_name ) == 0) {
        // Found desired format
        output_format = &format_list[i];
        rotter_debug("User selected [%s] '%s'.",  output_format->name,  output_format->desc);
        break;
      }
    }
    if (output_format==NULL) {
      rotter_fatal("Failed to find format [%s], please check the supported format list.", format_name);
      goto cleanup;
    }
  } else {
    output_format = &format_list[0];
  }

  // Initialise JACK
  if (init_jack( client_name, jack_opt )) {
    rotter_debug("Failed to initialise Jack client.");
    goto cleanup;
  }

  // Create ring buffers
  if (init_ringbuffers()) {
    rotter_debug("Failed to initialise ring buffers.");
    goto cleanup;
  }

  // Create temporary buffer for reading samples into
  if (init_tmpbuffers(output_format->samples_per_frame)) {
    rotter_debug("Failed to initialise temporary buffers.");
    goto cleanup;
  }

  // Initialise encoder
  encoder = output_format->initfunc(output_format, channels, bitrate);
  if (encoder==NULL) {
    rotter_debug("Failed to initialise encoder.");
    goto cleanup;
  }

  // Activate JACK
  if (jack_activate(client)) {
    rotter_fatal("Cannot activate JACK client.");
    goto cleanup;
  }

  // Setup signal handlers
  signal(SIGTERM, rotter_termination_handler);
  signal(SIGINT, rotter_termination_handler);
  signal(SIGHUP, rotter_termination_handler);

  // Auto-connect our input ports ?
  if (autoconnect) autoconnect_jack_ports( client );
  if (connect_left) connect_jack_port( connect_left, inport[0] );
  if (connect_right && channels == 2) connect_jack_port( connect_right, inport[1] );

  // Calculate period to wait when there is no audio to process
  sleep_time = (2.0f * output_format->samples_per_frame / jack_get_sample_rate( client ));
  rotter_debug("Sleep period is %dms.", (int)(sleep_time * 1000));


  while( rotter_run_state == ROTTER_STATE_RUNNING ) {
    int samples_processed = rotter_process_audio(encoder);
    if (samples_processed <= 0) {
      usleep(sleep_time * 1000000);
    }

    deletefiles_cleanup_child();
  }


cleanup:

  // Clean up JACK
  deinit_jack();

  // Free buffers and close files
  deinit_tmpbuffers();
  deinit_ringbuffers();

  // Shut down encoder
  if (encoder)
    encoder->deinit();

  // Did something go wrong?
  if (rotter_run_state == ROTTER_STATE_QUITING) {
    return EXIT_SUCCESS;
  } else {
    return EXIT_FAILURE;
  }
}
