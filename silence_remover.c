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

#include <gtk/gtk.h>
#include "deadbeef.h"
#include "fastftoi.h"


#define MAX_CHANNELS 2


/* Global variables */
static DB_misc_t plugin;
static DB_functions_t *deadbeef    = NULL;
static gboolean scan_start_blocked = FALSE;
static gboolean scan_end_blocked   = FALSE;
static gboolean plugin_enabled     = FALSE;
static intptr_t mutex              = 0;
static int pl_loop_mode            = 0;
static float dB_Threshold_Value_Start;
static float dB_Threshold_Value_Middle;
static float dB_Threshold_Value_End;



static void load_config(){
    dB_Threshold_Value_Start  = deadbeef->conf_get_int ("silence_remover.dB_start", 10);
    dB_Threshold_Value_Middle = deadbeef->conf_get_int ("silence_remover.dB_middle", 0);
    dB_Threshold_Value_End    = deadbeef->conf_get_int ("silence_remover.dB_end",   35);
    plugin_enabled = !(dB_Threshold_Value_Start == -1 && dB_Threshold_Value_Middle == -1 && dB_Threshold_Value_End == -1);

    pl_loop_mode = deadbeef->conf_get_int ("playback.loop", 0);

    //printf( "++ config reloaded: dB-start: %f    dB-middle: %f     dB-end: %f   enabled: %i \n",  dB_Threshold_Value_Start, dB_Threshold_Value_Middle, dB_Threshold_Value_End, plugin_enabled );
}


static void silenceremover_wavedata_listener( void *ctx, ddb_audio_data_t *data )
{
    float percent = deadbeef->playback_get_pos();
    if (percent <= 0.0) return;

    deadbeef->mutex_lock( mutex );

    float result = 0;
    int channels = MIN( MAX_CHANNELS, data->fmt->channels );
    int nsamples = data->nframes / channels;

    for ( int channel = 0; channel < channels; channel++ )
    {
        float sum = 0;
        for ( int s = 0; s < nsamples + channel; s++ )
        {
            float amplitude = data->data[ftoi( s * data->fmt->channels ) + channel];
            sum += amplitude * amplitude;
        }
        result += ( sqrt( sum / nsamples ) );
    }

    result /= channels;
    float dB = 100 + ( 20.0 * log10f (result) ); // 100 = DB_RANGE
  
    // printf( "dB: %f / start_dB: %f    middle_dB: %f     end_dB: %f   percent played: %f\n", dB, dB_Threshold_Value_Start, dB_Threshold_Value_Middle, dB_Threshold_Value_End, percent );

    //DB_playItem_t *it = deadbeef->streamer_get_playing_track_safe();
    //float length      = deadbeef->pl_get_item_duration( it );
    //float pos         = deadbeef->streamer_get_playpos();
    //deadbeef->pl_item_unref( it );



    if ( dB > dB_Threshold_Value_Start ) scan_start_blocked = TRUE;

    // Fast forward from the beginning until reaching of the threshold value
    if ( dB_Threshold_Value_Start >= 0 && !scan_start_blocked && percent < 10.0 )
    {
        deadbeef->playback_set_pos( percent + 0.05 );
        //printf( "++ Fast forward from the beginning: dB: %f / percent played: %f / play pos: %f / song length: %f\n", dB, percent, pos, length );
    }
    // If the music gets too quiet towards the end of the song we jump to the next song
    else if ( dB_Threshold_Value_End >= 0 && !scan_end_blocked && dB <= dB_Threshold_Value_End && percent > 90.0 )
    {
        // deadbeef->sendmessage( DB_EV_NEXT, 0, 0, 0 ); does not catch activated PLAYBACK_MODE_LOOP_SINGLE
        if (pl_loop_mode == PLAYBACK_MODE_LOOP_SINGLE) { // song finished, loop mode is "loop 1 track"
            deadbeef->sendmessage( DB_EV_PLAY_CURRENT, 0, 0, 0 );
        } else {
            deadbeef->sendmessage( DB_EV_NEXT, 0, 0, 0 );
        }

        scan_end_blocked = TRUE;
        //printf( "-> Next song (%f seconds skipped): dB: %f / percent played: %f / play pos: %f / song length: %f\n\n", length - pos, dB, percent, pos, length );
    }
    // Skip very quiet places in the middle part of the song
    else if ( dB_Threshold_Value_Middle >= 0 && dB <= dB_Threshold_Value_Middle && percent >= 10.0 && percent <= 90.0 )
    {
        deadbeef->playback_set_pos( percent + 0.1 );
        //printf( "++ Fast forward: dB: %f / percent played: %f / play pos: %f / song length: %f\n", dB, percent, pos, length );
    }

    deadbeef->mutex_unlock( mutex );
}

static int silenceremover_connect( void )
{
    if (mutex == 0) mutex = deadbeef->mutex_create();
    deadbeef->vis_waveform_listen( NULL, silenceremover_wavedata_listener );

    return 0;
}


static int silenceremover_disconnect( void )
{
    deadbeef->vis_waveform_unlisten( NULL );

    return 0;
}



static int silenceremover_start( void ) {
    load_config();

    return 0;
}

static int silenceremover_stop( void ) { return 0; }

static int handle_event( uint32_t current_event, uintptr_t ctx, uint32_t p1, uint32_t p2 )
{
    // Reset variables at the start of the (next) song
    if ( current_event == DB_EV_SONGSTARTED )
    {
        scan_start_blocked = FALSE;
        scan_end_blocked   = FALSE;
    }

    // Prevent songs from being skipped if DeaDBeeF switch to the next song itself 
    else if ( current_event == DB_EV_SONGCHANGED || current_event == DB_EV_SONGFINISHED )
    {
        scan_start_blocked = TRUE;
        scan_end_blocked   = TRUE;
    }

    else if ( current_event == DB_EV_CONFIGCHANGED ){
        load_config();

        if (plugin_enabled) silenceremover_connect();
        else silenceremover_disconnect();

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
    .plugin.copyright     = "Copyright (C) 2020 kpcee\n"
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
    .plugin.website      = "https://github.com/kpcee/deadbeef-silence_remover",
    .plugin.start        = silenceremover_start,
    .plugin.stop         = silenceremover_stop,
    .plugin.connect      = silenceremover_connect,
    .plugin.disconnect   = NULL,
    .plugin.message      = handle_event,
    .plugin.configdialog = "property \"Skip parts on the beginning less or equal as x dB (-1 to deactivate) \" spinbtn[-1,100,1] silence_remover.dB_start 10 ; \n"
                           "property \"Skip middle parts less or equal as x dB (-1 to deactivate) \" spinbtn[-1,100,1] silence_remover.dB_middle 0 ;\n"
                           "property \"Skip parts on the end less or equal as x dB (-1 to deactivate) \" spinbtn[-1,100,1] silence_remover.dB_end 35 ;\n"
};


DB_plugin_t *silence_remover_load( DB_functions_t *ddb )
{
    deadbeef = ddb;
    return &plugin.plugin;
}
