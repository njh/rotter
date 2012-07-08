Rotter
======
Nicholas J. Humfrey <njh@aelius.com>

For the latest stable version of Rotter, please see:
http://www.aelius.com/njh/rotter/


What is Rotter ?
----------------

Rotter is a [Recording of Transmission] / Audio Logger for [JACK]. It was
designed for use by radio stations, who are legally required to keep
a recording of all their output. Rotter runs continuously, writing to
a new file every at regular intervals (typically every hour).

Rotter can output files in various different structures, either all files
in a single directory or create a directory structure:

    flat:      /root_directory/YYYY-MM-DD-HH.suffix
    hierarchy: /root_directory/YYYY/MM/DD/HH/archive.suffix
    combo:     /root_directory/YYYY/MM/DD/HH/YYYY-MM-DD-HH.suffix
    dailydir:  /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH.suffix
    accurate:  /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-uu.suffix

The advantage of using a folder hierarchy is that you can store related
files in the hour's directory. The "accurate" structure stores the
start time of the hourly file to an accuracy of one hundredth of a
second.

There is also an option to write to a custom layout using strftime() the
based string format.



Usage
-----

    Usage: rotter [options] <root_directory>
       -a            Automatically connect JACK ports
       -l <port>     Connect the left input to this port
       -r <port>     Connect the right input to this port
       -f <format>   Format of recording (see list below)
       -b <bitrate>  Bitrate of recording (bitstream formats only)
       -c <channels> Number of channels
       -n <name>     Name for this JACK client
       -N <filename> Name for archive files (default 'archive')
       -p <secs>     Period of each archive file (in seconds, default 3600)
       -d <hours>    Delete files in directory older than this
       -R <secs>     Length of the ring buffer (in seconds, default 2.00)
       -L <layout>   File layout (default 'hierarchy')
       -j            Don't automatically start jackd
       -u            Use UTC rather than local time in filenames
       -v            Enable verbose mode
       -q            Enable quiet mode

    Supported file layouts:
       flat          /root_directory/YYYY-MM-DD-HH.suffix
       hierarchy     /root_directory/YYYY/MM/DD/HH/archive.suffix
       combo         /root_directory/YYYY/MM/DD/HH/YYYY-MM-DD-HH.suffix
       dailydir      /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH.suffix
       accurate      /root_directory/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-uu.suffix

    A custom file layout may be specified using a strftime-style format string,
    for example: -L "%Y-%m-%d/studio-1/%H%M.flac"

    Supported audio output formats:
       mp3           MPEG Audio Layer 3   [Default]
       mp2           MPEG Audio Layer 2
       aiff          AIFF (Apple/SGI 16 bit PCM)
       aiff32        AIFF (Apple/SGI 32 bit float)
       au            AU (Sun/Next 16 bit PCM)
       au32          AU (Sun/Next 32 bit float)
       caf           CAF (Apple 16 bit PCM)
       caf32         CAF (Apple 32 bit float)
       flac          FLAC 16 bit
       vorbis        Ogg Vorbis
       wav           WAV (Microsoft 16 bit PCM)
       wav32         WAV (Microsoft 32 bit float)


Example Run
-----------

    rotter -a -f mp3 -d 1000 -b 128 -v /var/achives
    [DEBUG]  Wed Jun 21 22:54:19 2006  Root directory: /var/archives
    [INFO]   Wed Jun 21 22:54:19 2006  JACK client registered as 'rotter'.
    [DEBUG]  Wed Jun 21 22:54:19 2006  Size of the ring buffers is 2.00 seconds (352800 bytes).
    [INFO]   Wed Jun 21 22:54:19 2006  Encoding using liblame version 3.96.1.
    [DEBUG]  Wed Jun 21 22:54:19 2006    Input: 44100 Hz, 2 channels
    [DEBUG]  Wed Jun 21 22:54:19 2006    Output: MPEG-1 Layer 3, 160 kbps, Joint Stereo
    [INFO]   Wed Jun 21 22:54:19 2006  Connecting 'alsa_pcm:capture_1' to 'rotter:left'
    [INFO]   Wed Jun 21 22:54:19 2006  Connecting 'alsa_pcm:capture_2' to 'rotter:right'
    [INFO]   Wed Jun 21 22:54:19 2006  Starting new archive file: /var/archives/2006/06/21/22/archive.mp3
    [DEBUG]  Wed Jun 21 22:54:19 2006  Opening MPEG Audio output file: /var/archives/2006/06/21/22/archive.mp3
    [INFO]   Wed Jun 21 23:00:00 2006  Starting new archive file: /var/archives/2006/06/21/23/archive.mp3
    [DEBUG]  Wed Jun 21 23:00:00 2006  Closing MPEG Audio output file.
    [DEBUG]  Wed Jun 21 23:00:00 2006  Opening MPEG Audio output file: /var/archives/2006/06/21/23/archive.mp3

Start logging audio to hourly files in /var/archives.
Rotter will automatically connect itself to the first two JACK output ports
it finds and encode to MPEG Layer 3 audio at 128kbps. Each hour it will
delete files older than 1000 hours (42 days). Verbose mode means it will
display more informational messages.



[Recording of Transmission]:  http://en.wikipedia.org/wiki/Recording_of_transmission
[JACK]:  http://jackaudio.org/

