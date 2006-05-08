/*

	rotter.c
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

#include "rotter.h"
#include "config.h"




// ------- Globals -------
jack_port_t *inport[2] = {NULL, NULL};
jack_ringbuffer_t *ringbuffer[2] = {NULL, NULL};
jack_client_t *client = NULL;

int quiet = 0;							// Only display error messages
int verbose = 0;						// Increase number of logging messages
int hierarchy = 0;						// Flat files or folder hierarchy ?
int channels = DEFAULT_CHANNELS;		// Number of input channels
float rb_duration = DEFAULT_RB_LEN;		// Duration of ring buffer
char *root_directory = NULL;			// Root directory of archives
time_t file_start = 0;					// Start time of the open file
int running = 1;						// True while still running



// Callback called by JACK when audio is available
static
int callback_jack(jack_nframes_t nframes, void *arg)
{
    size_t to_write = sizeof (jack_default_audio_sample_t) * nframes;
	unsigned int c;
	
	for (c=0; c < channels; c++)
	{	
        char *buf  = (char*)jack_port_get_buffer(inport[c], nframes);
        size_t len = jack_ringbuffer_write(ringbuffer[c], buf, to_write);
        if (len < to_write) {
            rotter_fatal("Failed to write to ring ruffer.");
            return 1;
         }
	}


	// Success
	return 0;
}
					


static
void shutdown_callback_jack(void *arg)
{
	rotter_error("Rotter quitting because jackd is shutting down." );
}


static
void connect_jack_port( const char* out, jack_port_t *port )
{
	const char* in = jack_port_name( port );
	int err;
		
	rotter_info("Connecting '%s' to '%s'", out, in);
	
	if ((err = jack_connect(client, out, in)) != 0) {
		rotter_fatal("connect_jack_port(): failed to jack_connect() ports: %d",err);
	}
}


// crude way of automatically connecting up jack ports
static
void autoconnect_jack_ports( jack_client_t* client )
{
	const char **all_ports;
	unsigned int ch=0;
	int i;

	// Get a list of all the jack ports
	all_ports = jack_get_ports(client, NULL, NULL, JackPortIsOutput);
	if (!all_ports) {
		rotter_fatal("autoconnect_jack_ports(): jack_get_ports() returned NULL.");
	}
	
	// Step through each port name
	for (i = 0; all_ports[i]; ++i) {
		
		// Connect the port
		connect_jack_port( all_ports[i], inport[ch] );
		
		// Found enough ports ?
		if (++ch >= channels) break;
	}
	
	free( all_ports );
}


static
void init_jack( const char* client_name, jack_options_t jack_opt ) 
{
	jack_status_t status;
	size_t ringbuffer_size = 0;
	int i;

	// Register with Jack
	if ((client = jack_client_open(client_name, jack_opt, &status)) == 0) {
		rotter_fatal("Failed to start jack client: 0x%x", status);
	}
	rotter_info( "JACK client registered as '%s'.", jack_get_client_name( client ) );


	// Create our input port(s)
	if (channels==1) {
		if (!(inport[0] = jack_port_register(client, "mono", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			rotter_fatal("Cannot register mono input port.");
		}
	} else {
		if (!(inport[0] = jack_port_register(client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			rotter_fatal("Cannot register left input port.");
		}
		
		if (!(inport[1] = jack_port_register(client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			rotter_fatal( "Cannot register left input port.");
		}
	}
	
	// Create ring buffers
	ringbuffer_size = jack_get_sample_rate( client ) * rb_duration * sizeof(float);
	rotter_debug("Size of the ring buffers is %2.2f seconds (%d bytes).", rb_duration, (int)ringbuffer_size );
	for(i=0; i<channels; i++) {
		if (!(ringbuffer[i] = jack_ringbuffer_create( ringbuffer_size ))) {
			rotter_fatal("Cannot create ringbuffer.");
		}
	}
	
	// Register shutdown callback
	jack_on_shutdown(client, shutdown_callback_jack, NULL );

	// Register callback
	jack_set_process_callback(client, callback_jack, NULL);
	
}


static
void finish_jack()
{
	// Leave the Jack graph
	jack_client_close(client);
	
	// Free up the ring buffers
	jack_ringbuffer_free( ringbuffer[0] );
	jack_ringbuffer_free( ringbuffer[1] );
}



static void
termination_handler (int signum)
{
	switch(signum) {
		case SIGHUP:	rotter_info("Got hangup signal."); break;
		case SIGTERM:	rotter_info("Got termination signal."); break;
		case SIGINT:	rotter_info("Got interupt signal."); break;
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
	
	// If fatal then exit
	if (level == ROTTER_FATAL) exit( -1 );
	
}




// Returns unix timestamp for the start of this hour
static time_t start_of_hour()
{
	struct tm tm;
	time_t now = time(NULL);
	
	// Break down the time
	localtime_r( &now, &tm );

	// Set minutes and seconds to 0
	tm.tm_min = 0;
	tm.tm_sec = 0;
	
	return mktime( &tm );
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
	
	// Create the full file path
	snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d-%2.2d-%2.2d-%2.2d%s",
				root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );

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
	snprintf( filepath, MAX_FILEPATH_LEN, "%s/%4.4d/%2.2d/%2.2d/%2.2d/archive%s",
				root_directory, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, suffix );

	return filepath;
}



static void main_loop( encoder_funcs_t* encoder )
{
	
	while( running ) {
		time_t this_hour = start_of_hour();
		

		// Time to change file?
		if (file_start != this_hour) {
			char* filepath;
			if (hierarchy) {
				filepath = time_to_filepath_hierarchy( this_hour, encoder->file_suffix );
			} else {
				filepath = time_to_filepath_flat( this_hour, encoder->file_suffix );
			}
			
			rotter_info( "Starting new archive file: %s", filepath );
			
			// Close the old file
			encoder->close();
			
			// Open the new file
			if (encoder->open( filepath )) break;
			
			file_start = this_hour;
			free(filepath);
		}
		

		// Encode a frame of audio
		int result = encoder->encode();
		if (result == 0) {
			// Sleep for 1/4 of the ringbuffer duration
			usleep( (rb_duration/4) * 1000000 );
		} else if (result < 0) {
			rotter_fatal("Shutting down, due to encoding error.");
			break;
		}
		
	}

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
	printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
	printf("Usage: %s [options] <directory>\n", PACKAGE_NAME);
	printf("   -a            Automatically connect JACK ports\n");
	printf("   -f <format>   Format of recording [mp2/mp3]\n");
	printf("   -b <bitrate>  Bitrate of recording\n");
	printf("   -c <channels> Number of channels\n");
	printf("   -n <name>     Name for this JACK client\n");
	printf("   -H            Create folder hierarchy instead of flat files\n");
	printf("   -j            Don't automatically start jackd\n");
	printf("   -v            Enable verbose mode\n");
	printf("   -q            Enable quiet mode\n");
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
	char *format = DEFAULT_FORMAT;
	int bitrate = DEFAULT_BITRATE;
	encoder_funcs_t* encoder = NULL;
	int opt;

	// Make STDOUT unbuffered
	setbuf(stdout, NULL);

	// Parse Switches
	while ((opt = getopt(argc, argv, "al:r:n:jf:b:c:R:Hvqh")) != -1) {
		switch (opt) {
			case 'a':  autoconnect = 1; break;
			case 'l':  connect_left = optarg; break;
			case 'r':  connect_right = optarg; break;
			case 'n':  client_name = optarg; break;
			case 'j':  jack_opt |= JackNoStartServer; break;
			case 'f':  format = str_tolower(optarg); break;
			case 'b':  bitrate = atoi(optarg); break;
			case 'c':  channels = atoi(optarg); break;
			case 'R':  rb_duration = atof(optarg); break;
			case 'H':  hierarchy = 1; break;
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

	
	// Initialise encoder
	if (strcmp( "mp2", format) == 0) {
		encoder = init_twolame( channels, bitrate );
	} else {
		rotter_error("Don't know how to encode to format '%s'.", format);
	}
	
	// Failure?
	if (encoder==NULL) {
		rotter_debug("Failed to initialise encoder.");
		finish_jack();
		exit(-1);
	}

	// Activate JACK
	if (jack_activate(client)) rotter_fatal("Cannot activate JACK client.");

	// Setup signal handlers
	signal(SIGTERM, termination_handler);
	signal(SIGINT, termination_handler);
	signal(SIGHUP, termination_handler);
	
	
	// Auto-connect our input ports ?
	if (autoconnect) autoconnect_jack_ports( client );
	if (connect_left) connect_jack_port( connect_left, inport[0] );
	if (connect_right && channels == 2) connect_jack_port( connect_right, inport[1] );


	
	main_loop( encoder );
	
	
	// Close the output file
	encoder->close();

	// Shut down encoder
	encoder->shutdown();

	// Clean up JACK
	finish_jack();
	
	
	return 0;
}

