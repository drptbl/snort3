//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
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
// codec_manager.cc author Josh Rosenbaum <jrosenba@cisco.com>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <algorithm>
#include <string.h>
#include "utils/dnet_header.h"
#include "framework/codec.h"
#include "managers/codec_manager.h"
#include "protocols/packet_manager.h"
#include "log/messages.h"
#include "parser/parser.h"
#include "packet_io/sfdaq.h"
#include "main/snort_config.h"

struct CodecManager::CodecApiWrapper
{
    const CodecApi* api;
    bool init;
};

//  CodecManager Private Data

// the zero initialization is not required but quiets the compiler
std::vector<CodecManager::CodecApiWrapper> CodecManager::s_codecs;
std::array<uint8_t, max_protocol_id> CodecManager::s_proto_map {
    { 0 }
};
std::array<Codec*, UINT8_MAX> CodecManager::s_protocols {
    { 0 }
};
THREAD_LOCAL uint8_t CodecManager::grinder = 0;
THREAD_LOCAL uint8_t CodecManager::max_layers = DEFAULT_LAYERMAX;

// This is hardcoded into Snort++
extern const CodecApi* default_codec;

// Local variables for various tasks
static const uint16_t IP_ID_COUNT = 8192;
static THREAD_LOCAL std::array<uint16_t, IP_ID_COUNT> s_id_pool {
    { 0 }
};
static THREAD_LOCAL rand_t* s_rand = NULL;

/*
 * Begin search from index 1.  0 is a special case in that it is the default
 * codec and is actually a duplicate. i.e., we can find the 0 indexed
 * codec somehwere else in the array too.
 *
 * Returns: 0 on failure, induex on success
 */
uint8_t CodecManager::get_codec(const char* const keyword)
{
    // starting at 1 since 0 is default
    for ( uint8_t i = 1; i < s_protocols.size(); i++)
    {
        if (s_protocols[i])
        {
            const char* name = s_protocols[i]->get_name();
            if ( !strncasecmp(name, keyword, strlen(name)) )
                return i;
        }
    }
    return 0;
}

CodecManager::CodecApiWrapper& CodecManager::get_api_wrapper(const CodecApi* cd_api)
{
    for (CodecApiWrapper& caw : s_codecs)
    {
        if (caw.api == cd_api)
            return caw;
    }

    ParseAbort("Attempting to instantiate Codec '%s', "
        "but codec has not been added", cd_api->base.name);
}

void CodecManager::add_plugin(const CodecApi* api)
{
    if (!api->ctor)
        ParseError("CodecApi ctor() for Codec %s: ctor() must be implemented\n",
            api->base.name);
    if (!api->dtor)
        ParseError("CodecApi ctor() for Codec %s: ctor() must be implemented\n",
            api->base.name);

    CodecApiWrapper wrap;
    wrap.api = api;
    wrap.init = false;

    s_codecs.push_back(wrap);
}

void CodecManager::release_plugins()
{
    for ( CodecApiWrapper& wrap : s_codecs )
    {
        if (wrap.api->pterm)
        {
            wrap.api->pterm();
            wrap.init = false; // Future proofing this functin.
        }

        uint8_t index = get_codec(wrap.api->base.name);
        if ( index != 0)
        {
            wrap.api->dtor(s_protocols[index]);
            s_protocols[index] = nullptr;
        }
    }

    // The default codec is NOT part of the plugin list
    if (default_codec->pterm)
        default_codec->pterm();

    if (s_protocols[0])
    {
        default_codec->dtor(s_protocols[0]);
        s_protocols[0] = nullptr;
    }

    s_codecs.clear();
    s_proto_map.fill(0);
}

void CodecManager::instantiate(CodecApiWrapper& wrap, Module* m, SnortConfig*)
{
    static std::size_t codec_id = 1;

    if (!wrap.init)
    {
        std::vector<uint16_t> ids;
        const CodecApi* const cd_api = wrap.api;

        if (codec_id >= s_protocols.size())
            ParseError("A maximum of 256 codecs can be registered\n");

        if (cd_api->pinit)
            cd_api->pinit();

        Codec* cd = cd_api->ctor(m);
        cd->get_protocol_ids(ids);
        for (auto id : ids)
        {
            if (s_proto_map[id] != 0)
                ParseError("The Codecs %s and %s have both been registered "
                    "for protocol_id %d. Codec %s will be used\n",
                    s_protocols[s_proto_map[id]]->get_name(), cd->get_name(),
                    id, cd->get_name());

            s_proto_map[id] = (decltype(s_proto_map[id]))codec_id; // future proofing
        }

        wrap.init = true;
        s_protocols[codec_id++] = cd;
    }
}

void CodecManager::instantiate(const CodecApi* cd_api, Module* m, SnortConfig* sc)
{
    CodecApiWrapper& wrap = get_api_wrapper(cd_api);
    instantiate(wrap, m, sc);
}

void CodecManager::instantiate()
{
    CodecApiWrapper tmp_wrap;
    tmp_wrap.api = default_codec;
    tmp_wrap.init = false;
    instantiate(tmp_wrap, nullptr, nullptr);

    // default codec is the api ... I want the codec.
    s_protocols[0] = s_protocols[get_codec(default_codec->base.name)];

    // and instantiate every codec which does not have a module
    for (CodecApiWrapper& wrap : s_codecs)
        instantiate(wrap, nullptr, nullptr);
}

void CodecManager::thread_init(SnortConfig* sc)
{
    max_layers = sc->num_layers;

    for ( CodecApiWrapper& wrap : s_codecs )
        if (wrap.api->tinit)
            wrap.api->tinit();

    int daq_dlt = DAQ_GetBaseProtocol();
    for (int i = 0; s_protocols[i] != 0; i++)
    {
        Codec* cd = s_protocols[i];
        std::vector<int> data_link_types;

        cd->get_data_link_type(data_link_types);
        for (auto curr_dlt : data_link_types)
        {
            if (curr_dlt == daq_dlt)
            {
                if (grinder != 0)
                    WarningMessage("The Codecs %s and %s have both been registered "
                        "as the raw decoder. Codec %s will be used\n",
                        s_protocols[grinder]->get_name(), cd->get_name(),
                        cd->get_name());

                grinder = (uint8_t)i;
            }
        }
    }

    if (!grinder)
        ParseError("Unable to find a Codec with data link type %d\n", daq_dlt);

#ifndef VALGRIND_TESTING
    if ( s_rand )
        rand_close(s_rand);

    // rand_open() can yield valgriind errors because the
    // starting seed may come from "random stack contents"
    // (see man 3 dnet)
    s_rand = rand_open();

    if ( !s_rand )
        ParseError("rand_open() failed.\n");

    rand_get(s_rand, s_id_pool.data(), s_id_pool.size());
#endif
}

void CodecManager::thread_term()
{
    PacketManager::accumulate(); // statistics

    for ( CodecApiWrapper& wrap : s_codecs )
    {
        if (wrap.api->tterm)
            wrap.api->tterm();
    }

    if ( s_rand )
    {
        rand_close(s_rand);
        s_rand = NULL;
    }
}

void CodecManager::dump_plugins()
{
    Dumper d("Codecs");

    for ( CodecApiWrapper& wrap : s_codecs )
        d.dump(wrap.api->base.name, wrap.api->base.version);
}

#ifdef PIGLET
const CodecApi* CodecManager::find_api(const char* name)
{
    for ( auto wrap : CodecManager::s_codecs )
        if ( !strcmp(wrap.api->base.name, name) )
            return wrap.api;

    return nullptr;
}

CodecWrapper* CodecManager::instantiate(const char* name, Module* m, SnortConfig*)
{
    auto api = find_api(name);
    if ( !api )
        return nullptr;

    auto p = api->ctor(m);
    if ( !p )
        return nullptr;

    return new CodecWrapper(api, p);
}

#endif

