/*

	rotter.h
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


#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <twolame.h>


#ifndef _ROTTER_H_
#define _ROTTER_H_


// ------- Constants -------
#define DEFAULT_RB_LEN			(2.0)
#define READ_BUFFER_SIZE		(2048)
#define DEFAULT_CLIENT_NAME		"rotter"
#define DEFAULT_BITRATE			(160)
#define DEFAULT_CHANNELS		(2)


// ------- Globals ---------
extern jack_port_t *inport[2];
extern jack_ringbuffer_t *ringbuffer[2];
extern jack_client_t *client;

extern int quiet;
extern int verbose;
extern float rb_duration;



typedef struct encoder_s
{
	void *encoder;
	
	void (*encode)();
	void (*shutdown)();

} encoder_t;


// In twolame.c
int init_twolame( int channels, int bitrate );



#endif
