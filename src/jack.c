/*

  jack.c
  
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
#include <limits.h>

#include "config.h"
#include "rotter.h"



// ------- Globals -------
jack_port_t *inport[2] = {NULL, NULL};
jack_client_t *client = NULL;



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


/* Callback called by JACK when audio is available
   Use as little CPU time as possible, just copy accross the audio
   into the ring buffer
*/
static
int callback_jack(jack_nframes_t nframes, void *arg)
{
  size_t to_write = sizeof(jack_default_audio_sample_t) * nframes;
  time_t this_hour = start_of_hour();
  unsigned int c;

  // Time to swap ring buffers?
  if (active_ringbuffer == NULL || active_ringbuffer->hour_start != this_hour) {
    if (active_ringbuffer) {
      active_ringbuffer->close_file = 1;
    }
    if (active_ringbuffer == ringbuffers[0]) {
      active_ringbuffer = ringbuffers[1];
    } else {
      active_ringbuffer = ringbuffers[0];
    }
    active_ringbuffer->hour_start = this_hour;
  }

  for (c=0; c < channels; c++)
  { 
    size_t space = jack_ringbuffer_write_space(active_ringbuffer->buffer[c]);
    if (space < to_write) {
      // Glitch in audio is preferable to a fatal error or ring buffer corruption
      active_ringbuffer->overflow = 1;
      return 0;
    }
  }
  
  for (c=0; c < channels; c++)
  { 
    char *buf  = (char*)jack_port_get_buffer(inport[c], nframes);
    size_t len = jack_ringbuffer_write(active_ringbuffer->buffer[c], buf, to_write);
    if (len < to_write) {
      rotter_fatal("Failed to write to ring buffer.");
      return 1;
    }
  }

  // Success
  return 0;
}
          

// Callback called by JACK when jackd is shutting down
static
void shutdown_callback_jack(void *arg)
{
  rotter_error("Rotter quitting because jackd is shutting down." );
  
  // Signal the main thead to stop
  running = 0;
}


// Connect one Jack port to another
void connect_jack_port( const char* out, jack_port_t *port )
{
  const char* in = jack_port_name( port );
  int err;
    
  rotter_info("Connecting '%s' to '%s'", out, in);
  
  if ((err = jack_connect(client, out, in)) != 0) {
    rotter_fatal("connect_jack_port(): failed to jack_connect() ports: %d",err);
  }
}


// Crude way of automatically connecting up jack ports
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


// Initialise Jack related stuff
void init_jack( const char* client_name, jack_options_t jack_opt ) 
{
  jack_status_t status;

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
  
  // Register shutdown callback
  jack_on_shutdown(client, shutdown_callback_jack, NULL );

  // Register callback
  jack_set_process_callback(client, callback_jack, NULL);
  
}


// Shut down jack related stuff
void deinit_jack()
{
  // FIXME: jack_deactivate
  
  // Leave the Jack graph
  jack_client_close(client);
  
}


