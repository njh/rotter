/*

	twolame.c
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

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <getopt.h>
#include <errno.h>
#include <stdarg.h>

#include "rotter.h"
#include "config.h"



int init_twolame( int channels, int bitrate )
{
	twolame_options *twolame_opts = twolame_init();
	if (twolame_opts==NULL) {
		fprintf(stderr, "TwoLAME error: failed to initialise\n.");
		return -1;
	}
	
	if ( 0 > twolame_set_num_channels( twolame_opts, channels ) ) {
		fprintf(stderr, "TwoLAME error: failed to set number of channels\n.");
		return -1;
    }

	if ( 0 > twolame_set_in_samplerate( twolame_opts, jack_get_sample_rate( client ) )) {
		fprintf(stderr, "TwoLAME error: failed to set input samplerate\n.");
		return -1;
    }

	if ( 0 > twolame_set_out_samplerate( twolame_opts, jack_get_sample_rate( client ) )) {
		fprintf(stderr, "TwoLAME error: failed to set output samplerate\n.");
		return -1;
    }

	if ( 0 > twolame_set_brate( twolame_opts, bitrate) ) {
		fprintf(stderr, "TwoLAME error: failed to set bitrate\n.");
		return -1;
	}

	if ( 0 > twolame_init_params( twolame_opts ) ) {
		fprintf(stderr, "TwoLAME error: failed to initialize parameters\n.");
		return -1;
    }
    
    if (verbose) twolame_print_config( twolame_opts );

	// Success
	return 0;
}



int encode_twolame()
{

	/*
	
	    jack_nframes_t samples         = len / 2 / getChannel();
    jack_nframes_t samples_read[2] = {0,0};
    short        * output          = (short*)buf;
    unsigned int c, n;

    if ( !isOpen() ) {
        return 0;
    }


    // Ensure the temporary buffer is big enough
    tmp_buffer = (jack_default_audio_sample_t*)realloc(tmp_buffer,
                             samples * sizeof( jack_default_audio_sample_t ) );
    if (!tmp_buffer) {
        throw Exception( __FILE__, __LINE__, "realloc on tmp_buffer failed");
    }


    for (c=0; c<getChannel(); c++)
    {    
        // Copy frames from ring buffer to temporary buffer
        // and then convert samples to output buffer
        int bytes_read = jack_ringbuffer_read(rb[c],
                                             (char*)tmp_buffer,
                              samples * sizeof( jack_default_audio_sample_t ));
        samples_read[c] = bytes_read / sizeof( jack_default_audio_sample_t );
        

        // Convert samples from float to short and put in output buffer
        for(n=0; n<samples_read[c]; n++) {
            int tmp = lrintf(tmp_buffer[n] * 32768.0f);
            if (tmp > SHRT_MAX) {
                output[n*getChannel()+c] = SHRT_MAX;
            } else if (tmp < SHRT_MIN) {
                output[n*getChannel()+c] = SHRT_MIN;
            } else {
                output[n*getChannel()+c] = (short) tmp;
            }
        }
    }

    // Didn't get as many samples as we wanted ?
    if (getChannel() == 2 && samples_read[0] != samples_read[1]) {
        Reporter::reportEvent( 2,
                              "Warning: Read a different number of samples "
                              "for left and right channels");
    }

    // Return the number of bytes put in the output buffer
    return samples_read[0] * 2 * getChannel();
    
   */

/*
	int twolame_encode_buffer(
 twolame_options *glopts,   // the set of options you're using
 const short int leftpcm[], // the left and right audio channels
 const short int rightpcm[],
 int num_samples,           // the number of samples in each channel
 unsigned char *mp2buffer,  // a pointer to a buffer for the MP2 audio data
                            // NB User must allocate space!
 int mp2buffer_size);       // The size of the mp2buffer that the user allocated
 int *mp2fill_size);
*/



}


int close_twolame()
{
	// twolame_close( twolame_opts );

}




