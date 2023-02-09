/*
 * ShortCircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2023, Various authors, as described in the github
 * transaction log.
 *
 * ShortCircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortCircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortCircuitXT is inspired by, and shares some code with, the
 * commercial product ShortCircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for ShortCircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortCircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#include "voice_matrix.h"
#include "engine/zone.h"
#include "voice/voice.h"

namespace scxt::modulation
{
void VoiceModMatrix::clear()
{
    memset(sourcePointers, 0, sizeof(sourcePointers));
    memset(baseValues, 0, sizeof(baseValues));
    memset(modulatedValues, 0, sizeof(modulatedValues));
}

void VoiceModMatrix::snapRoutingFromZone(engine::Zone *z) { routingTable = z->routingTable; }

void VoiceModMatrix::copyBaseValuesFromZone(engine::Zone *z)
{
    for (int i = 0; i < engine::processorsPerZone; ++i)
    {
        baseValues[vmd_Processor1_Mix + i] = z->processorStorage[i].mix;
    }
}

void VoiceModMatrix::attachSourcesFromVoice(voice::Voice *v)
{
    std::cout << "Attaching to voice " << v << std::endl;
    sourcePointers[vms_none] = nullptr;
    for (int i = 0; i < engine::lfosPerZone; ++i)
    {
        sourcePointers[vms_LFO1 + i] = &(v->lfos[i].output);
    }
}

void VoiceModMatrix::process()
{
    memcpy(modulatedValues, baseValues, sizeof(modulatedValues));
    for (const auto &r : routingTable)
    {
        if (r.dst == vmd_none || r.src == vms_none)
            continue;
        modulatedValues[r.dst] += (*(sourcePointers[r.src])) * r.depth;
    }
}

std::string getVoiceModMatrixDestStreamingName(const VoiceModMatrixDestination &dest)
{
    switch (dest)
    {
    case vmd_none:
        return "vmd_none";

    case vmd_LFO1_Rate:
        return "vmd_lfo1_rate";
    case vmd_LFO2_Rate:
        return "vmd_lfo2_rate";
    case vmd_LFO3_Rate:
        return "vmd_lfo3_rate";

    case vmd_Processor1_Mix:
        return "vmd_processor1_mix";
    case vmd_Processor2_Mix:
        return "vmd_processor2_mix";

    case numVoiceMatrixDestinations:
        throw std::logic_error("Can't convert numVoiceMatrixDestinations to string");
    }

    throw std::logic_error("Invalid enum");
}
std::optional<VoiceModMatrixDestination> fromVoiceModMatrixDestStreamingName(const std::string &s)
{
    for (int i = vmd_none; i < numVoiceMatrixDestinations; ++i)
    {
        if (getVoiceModMatrixDestStreamingName((VoiceModMatrixDestination)i) == s)
        {
            return ((VoiceModMatrixDestination)i);
        }
    }
    return vmd_none;
}

std::string getVoiceModMatrixSourceStreamingName(const VoiceModMatrixSource &dest)
{
    switch (dest)
    {
    case vms_none:
        return "vms_none";
    case vms_LFO1:
        return "vms_lfo1";
    case vms_LFO2:
        return "vms_lfo2";
    case vms_LFO3:
        return "vms_lfo3";
    case numVoiceMatrixSources:
        throw std::logic_error("Don't call with numVoiceMatrixSources");
    }

    throw std::logic_error("Mysterious unhandled condition");
}
std::optional<VoiceModMatrixSource> fromVoiceModMatrixSourceStreamingName(const std::string &s)
{
    for (int i = vmd_none; i < numVoiceMatrixSources; ++i)
    {
        if (getVoiceModMatrixSourceStreamingName((VoiceModMatrixSource)i) == s)
        {
            return ((VoiceModMatrixSource)i);
        }
    }
    return vms_none;
}

} // namespace scxt::modulation