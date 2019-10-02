/*
    Silence Remover, a plugin for the DeaDBeeF audio player

    Based on Volume Meter plugin from Christian Boxdörfer <christian.boxdoerfer@posteo.de>

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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <assert.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "deadbeef.h"
#include "fastftoi.h"


#define MAX_CHANNELS 2


/* Global variables */
static DB_misc_t plugin;
static DB_functions_t *deadbeef    = NULL;
static gboolean scan_start_blocked = FALSE;
static gboolean scan_end_blocked   = FALSE;
static int channels;
static intptr_t mutex;


static void silenceremover_wavedata_listener( void *ctx, ddb_audio_data_t *data )
{
    float middle = 0;

    deadbeef->mutex_lock( mutex );
    channels     = MIN( MAX_CHANNELS, data->fmt->channels );
    int nsamples = data->nframes / channels;

    for ( int channel = 0; channel < channels; channel++ )
    {
        float sum = 0;
        for ( int s = 0; s < nsamples + channel; s++ )
        {
            float amplitude = data->data[ftoi( s * data->fmt->channels ) + channel];
            sum += amplitude * amplitude;
        }
        middle += ( sqrt( sum / nsamples ) ) / 2;
    }


    DB_playItem_t *it = deadbeef->streamer_get_playing_track();
    // float length      = deadbeef->pl_get_item_duration( it );
    // float pos         = deadbeef->streamer_get_playpos();
    float percent     = deadbeef->playback_get_pos();

    if ( middle > 0.001500 ) scan_start_blocked = TRUE;

    // Fast forward from the beginning until reaching of the threshold value
    if ( !scan_start_blocked && percent < 10.0 )
    {
        deadbeef->playback_set_pos( percent + 0.5 );
        // printf( "++ Fast forward from the beginning: Amplitude: %f / percent played: %f / play pos: %f / song length: %f\n", middle, percent, pos, length );
    }
    // If the music gets too quiet towards the end of the song we jump to the next song
    else if ( !scan_end_blocked && middle <= 0.001500 && percent > 90.0 )
    {
        deadbeef->sendmessage( DB_EV_NEXT, 0, 0, 0 );
        scan_end_blocked = TRUE;
        // printf( "-> Next song (%f seconds skipped): Amplitude: %f / percent played: %f / play pos: %f / song length: %f\n\n", length - pos, middle, percent, pos, length );
    }
    // Skip very quiet places in the middle part of the song
    else if ( middle < 0.000100 && percent >= 10.0 && percent <= 90.0 )
    {
        deadbeef->playback_set_pos( percent + 0.5 );
        // printf( "++ Fast forward: Amplitude: %f / percent played: %f / play pos: %f / song length: %f\n", middle, percent, pos, length );
    }

    deadbeef->pl_item_unref( it );
    deadbeef->mutex_unlock( mutex );
}

int silenceremover_connect( void )
{
    mutex = deadbeef->mutex_create();
    deadbeef->vis_waveform_listen( NULL, silenceremover_wavedata_listener );

    return 0;
}

int silenceremover_start( void ) { return 0; }

int silenceremover_stop( void ) { return 0; }

static int handle_event( uint32_t current_event, uintptr_t ctx, uint32_t p1, uint32_t p2 )
{
    // Reset variables at the start of the (next) song
    if ( current_event == DB_EV_SONGSTARTED )
    {
        scan_start_blocked = FALSE;
        scan_end_blocked   = FALSE;
    }

    return 0;
}

static DB_misc_t plugin = {
    .plugin.type          = DB_PLUGIN_MISC,
    .plugin.api_vmajor    = 1,
    .plugin.api_vminor    = 5,
    .plugin.version_major = 1,
    .plugin.version_minor = 0,
    .plugin.id            = "silenceremover",
    .plugin.name          = "Silence Remover",
    .plugin.descr         = "The plugin automatically skips quiet areas of a song for gapless playback.",
    .plugin.copyright     = "Copyright (C) 2019 kpcee\n"
                        "\n"
                        "This program is free software; you can redistribute it and/or\n"
                        "modify it under the terms of the GNU General Public License\n"
                        "as published by the Free Software Foundation; either version 2\n"
                        "of the License, or (at your option) any later version.\n"
                        "\n"
                        "This program is distributed in the hope that it will be useful,\n"
                        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
                        "GNU General Public License for more details.\n"
                        "\n"
                        "You should have received a copy of the GNU General Public License\n"
                        "along with this program; if not, write to the Free Software\n"
                        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n",
    .plugin.website    = "https://github.com/kpcee/deadbeef-silence_remover",
    .plugin.start      = silenceremover_start,
    .plugin.stop       = silenceremover_stop,
    .plugin.connect    = silenceremover_connect,
    .plugin.disconnect = NULL,
    .plugin.message    = handle_event,
};

DB_plugin_t *silence_remover_load( DB_functions_t *ddb )
{
    deadbeef = ddb;
    return &plugin.plugin;
}
