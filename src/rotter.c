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
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>
#include <errno.h>
#include <stdarg.h>

#include "rotter.h"
#include "config.h"



// ------- Globals -------
jack_port_t *inport[2] = {NULL, NULL};
jack_ringbuffer_t *ringbuffer[2] = {NULL, NULL};
jack_client_t *client = NULL;

int quiet = 0;
int verbose = 0;
int channels = DEFAULT_CHANNELS;
float rb_duration = DEFAULT_RB_LEN;
int running = 1;



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
            fprintf(stderr, "Failed to write to ring ruffer.\n");
            running = 0;
            return 1;
         }
	}


	// Success
	return 0;
}
					


static
void shutdown_callback_jack(void *arg)
{
	fprintf(stderr, "Warning: Rotter quitting because jackd is shutting down.\n" );
	running = 0;
}


void connect_jack_port( jack_port_t *port, const char* in )
{
	const char* out = jack_port_name( port );
	int err;
		
	if (!quiet) printf("Connecting %s to %s\n", out, in);
	
	if ((err = jack_connect(client, out, in)) != 0) {
		fprintf(stderr, "connect_jack_port(): failed to jack_connect() ports: %d\n",err);
		exit(1);
	}
}


// crude way of automatically connecting up jack ports
void autoconnect_jack_ports( jack_client_t* client )
{
	const char **all_ports;
	unsigned int ch=0;
	int i;

	// Get a list of all the jack ports
	all_ports = jack_get_ports(client, NULL, NULL, JackPortIsInput);
	if (!all_ports) {
		fprintf(stderr, "autoconnect_jack_ports(): jack_get_ports() returned NULL.");
		exit(1);
	}
	
	// Step through each port name
	for (i = 0; all_ports[i]; ++i) {
		
		// Connect the port
		connect_jack_port( inport[ch], all_ports[i] );
		
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
		fprintf(stderr, "Failed to start jack client: 0x%x\n", status);
		exit(1);
	}
	if (!quiet) printf("JACK client registered as '%s'.\n", jack_get_client_name( client ) );


	// Create our input port(s)
	if (channels==1) {
		if (!(inport[0] = jack_port_register(client, "mono", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			fprintf(stderr, "Cannot register mono input port.\n");
			exit(1);
		}
	} else {
		if (!(inport[0] = jack_port_register(client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			fprintf(stderr, "Cannot register left input port.\n");
			exit(1);
		}
		
		if (!(inport[1] = jack_port_register(client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			fprintf(stderr, "Cannot register left input port.\n");
			exit(1);
		}
	}
	
	// Create ring buffers
	ringbuffer_size = jack_get_sample_rate( client ) * rb_duration * sizeof(float);
	if (verbose) printf("Size of the ring buffers is %2.2f seconds (%d bytes).\n", rb_duration, (int)ringbuffer_size );
	for(i=0; i<2; i++) {
		if (!(ringbuffer[i] = jack_ringbuffer_create( ringbuffer_size ))) {
			fprintf(stderr, "Cannot create ringbuffer.\n");
			exit(1);
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
		case SIGHUP:	fprintf(stderr, "Got hangup signal.\n"); break;
		case SIGTERM:	fprintf(stderr, "Got termination signal.\n"); break;
		case SIGINT:	fprintf(stderr, "Got interupt signal.\n"); break;
	}
	
	// Set state to Quit
	//set_state( MADJACK_STATE_QUIT );
	
	signal(signum, termination_handler);
}




// Display how to use this program
static
void usage()
{
	printf("%s version %s\n\n", PACKAGE_NAME, PACKAGE_VERSION);
	printf("Usage: %s [options] <directory>\n", PACKAGE_NAME);
	printf("   -a            Automatically connect JACK ports\n");
	printf("   -b <bitrate>  Bitrate of recording\n");
	printf("   -c <channels> Number of channels\n");
	printf("   -n <name>     Name for this JACK client\n");
	printf("   -j            Don't automatically start jackd\n");
	printf("   -v            Enable verbose mode\n");
	printf("   -q            Enable quiet mode\n");
	printf("\n");
	exit(1);
}



int main(int argc, char *argv[])
{
	int autoconnect = 0;
	char *root_directory = NULL;
	jack_options_t jack_opt = JackNullOption;
	char *client_name = DEFAULT_CLIENT_NAME;
	char *connect_left = NULL;
	char *connect_right = NULL;
	int bitrate = DEFAULT_BITRATE;
	int opt;

	// Make STDOUT unbuffered
	setbuf(stdout, NULL);

	// Parse Switches
	while ((opt = getopt(argc, argv, "al:r:n:jd:c:R:vqh")) != -1) {
		switch (opt) {
			case 'a':  autoconnect = 1; break;
			case 'l':  connect_left = optarg; break;
			case 'r':  connect_right = optarg; break;
			case 'n':  client_name = optarg; break;
			case 'j':  jack_opt |= JackNoStartServer; break;
			case 'b':  bitrate = atoi(optarg); break;
			case 'c':  channels = atoi(optarg); break;
			case 'R':  rb_duration = atof(optarg); break;
			case 'v':  verbose = 1; break;
			case 'q':  quiet = 1; break;
			default:  usage(); break;
		}
	}
	
	// Validate parameters
	if (quiet && verbose) {
    	fprintf(stderr, "Can't be quiet and verbose at the same time.\n");
    	usage();
	}

	// Check the number of channels
	if (channels!=1 && channels!=2) {
		fprintf(stderr, "Number of channels should be either 1 or 2.\n");
		usage();
	}

	// Check remaining arguments
    argc -= optind;
    argv += optind;
    if (argc!=1) {
    	fprintf(stderr, "%s requires a root directory argument.\n", PACKAGE_NAME);
    	usage();
	} else {
		root_directory = argv[0];
		if (verbose) fprintf(stderr, "Root directory: %s\n", root_directory);
	}

	// Initialise JACK
	init_jack( client_name, jack_opt );
	
	// Initialise encoder
	init_twolame( channels, bitrate );


	// Activate JACK
	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate JACK client.\n");
		exit(1);
	}

	// Setup signal handlers
	signal(SIGTERM, termination_handler);
	signal(SIGINT, termination_handler);
	signal(SIGHUP, termination_handler);
	
	
	// Auto-connect our input ports ?
	if (autoconnect) autoconnect_jack_ports( client );
	if (connect_left) connect_jack_port( inport[0], connect_left );
	if (connect_right && channels == 2) connect_jack_port( inport[1], connect_right );


	// Encode stuff
	sleep( 10 );


	// Clean up JACK
	finish_jack();
	
	
	return 0;
}

