//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2009-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// rate_filter.cc author Dilbagh Chahal <dchahal@sourcefire.com>

#include "rate_filter.h"

// rate filter interface for Snort

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "parser.h"

#include "sfrf.h"
#include "rules.h"
#include "snort_config.h"
#include "sfthd.h"
#include "sfip/sf_ip.h"

//static int _printThresholdContext(RateFilterConfig*);

RateFilterConfig* RateFilter_ConfigNew(void)
{
    RateFilterConfig* rf_config = (RateFilterConfig*)SnortAlloc(sizeof(*rf_config));

    rf_config->memcap = 1024 * 1024;

    return rf_config;
}

/* Free threshold context */
void RateFilter_ConfigFree(RateFilterConfig* config)
{
    int i;

    if (config == NULL)
        return;

    for (i = 0; i < SFRF_MAX_GENID; i++)
    {
        if (config->genHash[i] != NULL)
            sfghash_delete(config->genHash[i]);
    }

    free(config);
}

void RateFilter_Cleanup(void)
{
    SFRF_Delete();
}

/*
 * Create and Add a Thresholding Event Object
 */
int RateFilter_Create(
    SnortConfig* sc, RateFilterConfig* rf_config, tSFRFConfigNode* thdx)
{
    int error;

    if (rf_config == NULL)
        return -1;

#ifdef RF_DBG
    printf(
        "THRESHOLD: gid=%u, sid=%u, tracking=%d, count=%d, seconds=%d \n",
        thdx->gid, thdx->sid, thdx->tracking, thdx->count, thdx->seconds);
}

#endif

    /* Add the object to the table - */
    error = SFRF_ConfigAdd(sc, rf_config, thdx);

    // enable internal events as required
    if ( !error && EventIsInternal(thdx->gid) )
    {
        EnableInternalEvent(rf_config, thdx->sid);

        if ( thdx->sid == INTERNAL_EVENT_SESSION_ADD )
            EnableInternalEvent(rf_config, INTERNAL_EVENT_SESSION_DEL);
    }
    return error;
}

/*
    Test an event against the threshold object table
    to determine if the new_action should be applied.

    returns 1 - rate threshold reached
            0 - rate threshold not reached
*/
int RateFilter_Test(
    OptTreeNode* otn,
    Packet* p)
{
    unsigned gid = otn->sigInfo.generator;
    unsigned sid = otn->sigInfo.id;

    const sfip_t* sip;
    const sfip_t* dip;
    sfip_t cleared;

    if ( p->ptrs.ip_api.is_ip() )
    {
        sip = p->ptrs.ip_api.get_src();
        dip = p->ptrs.ip_api.get_dst();
    }
    else
    {
        sfip_clear(cleared);
        sip = &cleared;
        dip = &cleared;
    }

    if ((snort_conf == NULL) || (snort_conf->rate_filter_config == NULL))
    {
        /* this should not happen, see the create fcn */
        return -1;
    }

    if ( EventIsInternal(gid) )
    {
        // at present stream connection events are the only internal
        // events and these require: src -> client, dst -> server.
        if ( p->packet_flags & PKT_FROM_SERVER )
        {
            return SFRF_TestThreshold(
                snort_conf->rate_filter_config, gid, sid, dip, sip,
                p->pkth->ts.tv_sec, SFRF_COUNT_INCREMENT);
        }
    }

    return SFRF_TestThreshold(
        snort_conf->rate_filter_config, gid, sid, sip, dip,
        p->pkth->ts.tv_sec, SFRF_COUNT_INCREMENT);
}

/* empty out active entries */
void RateFilter_ResetActive(void)
{
    SFRF_Flush();
}

void RateFilter_PrintConfig(RateFilterConfig*)
{
    // FIXIT-L print from module
    //_printThresholdContext(config);
}

#if 0
static int _logConfigNode(tSFRFConfigNode* p)
{
    const char* trackBy = "?";
    char buf[STD_BUF+1];
    *buf = '\0';

    // SnortSnprintfAppend(buf, STD_BUF, "| thd-id=%d", p->thd_id );

    if ( p->gid == 0 )
    {
        SnortSnprintfAppend(buf, STD_BUF, "| gen-id=global");
    }
    else
    {
        SnortSnprintfAppend(buf, STD_BUF, "| gen-id=%-6d", p->gid);
    }
    if ( p->sid == 0 )
    {
        SnortSnprintfAppend(buf, STD_BUF, " sig-id=global");
    }
    else
    {
        SnortSnprintfAppend(buf, STD_BUF, " sig-id=%-10d", p->sid);
    }

    SnortSnprintfAppend(buf, STD_BUF, " policyId=%-10d", p->policyId);

    switch ( p->tracking )
    {
    case SFRF_TRACK_BY_SRC: trackBy = "src"; break;
    case SFRF_TRACK_BY_DST: trackBy = "dst"; break;
    case SFRF_TRACK_BY_RULE: trackBy = "rule"; break;
    default: break;
    }
    SnortSnprintfAppend(buf, STD_BUF, " tracking=%s", trackBy);
    SnortSnprintfAppend(buf, STD_BUF, " count=%-3d", p->count);
    SnortSnprintfAppend(buf, STD_BUF, " seconds=%-3d", p->seconds);

    LogMessage("%s\n", buf);

    return 1;
}

static int _printThresholdContext(RateFilterConfig* config)
{
    int gid;
    int lcnt=0;

    if (config == NULL)
        return 0;

    for ( gid=0; gid < SFRF_MAX_GENID; gid++ )
    {
        SFGHASH_NODE* item_hash_node;
        SFGHASH* sfrf_hash = config->genHash [ gid ];

        if ( !sfrf_hash )
        {
            continue;
        }

        for ( item_hash_node  = sfghash_findfirst(sfrf_hash);
            item_hash_node != 0;
            item_hash_node  = sfghash_findnext(sfrf_hash) )
        {
            tSFRFSidNode* sfrf_item;
            tSFRFConfigNode* sfrf_node;

            /* Check for any Permanent sid objects for this gid */
            sfrf_item = (tSFRFSidNode*)item_hash_node->data;
            SF_LNODE* cursor;

            for ( sfrf_node  =
                (tSFRFConfigNode*)sflist_first(sfrf_item->configNodeList, &cursor);
                sfrf_node != 0;
                sfrf_node =
                (tSFRFConfigNode*)sflist_next(&cursor) )
            {
                if ( _logConfigNode(sfrf_node) != 0 )
                    lcnt++;
            }
        }
    }

    if ( !lcnt )
        LogMessage("| none\n");

    return 0;
}

#endif

