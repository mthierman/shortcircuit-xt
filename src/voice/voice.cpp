/*
 * Shortcircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2023, Various authors, as described in the github
 * transaction log.
 *
 * ShortcircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortcircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortcircuitXT is inspired by, and shares code with, the
 * commercial product Shortcircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for Shortcircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortcircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#include "voice.h"
#include "tuning/equal.h"
#include <cassert>
#include <cmath>

#include "sst/basic-blocks/mechanics/block-ops.h"
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "engine/engine.h"

namespace scxt::voice
{

Voice::Voice(engine::Zone *z) : zone(z), aeg(this), eg2(this)
{
    assert(zone);
    memset(output, 0, 2 * blockSize * sizeof(float));
    memset(processorIntParams, 0, sizeof(processorIntParams));
}

Voice::~Voice()
{
    for (auto i = 0; i < engine::processorsPerZone; ++i)
    {
        dsp::processor::unspawnProcessor(processors[i]);
    }
}

void Voice::voiceStarted()
{
    initializeGenerator();
    initializeProcessors();

    modMatrix.snapRoutingFromZone(zone);
    modMatrix.snapDepthScalesFromZone(zone);
    modMatrix.copyBaseValuesFromZone(zone);
    modMatrix.attachSourcesFromVoice(this);
    modMatrix.initializeModulationValues();

    for (auto i = 0U; i < engine::lfosPerZone; ++i)
    {
        lfos[i].setSampleRate(sampleRate, sampleRateInv);

        lfos[i].assign(&zone->lfoStorage[i], modMatrix.getValuePtr(modulation::vmd_LFO_Rate, i),
                       nullptr);
    }

    aeg.attackFrom(0.0, modMatrix.getValue(modulation::vmd_eg_A, 0),
                   1, // this needs fixing in legato mode
                   zone->aegStorage.isDigital);
    eg2.attackFrom(0.0, modMatrix.getValue(modulation::vmd_eg_A, 1),
                   1, // this needs fixing in legato mode
                   zone->aegStorage.isDigital);

    zone->addVoice(this);
}

bool Voice::process()
{
    namespace mech = sst::basic_blocks::mechanics;

    assert(zone);
    if (!isVoicePlaying)
    {
        memset(output, 0, sizeof(output));
        return true;
    }
    // TODO round robin state
    auto &s = zone->samplePointers[0];
    auto &sdata = zone->sampleData[0];
    assert(s);

    modMatrix.copyBaseValuesFromZone(zone);

    // Run Modulators
    for (auto i = 0; i < engine::lfosPerZone; ++i)
    {
        // TODO - only if we need it
        lfos[i].process(blockSize);
    }

    // TODO probably want to process LFO modulations at this point so the AEG and EG2
    // are modulatable

    // TODO SHape Fixes
    bool envGate = (sdata.playMode == engine::Zone::NORMAL && isGated) ||
                   (sdata.playMode == engine::Zone::ONE_SHOT && isGeneratorRunning) ||
                   (sdata.playMode == engine::Zone::ON_RELEASE && isGeneratorRunning);
    aeg.processBlock(modMatrix.getValue(modulation::vmd_eg_A, 0),
                     modMatrix.getValue(modulation::vmd_eg_D, 0),
                     modMatrix.getValue(modulation::vmd_eg_S, 0),
                     modMatrix.getValue(modulation::vmd_eg_R, 0), 1, 1, 1, envGate);
    eg2.processBlock(modMatrix.getValue(modulation::vmd_eg_A, 1),
                     modMatrix.getValue(modulation::vmd_eg_D, 1),
                     modMatrix.getValue(modulation::vmd_eg_S, 1),
                     modMatrix.getValue(modulation::vmd_eg_R, 1), 1, 1, 1, envGate);

    // TODO: And output is non zero once we are past attack
    isAEGRunning = (aeg.stage != adsrenv_t ::s_complete);

    // TODO and probably just want to process the envelopes here
    modMatrix.process();

    auto fpitch = calculateVoicePitch();
    calculateGeneratorRatio(fpitch);
    fpitch -= 69;

    // TODO : Start and End Points
    GD.sampleStart = 0;
    GD.sampleStop = s->sample_length;

    GD.gated = isGated;
    GD.loopInvertedBounds = 1.f / std::max(1, GD.loopUpperBound - GD.loopLowerBound);
    GD.playbackInvertedBounds = 1.f / std::max(1, GD.playbackUpperBound - GD.playbackLowerBound);
    if (!GD.isFinished)
        Generator(&GD, &GDIO);
    else
        memset(output, 0, sizeof(output));

    // TODO Ringout - this is obvioulsy wrong
    if (GD.isFinished)
    {
        isGeneratorRunning = false;
    }

    float tempbuf alignas(16)[2][BLOCK_SIZE], postfader_buf alignas(16)[2][BLOCK_SIZE];

    /*
     * Alright so time to document the logic. Remember all processors are required to do
     * stereo to stereo but may optionally do mono to mono or mono to stereo. Also this is
     * just for the linear routing case now. revisit this when we add the other routings.
     *
     * chainIsMono is the current state of mono down the chain.
     * If you get to processor[i] and chain is mono
     *    if the processor consumes mono and produces mono process mono and fade_1_block
     *    if the processor consumes mono and produces stereo process mono and fade_2 block and
     * toggle
     *    if the processor cannot consume mono, copy and proceed and toggle chainIsMon
     */
    bool chainIsMono{monoGenerator};

    /*
     * Implement Sample Pan
     */
    auto pvz = modMatrix.getValue(modulation::vmd_Zone_Sample_Pan, 0);
    if (pvz != 0.f)
    {
        panOutputsBy(chainIsMono, pvz);
        chainIsMono = false;
    }

    for (auto i = 0; i < engine::processorsPerZone; ++i)
    {
        if (processors[i]) // TODO && (!zone->processors[0].bypass))
        {
            processorMix[i].set_target(modMatrix.getValue(modulation::vmd_Processor_Mix, i));

            if (chainIsMono && processorConsumesMono[i] && !processorProducesStereo[i])
            {
                // mono to mono
                processors[i]->process_mono(output[0], tempbuf[0], tempbuf[1], fpitch);
                processorMix[i].fade_block_to(output[0], tempbuf[0], output[0], BLOCK_SIZE_QUAD);
            }
            else if (chainIsMono && processorConsumesMono[i] && processorProducesStereo[i])
            {
                // mono to stereo. process then toggle
                processors[i]->process_mono(output[0], tempbuf[0], tempbuf[1], fpitch);
                processorMix[i].fade_2_blocks_to(
                    output[0], tempbuf[0], output[0], // this out[0] is NOT a typo. Input is mono
                    tempbuf[1], output[0], output[1], BLOCK_SIZE_QUAD);
                chainIsMono = false;
            }
            else if (chainIsMono)
            {
                // stereo to stereo. copy L to R then process
                mech::copy_from_to<blockSize>(output[0], output[1]);
                chainIsMono = false;
                processors[i]->process_stereo(output[0], output[1], tempbuf[0], tempbuf[1], fpitch);
                processorMix[i].fade_2_blocks_to(output[0], tempbuf[0], output[1], tempbuf[1],
                                                 output[0], output[1], BLOCK_SIZE_QUAD);
            }
            else
            {
                // stereo to stereo. process
                processors[i]->process_stereo(output[0], output[1], tempbuf[0], tempbuf[1], fpitch);
                processorMix[i].fade_2_blocks_to(output[0], tempbuf[0], output[1], tempbuf[1],
                                                 output[0], output[1], BLOCK_SIZE_QUAD);
            }
            // TODO: What was the filter_modout? Seems SC2 never finished it
            /*
            filter_modout[0] = voice_filter[0]->modulation_output;
                                   */
        }
    }

    /*
     * Implement output pan
     */
    auto pvo = modMatrix.getValue(modulation::vmd_Zone_Output_Pan, 0);
    if (pvo != 0.f)
    {
        panOutputsBy(chainIsMono, pvo);
        chainIsMono = false;
    }

    // At the end of the voice we have to produce stereo
    if (chainIsMono)
    {
        mech::copy_from_to<blockSize>(output[0], output[1]);
    }

    mech::scale_by<blockSize>(aeg.outputCache, output[0], output[1]);

    /*
     * Finally do voice state update
     */
    if (isAEGRunning)
        isVoicePlaying = true;
    else
        isVoicePlaying = false;

    return true;
}

void Voice::panOutputsBy(bool chainIsMono, float pv)
{
    namespace pl = sst::basic_blocks::dsp::pan_laws;
    pv = (std::clamp(pv, -1.f, 1.f) + 1) * 0.5;
    pl::panmatrix_t pmat{1, 1, 0, 0};
    if (chainIsMono)
    {
        chainIsMono = false;
        pl::monoEqualPower(pv, pmat);
        for (int i = 0; i < blockSize; ++i)
        {
            output[1][i] = output[0][i] * pmat[3];
            output[0][i] = output[0][i] * pmat[0];
        }
    }
    else
    {
        pl::stereoEqualPower(pv, pmat);

        for (int i = 0; i < blockSize; ++i)
        {
            auto il = output[0][i];
            auto ir = output[1][i];
            output[0][i] = pmat[0] * il + pmat[2] * ir;
            output[1][i] = pmat[1] * ir + pmat[3] * il;
        }
    }
}

void Voice::initializeGenerator()
{
    // TODO round robin
    auto &s = zone->samplePointers[0];
    auto &sampleData = zone->sampleData[0];
    assert(s);

    GDIO.outputL = output[0];
    GDIO.outputR = output[1];
    GDIO.sampleDataL = s->sampleData[0];
    GDIO.sampleDataR = s->sampleData[1];
    GDIO.waveSize = s->sample_length;

    GD.samplePos = sampleData.startSample;
    GD.sampleSubPos = 0;
    GD.loopLowerBound = sampleData.startSample;
    GD.loopUpperBound = sampleData.endSample;
    GD.playbackLowerBound = sampleData.startSample;
    GD.playbackUpperBound = sampleData.endSample;
    GD.direction = 1;
    GD.isFinished = false;

    if (sampleData.loopActive)
    {
        GD.loopLowerBound = sampleData.startLoop;
        GD.loopUpperBound = sampleData.endLoop;
    }

    if (sampleData.playReverse)
    {
        GD.samplePos = GD.playbackUpperBound;
        GD.direction = -1;
    }
    GD.directionAtOutset = GD.direction;

    // TODO Oversampling if ratio too big
    calculateGeneratorRatio(calculateVoicePitch());

    GD.blockSize = blockSize;

    Generator = nullptr;

    monoGenerator = s->channels == 1;
    Generator = dsp::GetFPtrGeneratorSample(!monoGenerator, s->bitDepth == sample::Sample::BD_F32,
                                            sampleData.loopActive,
                                            sampleData.loopDirection == engine::Zone::FORWARD_ONLY,
                                            sampleData.loopMode == engine::Zone::LOOP_WHILE_GATED);
}

float Voice::calculateVoicePitch()
{
    auto fpitch = key + modMatrix.getValue(modulation::vmd_Sample_Pitch_Offset, 0);

    auto pitchWheel = zone->parentGroup->parentPart->pitchBendSmoother.output;
    auto pitchMv = pitchWheel > 0 ? zone->mapping.pbUp : zone->mapping.pbDown;
    fpitch += pitchWheel * pitchMv;

    return fpitch;
}

void Voice::calculateGeneratorRatio(float pitch)
{
    // TODO all of this obviously
#if 0
    keytrack = (fkey + zone->pitch_bend_depth * ctrl[c_pitch_bend] - 60.0f) *
               (1 / 12.0f); // keytrack as modulation source
    float kt = (playmode == pm_forward_hitpoints) ? 0 : ((fkey - zone->key_root) * zone->keytrack);

    fpitch = part->transpose + zone->transpose + zone->finetune +
             zone->pitch_bend_depth * ctrl[c_pitch_bend] + mm.get_destination_value(md_pitch);

    // not important enough to sacrifice perf for.. better to use on demand in case
    // loop_pos = limit_range((sample_pos -
    // mm.get_destination_value_int(md_loop_start))/(float)mm.get_destination_value_int(md_loop_length),0,1);

    GD.ratio = Float2Int((float)((wave->sample_rate * samplerate_inv) * 16777216.f *
                                 note_to_pitch(fpitch + kt - zone->pitchcorrection) *
                                 mm.get_destination_value(md_rate)));
    fpitch += fkey - 69.f; // relative to A3 (440hz)
#else
    // TODO gross for now - correct
    float ndiff = pitch - zone->mapping.rootKey;
    auto fac = tuning::equalTuning.note_to_pitch(ndiff);
    // TODO round robin
    GD.ratio = (int32_t)((1 << 24) * fac * zone->samplePointers[0]->sample_rate * sampleRateInv *
                         (1.0 + modMatrix.getValue(modulation::vmd_Sample_Playback_Ratio, 0)));
#endif
}

void Voice::initializeProcessors()
{
    assert(zone);
    assert(zone->getEngine());
    for (auto i = 0; i < engine::processorsPerZone; ++i)
    {
        processorMix[i].set_target(modMatrix.getValue(modulation::vmd_Processor_Mix, i));
        processorMix[i].instantize();

        processorType[i] = zone->processorStorage[i].type;
        assert(dsp::processor::isZoneProcessor(processorType[i]));

        auto fp = modMatrix.getValuePtr(modulation::vmd_Processor_FP1, i);
        memcpy(&processorIntParams[i][0], zone->processorStorage[i].intParams.data(),
               sizeof(processorIntParams[i]));
        processors[i] = dsp::processor::spawnProcessorInPlace(
            processorType[i], zone->getEngine()->getMemoryPool().get(),
            processorPlacementStorage[i], dsp::processor::processorMemoryBufferSize, fp,
            processorIntParams[i]);

        if (processors[i])
        {
            processors[i]->setSampleRate(sampleRate);
            processors[i]->init();

            processorConsumesMono[i] = monoGenerator && processors[i]->canProcessMono();
            processorProducesStereo[i] = processors[i]->monoInputCreatesStereoOutput();
        }
    }
}
} // namespace scxt::voice