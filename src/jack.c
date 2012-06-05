/*

  jack.c

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
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>

#include "config.h"
#include "rotter.h"



// ------- Globals -------
jack_port_t *inport[2] = {NULL, NULL};
jack_client_t *client = NULL;
rotter_ringbuffer_t *active_ringbuffer = NULL;

// Given unix timestamp for current time
// Returns unix timestamp for the start of this archive period
// relies on global archive_period_seconds variable
static time_t start_of_hour(time_t now)
{
  return (floor(now / archive_period_seconds) * archive_period_seconds);
}


static int write_to_ringbuffer(rotter_ringbuffer_t *rb, jack_nframes_t start,
                               jack_nframes_t nframes)
{
  size_t to_write = sizeof(jack_default_audio_sample_t) * nframes;
  unsigned int c;

  if (nframes <= 0)
    return 0;

  for (c=0; c < channels; c++)
  {
    size_t space = jack_ringbuffer_write_space(rb->buffer[c]);
    if (space < to_write) {
      // Glitch in audio is preferable to a fatal error or ring buffer corruption
      rb->overflow = 1;
      return 0;
    }
  }

  for (c=0; c < channels; c++)
  {
    jack_default_audio_sample_t *buf = jack_port_get_buffer(inport[c], nframes);
    size_t len = 0;

    len = jack_ringbuffer_write(rb->buffer[c], (char*)&buf[start], to_write);
    if (len < to_write) {
      rotter_fatal("Failed to write to ring buffer.");
      return 1;
    }
  }

  // Success
  return 0;
}


/* Callback called by JACK when audio is available
   Use as little CPU time as possible, just copy accross the audio
   into the ring buffer
*/
static
int callback_jack(jack_nframes_t nframes, void *arg)
{
  jack_nframes_t frames_until_whole_second = 0;
  jack_nframes_t read_pos = 0;
  time_t this_hour;
  struct timeval tv;

  // Get the current time
  if (gettimeofday(&tv, NULL)) {
    rotter_fatal("Failed to gettimeofday(): %s", strerror(errno));
    return 1;
  }

  // FIXME: this won't work if rotter is started *just* before the hour
  if (active_ringbuffer) {
    unsigned int duration;
    int result;

    // Calculate the number of frames until we have a whole number of seconds
    // FIXME: what if the callback buffer contains over 1 second of audio?
    frames_until_whole_second = ceil(jack_get_sample_rate( client ) *
                                ((double)(1000000 - tv.tv_usec) / 1000000));

    if (frames_until_whole_second < nframes) {
      result = write_to_ringbuffer(active_ringbuffer, read_pos, frames_until_whole_second);
      if (result)
        return result;

      // Calculate the duration of the audio that we wrote
      // and add it on to the current time
      duration = ((double)frames_until_whole_second /
                 jack_get_sample_rate( client )) * 1000000;
      tv.tv_usec += (duration - 1000000);
      tv.tv_sec += 1;

      nframes -= frames_until_whole_second;
      read_pos += frames_until_whole_second;
    }
  }


  // Time to swap ring buffers, if we are now in a new hour period
  this_hour = start_of_hour(tv.tv_sec);
  if (active_ringbuffer == NULL || active_ringbuffer->hour_start != this_hour) {
    if (active_ringbuffer) {
      active_ringbuffer->close_file = 1;
    }
    if (active_ringbuffer == ringbuffers[0]) {
      active_ringbuffer = ringbuffers[1];
    } else {
      active_ringbuffer = ringbuffers[0];
    }
    active_ringbuffer->file_start = tv;
    active_ringbuffer->hour_start = this_hour;
  }

  // Finally, write any frames after the 1 second boundary
  return write_to_ringbuffer(active_ringbuffer, read_pos, nframes);
}


// Callback called by JACK when jackd is shutting down
static
void shutdown_callback_jack(void *arg)
{
  rotter_error("Rotter quitting because jackd is shutting down." );

  // Signal the main thead to stop
  rotter_run_state = ROTTER_STATE_QUITING;
}


// Connect one Jack port to another
int connect_jack_port( const char* out, jack_port_t *port )
{
  const char* in = jack_port_name( port );
  int err;

  rotter_info("Connecting '%s' to '%s'", out, in);

  if ((err = jack_connect(client, out, in)) != 0) {
    rotter_fatal("connect_jack_port(): failed to jack_connect() ports: %d", err);
    return err;
  }

  // Success
  return 0;
}


// Crude way of automatically connecting up jack ports
int autoconnect_jack_ports( jack_client_t* client )
{
  const char **all_ports;
  unsigned int ch=0;
  int i;

  // Get a list of all the jack ports
  all_ports = jack_get_ports(client, NULL, NULL, JackPortIsOutput);
  if (!all_ports) {
    rotter_fatal("autoconnect_jack_ports(): jack_get_ports() returned NULL.");
    return -1;
  }

  // Step through each port name
  for (i = 0; all_ports[i]; ++i) {
    // Connect the port
    if (connect_jack_port( all_ports[i], inport[ch] )) {
      return -1;
    }

    // Found enough ports ?
    if (++ch >= channels) break;
  }

  free( all_ports );

  return 0;
}


// Initialise Jack related stuff
int init_jack( const char* client_name, jack_options_t jack_opt )
{
  jack_status_t status;

  // Register with Jack
  if ((client = jack_client_open(client_name, jack_opt, &status)) == 0) {
    rotter_fatal("Failed to start jack client: 0x%x", status);
    return -1;
  }
  rotter_info( "JACK client registered as '%s'.", jack_get_client_name( client ) );


  // Create our input port(s)
  if (channels==1) {
    if (!(inport[0] = jack_port_register(client, "mono", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
      rotter_fatal("Cannot register mono input port.");
      return -1;
    }
  } else {
    if (!(inport[0] = jack_port_register(client, "left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
      rotter_fatal("Cannot register left input port.");
      return -1;
    }

    if (!(inport[1] = jack_port_register(client, "right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
      rotter_fatal( "Cannot register left input port.");
      return -1;
    }
  }

  // Register shutdown callback
  jack_on_shutdown(client, shutdown_callback_jack, NULL );

  // Register callback
  if (jack_set_process_callback(client, callback_jack, NULL)) {
    rotter_fatal( "Failed to set Jack process callback.");
    return -1;
  }

  // Success
  return 0;
}


// Shut down jack related stuff
int deinit_jack()
{
  if (client) {
    rotter_debug("Stopping Jack client.");

    if (jack_deactivate(client)) {
      rotter_error("Failed to de-activate Jack");
    }

    if (jack_client_close(client)) {
      rotter_error("Failed to close Jack client");
    }
  }

  return 0;
}
