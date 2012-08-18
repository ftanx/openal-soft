/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"
#include "alSource.h"
#include "alBuffer.h"
#include "alListener.h"
#include "alAuxEffectSlot.h"
#include "alu.h"
#include "bs2b.h"


struct ChanMap {
    enum Channel channel;
    ALfloat angle;
};

/* Cone scalar */
ALfloat ConeScale = 1.0f;

/* Localized Z scalar for mono sources */
ALfloat ZScale = 1.0f;


static __inline ALvoid aluMatrixVector(ALfloat *vector,ALfloat w,ALfloat matrix[4][4])
{
    ALfloat temp[4] = {
        vector[0], vector[1], vector[2], w
    };

    vector[0] = temp[0]*matrix[0][0] + temp[1]*matrix[1][0] + temp[2]*matrix[2][0] + temp[3]*matrix[3][0];
    vector[1] = temp[0]*matrix[0][1] + temp[1]*matrix[1][1] + temp[2]*matrix[2][1] + temp[3]*matrix[3][1];
    vector[2] = temp[0]*matrix[0][2] + temp[1]*matrix[1][2] + temp[2]*matrix[2][2] + temp[3]*matrix[3][2];
}


ALvoid CalcNonAttnSourceParams(ALsource *ALSource, const ALCcontext *ALContext)
{
    static const struct ChanMap MonoMap[1] = { { FrontCenter, 0.0f } };
    static const struct ChanMap StereoMap[2] = {
        { FrontLeft, -30.0f * F_PI/180.0f },
        { FrontRight, 30.0f * F_PI/180.0f }
    };
    static const struct ChanMap StereoWideMap[2] = {
        { FrontLeft, -90.0f * F_PI/180.0f },
        { FrontRight, 90.0f * F_PI/180.0f }
    };
    static const struct ChanMap RearMap[2] = {
        { BackLeft, -150.0f * F_PI/180.0f },
        { BackRight, 150.0f * F_PI/180.0f }
    };
    static const struct ChanMap QuadMap[4] = {
        { FrontLeft, -45.0f * F_PI/180.0f },
        { FrontRight, 45.0f * F_PI/180.0f },
        { BackLeft, -135.0f * F_PI/180.0f },
        { BackRight, 135.0f * F_PI/180.0f }
    };
    static const struct ChanMap X51Map[6] = {
        { FrontLeft, -30.0f * F_PI/180.0f },
        { FrontRight, 30.0f * F_PI/180.0f },
        { FrontCenter, 0.0f * F_PI/180.0f },
        { LFE, 0.0f },
        { BackLeft, -110.0f * F_PI/180.0f },
        { BackRight, 110.0f * F_PI/180.0f }
    };
    static const struct ChanMap X61Map[7] = {
        { FrontLeft,  -30.0f * F_PI/180.0f },
        { FrontRight,  30.0f * F_PI/180.0f },
        { FrontCenter,  0.0f * F_PI/180.0f },
        { LFE, 0.0f },
        { BackCenter, 180.0f * F_PI/180.0f },
        { SideLeft,   -90.0f * F_PI/180.0f },
        { SideRight,   90.0f * F_PI/180.0f }
    };
    static const struct ChanMap X71Map[8] = {
        { FrontLeft, -30.0f * F_PI/180.0f },
        { FrontRight, 30.0f * F_PI/180.0f },
        { FrontCenter, 0.0f * F_PI/180.0f },
        { LFE, 0.0f },
        { BackLeft, -150.0f * F_PI/180.0f },
        { BackRight, 150.0f * F_PI/180.0f },
        { SideLeft,  -90.0f * F_PI/180.0f },
        { SideRight,  90.0f * F_PI/180.0f }
    };

    ALCdevice *Device = ALContext->Device;
    ALfloat SourceVolume,ListenerGain,MinVolume,MaxVolume;
    ALbufferlistitem *BufferListItem;
    enum FmtChannels Channels;
    ALfloat (*SrcMatrix)[MaxChannels];
    ALfloat DryGain, DryGainHF;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALint NumSends, Frequency;
    const struct ChanMap *chans = NULL;
    enum Resampler Resampler;
    ALint num_channels = 0;
    ALboolean DirectChannels;
    ALfloat hwidth = 0.0f;
    ALfloat Pitch;
    ALfloat cw;
    ALint i, c;

    /* Get device properties */
    NumSends  = Device->NumAuxSends;
    Frequency = Device->Frequency;

    /* Get listener properties */
    ListenerGain = ALContext->Listener.Gain;

    /* Get source properties */
    SourceVolume    = ALSource->Gain;
    MinVolume       = ALSource->MinGain;
    MaxVolume       = ALSource->MaxGain;
    Pitch           = ALSource->Pitch;
    Resampler       = ALSource->Resampler;
    DirectChannels  = ALSource->DirectChannels;

    /* Calculate the stepping value */
    Channels = FmtMono;
    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            ALsizei maxstep = STACK_DATA_SIZE/sizeof(ALfloat) /
                              ALSource->NumChannels;
            maxstep -= ResamplerPadding[Resampler] +
                       ResamplerPrePadding[Resampler] + 1;
            maxstep = mini(maxstep, INT_MAX>>FRACTIONBITS);

            Pitch = Pitch * ALBuffer->Frequency / Frequency;
            if(Pitch > (ALfloat)maxstep)
                ALSource->Params.Step = maxstep<<FRACTIONBITS;
            else
            {
                ALSource->Params.Step = fastf2i(Pitch*FRACTIONONE);
                if(ALSource->Params.Step == 0)
                    ALSource->Params.Step = 1;
            }
            if(ALSource->Params.Step == FRACTIONONE)
                Resampler = PointResampler;

            Channels = ALBuffer->FmtChannels;
            break;
        }
        BufferListItem = BufferListItem->next;
    }
    if(!DirectChannels && Device->Hrtf)
        ALSource->Params.DryMix = SelectHrtfMixer(Resampler);
    else
        ALSource->Params.DryMix = SelectDirectMixer(Resampler);
    ALSource->Params.WetMix = SelectSendMixer(Resampler);

    /* Calculate gains */
    DryGain  = clampf(SourceVolume, MinVolume, MaxVolume);
    DryGain *= ALSource->DirectGain * ListenerGain;
    DryGainHF = ALSource->DirectGainHF;
    for(i = 0;i < NumSends;i++)
    {
        WetGain[i]  = clampf(SourceVolume, MinVolume, MaxVolume);
        WetGain[i] *= ALSource->Send[i].Gain * ListenerGain;
        WetGainHF[i] = ALSource->Send[i].GainHF;
    }

    SrcMatrix = ALSource->Params.Direct.Gains;
    for(i = 0;i < MaxChannels;i++)
    {
        for(c = 0;c < MaxChannels;c++)
            SrcMatrix[i][c] = 0.0f;
    }
    switch(Channels)
    {
    case FmtMono:
        chans = MonoMap;
        num_channels = 1;
        break;

    case FmtStereo:
        if(!(Device->Flags&DEVICE_WIDE_STEREO))
            chans = StereoMap;
        else
        {
            chans = StereoWideMap;
            hwidth = 60.0f * F_PI/180.0f;
        }
        num_channels = 2;
        break;

    case FmtRear:
        chans = RearMap;
        num_channels = 2;
        break;

    case FmtQuad:
        chans = QuadMap;
        num_channels = 4;
        break;

    case FmtX51:
        chans = X51Map;
        num_channels = 6;
        break;

    case FmtX61:
        chans = X61Map;
        num_channels = 7;
        break;

    case FmtX71:
        chans = X71Map;
        num_channels = 8;
        break;
    }

    if(DirectChannels != AL_FALSE)
    {
        for(c = 0;c < num_channels;c++)
        {
            for(i = 0;i < (ALint)Device->NumChan;i++)
            {
                enum Channel chan = Device->Speaker2Chan[i];
                if(chan == chans[c].channel)
                {
                    SrcMatrix[c][chan] += DryGain;
                    break;
                }
            }
        }
    }
    else if(Device->Hrtf)
    {
        for(c = 0;c < num_channels;c++)
        {
            if(chans[c].channel == LFE)
            {
                /* Skip LFE */
                ALSource->Params.Direct.Hrtf.Delay[c][0] = 0;
                ALSource->Params.Direct.Hrtf.Delay[c][1] = 0;
                for(i = 0;i < HRIR_LENGTH;i++)
                {
                    ALSource->Params.Direct.Hrtf.Coeffs[c][i][0] = 0.0f;
                    ALSource->Params.Direct.Hrtf.Coeffs[c][i][1] = 0.0f;
                }
            }
            else
            {
                /* Get the static HRIR coefficients and delays for this
                 * channel. */
                GetLerpedHrtfCoeffs(Device->Hrtf,
                                    0.0f, chans[c].angle,  DryGain,
                                    ALSource->Params.Direct.Hrtf.Coeffs[c],
                                    ALSource->Params.Direct.Hrtf.Delay[c]);
            }
        }
        ALSource->Hrtf.Counter = 0;
    }
    else
    {
        DryGain *= lerp(1.0f, 1.0f/sqrtf(Device->NumChan), hwidth/(F_PI*2.0f));
        for(c = 0;c < num_channels;c++)
        {
            /* Special-case LFE */
            if(chans[c].channel == LFE)
            {
                SrcMatrix[c][chans[c].channel] = DryGain;
                continue;
            }
            ComputeAngleGains(Device, chans[c].angle, hwidth, DryGain,
                              SrcMatrix[c]);
        }
    }
    for(i = 0;i < NumSends;i++)
    {
        ALeffectslot *Slot = ALSource->Send[i].Slot;

        if(!Slot && i == 0)
            Slot = Device->DefaultSlot;
        if(Slot && Slot->effect.type == AL_EFFECT_NULL)
            Slot = NULL;
        ALSource->Params.Slot[i] = Slot;
        ALSource->Params.Send[i].Gain = WetGain[i];
    }

    /* Update filter coefficients. Calculations based on the I3DL2
     * spec. */
    cw = cosf(F_PI*2.0f * LOWPASSFREQREF / Frequency);

    /* We use two chained one-pole filters, so we need to take the
     * square root of the squared gain, which is the same as the base
     * gain. */
    ALSource->Params.Direct.iirFilter.coeff = lpCoeffCalc(DryGainHF, cw);
    for(i = 0;i < NumSends;i++)
    {
        ALfloat a = lpCoeffCalc(WetGainHF[i], cw);
        ALSource->Params.Send[i].iirFilter.coeff = a;
    }
}

ALvoid CalcSourceParams(ALsource *ALSource, const ALCcontext *ALContext)
{
    const ALCdevice *Device = ALContext->Device;
    ALfloat InnerAngle,OuterAngle,Angle,Distance,ClampedDist;
    ALfloat Direction[3],Position[3],SourceToListener[3];
    ALfloat Velocity[3],ListenerVel[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff;
    ALfloat ConeVolume,ConeHF,SourceVolume,ListenerGain;
    ALfloat DopplerFactor, SpeedOfSound;
    ALfloat AirAbsorptionFactor;
    ALfloat RoomAirAbsorption[MAX_SENDS];
    ALbufferlistitem *BufferListItem;
    ALfloat Attenuation;
    ALfloat RoomAttenuation[MAX_SENDS];
    ALfloat MetersPerUnit;
    ALfloat RoomRolloffBase;
    ALfloat RoomRolloff[MAX_SENDS];
    ALfloat DecayDistance[MAX_SENDS];
    ALfloat DryGain;
    ALfloat DryGainHF;
    ALboolean DryGainHFAuto;
    ALfloat WetGain[MAX_SENDS];
    ALfloat WetGainHF[MAX_SENDS];
    ALboolean WetGainAuto;
    ALboolean WetGainHFAuto;
    enum Resampler Resampler;
    ALfloat Matrix[4][4];
    ALfloat Pitch;
    ALuint Frequency;
    ALint NumSends;
    ALfloat cw;
    ALint i, j;

    DryGainHF = 1.0f;
    for(i = 0;i < MAX_SENDS;i++)
        WetGainHF[i] = 1.0f;

    /* Get context/device properties */
    DopplerFactor = ALContext->DopplerFactor * ALSource->DopplerFactor;
    SpeedOfSound  = ALContext->SpeedOfSound * ALContext->DopplerVelocity;
    NumSends      = Device->NumAuxSends;
    Frequency     = Device->Frequency;

    /* Get listener properties */
    ListenerGain   = ALContext->Listener.Gain;
    MetersPerUnit  = ALContext->Listener.MetersPerUnit;
    ListenerVel[0] = ALContext->Listener.Velocity[0];
    ListenerVel[1] = ALContext->Listener.Velocity[1];
    ListenerVel[2] = ALContext->Listener.Velocity[2];
    for(i = 0;i < 4;i++)
    {
        for(j = 0;j < 4;j++)
            Matrix[i][j] = ALContext->Listener.Matrix[i][j];
    }

    /* Get source properties */
    SourceVolume   = ALSource->Gain;
    MinVolume      = ALSource->MinGain;
    MaxVolume      = ALSource->MaxGain;
    Pitch          = ALSource->Pitch;
    Resampler      = ALSource->Resampler;
    Position[0]    = ALSource->Position[0];
    Position[1]    = ALSource->Position[1];
    Position[2]    = ALSource->Position[2];
    Direction[0]   = ALSource->Orientation[0];
    Direction[1]   = ALSource->Orientation[1];
    Direction[2]   = ALSource->Orientation[2];
    Velocity[0]    = ALSource->Velocity[0];
    Velocity[1]    = ALSource->Velocity[1];
    Velocity[2]    = ALSource->Velocity[2];
    MinDist        = ALSource->RefDistance;
    MaxDist        = ALSource->MaxDistance;
    Rolloff        = ALSource->RollOffFactor;
    InnerAngle     = ALSource->InnerAngle;
    OuterAngle     = ALSource->OuterAngle;
    AirAbsorptionFactor = ALSource->AirAbsorptionFactor;
    DryGainHFAuto   = ALSource->DryGainHFAuto;
    WetGainAuto     = ALSource->WetGainAuto;
    WetGainHFAuto   = ALSource->WetGainHFAuto;
    RoomRolloffBase = ALSource->RoomRolloffFactor;
    for(i = 0;i < NumSends;i++)
    {
        ALeffectslot *Slot = ALSource->Send[i].Slot;

        if(!Slot && i == 0)
            Slot = Device->DefaultSlot;
        if(!Slot || Slot->effect.type == AL_EFFECT_NULL)
        {
            Slot = NULL;
            RoomRolloff[i] = 0.0f;
            DecayDistance[i] = 0.0f;
            RoomAirAbsorption[i] = 1.0f;
        }
        else if(Slot->AuxSendAuto)
        {
            RoomRolloff[i] = RoomRolloffBase;
            if(IsReverbEffect(Slot->effect.type))
            {
                RoomRolloff[i] += Slot->effect.Reverb.RoomRolloffFactor;
                DecayDistance[i] = Slot->effect.Reverb.DecayTime *
                                   SPEEDOFSOUNDMETRESPERSEC;
                RoomAirAbsorption[i] = Slot->effect.Reverb.AirAbsorptionGainHF;
            }
            else
            {
                DecayDistance[i] = 0.0f;
                RoomAirAbsorption[i] = 1.0f;
            }
        }
        else
        {
            /* If the slot's auxiliary send auto is off, the data sent to the
             * effect slot is the same as the dry path, sans filter effects */
            RoomRolloff[i] = Rolloff;
            DecayDistance[i] = 0.0f;
            RoomAirAbsorption[i] = AIRABSORBGAINHF;
        }

        ALSource->Params.Slot[i] = Slot;
    }

    /* Transform source to listener space (convert to head relative) */
    if(ALSource->HeadRelative == AL_FALSE)
    {
        /* Translate position */
        Position[0] -= ALContext->Listener.Position[0];
        Position[1] -= ALContext->Listener.Position[1];
        Position[2] -= ALContext->Listener.Position[2];

        /* Transform source vectors */
        aluMatrixVector(Position, 1.0f, Matrix);
        aluMatrixVector(Direction, 0.0f, Matrix);
        aluMatrixVector(Velocity, 0.0f, Matrix);
        /* Transform listener velocity */
        aluMatrixVector(ListenerVel, 0.0f, Matrix);
    }
    else
    {
        /* Transform listener velocity from world space to listener space */
        aluMatrixVector(ListenerVel, 0.0f, Matrix);
        /* Offset the source velocity to be relative of the listener velocity */
        Velocity[0] += ListenerVel[0];
        Velocity[1] += ListenerVel[1];
        Velocity[2] += ListenerVel[2];
    }

    SourceToListener[0] = -Position[0];
    SourceToListener[1] = -Position[1];
    SourceToListener[2] = -Position[2];
    aluNormalize(SourceToListener);
    aluNormalize(Direction);

    /* Calculate distance attenuation */
    Distance = sqrtf(aluDotproduct(Position, Position));
    ClampedDist = Distance;

    Attenuation = 1.0f;
    for(i = 0;i < NumSends;i++)
        RoomAttenuation[i] = 1.0f;
    switch(ALContext->SourceDistanceModel ? ALSource->DistanceModel :
                                            ALContext->DistanceModel)
    {
        case InverseDistanceClamped:
            ClampedDist = clampf(ClampedDist, MinDist, MaxDist);
            if(MaxDist < MinDist)
                break;
            /*fall-through*/
        case InverseDistance:
            if(MinDist > 0.0f)
            {
                if((MinDist + (Rolloff * (ClampedDist - MinDist))) > 0.0f)
                    Attenuation = MinDist / (MinDist + (Rolloff * (ClampedDist - MinDist)));
                for(i = 0;i < NumSends;i++)
                {
                    if((MinDist + (RoomRolloff[i] * (ClampedDist - MinDist))) > 0.0f)
                        RoomAttenuation[i] = MinDist / (MinDist + (RoomRolloff[i] * (ClampedDist - MinDist)));
                }
            }
            break;

        case LinearDistanceClamped:
            ClampedDist = clampf(ClampedDist, MinDist, MaxDist);
            if(MaxDist < MinDist)
                break;
            /*fall-through*/
        case LinearDistance:
            if(MaxDist != MinDist)
            {
                Attenuation = 1.0f - (Rolloff*(ClampedDist-MinDist)/(MaxDist - MinDist));
                Attenuation = maxf(Attenuation, 0.0f);
                for(i = 0;i < NumSends;i++)
                {
                    RoomAttenuation[i] = 1.0f - (RoomRolloff[i]*(ClampedDist-MinDist)/(MaxDist - MinDist));
                    RoomAttenuation[i] = maxf(RoomAttenuation[i], 0.0f);
                }
            }
            break;

        case ExponentDistanceClamped:
            ClampedDist = clampf(ClampedDist, MinDist, MaxDist);
            if(MaxDist < MinDist)
                break;
            /*fall-through*/
        case ExponentDistance:
            if(ClampedDist > 0.0f && MinDist > 0.0f)
            {
                Attenuation = powf(ClampedDist/MinDist, -Rolloff);
                for(i = 0;i < NumSends;i++)
                    RoomAttenuation[i] = powf(ClampedDist/MinDist, -RoomRolloff[i]);
            }
            break;

        case DisableDistance:
            ClampedDist = MinDist;
            break;
    }

    /* Source Gain + Attenuation */
    DryGain = SourceVolume * Attenuation;
    for(i = 0;i < NumSends;i++)
        WetGain[i] = SourceVolume * RoomAttenuation[i];

    /* Distance-based air absorption */
    if(AirAbsorptionFactor > 0.0f && ClampedDist > MinDist)
    {
        ALfloat meters = maxf(ClampedDist-MinDist, 0.0f) * MetersPerUnit;
        DryGainHF *= powf(AIRABSORBGAINHF, AirAbsorptionFactor*meters);
        for(i = 0;i < NumSends;i++)
            WetGainHF[i] *= powf(RoomAirAbsorption[i], AirAbsorptionFactor*meters);
    }

    if(WetGainAuto)
    {
        ALfloat ApparentDist = 1.0f/maxf(Attenuation, 0.00001f) - 1.0f;

        /* Apply a decay-time transformation to the wet path, based on the
         * attenuation of the dry path.
         *
         * Using the apparent distance, based on the distance attenuation, the
         * initial decay of the reverb effect is calculated and applied to the
         * wet path.
         */
        for(i = 0;i < NumSends;i++)
        {
            if(DecayDistance[i] > 0.0f)
                WetGain[i] *= powf(0.001f/*-60dB*/, ApparentDist/DecayDistance[i]);
        }
    }

    /* Calculate directional soundcones */
    Angle = acosf(aluDotproduct(Direction,SourceToListener)) * ConeScale * (360.0f/F_PI);
    if(Angle > InnerAngle && Angle <= OuterAngle)
    {
        ALfloat scale = (Angle-InnerAngle) / (OuterAngle-InnerAngle);
        ConeVolume = lerp(1.0f, ALSource->OuterGain, scale);
        ConeHF = lerp(1.0f, ALSource->OuterGainHF, scale);
    }
    else if(Angle > OuterAngle)
    {
        ConeVolume = ALSource->OuterGain;
        ConeHF = ALSource->OuterGainHF;
    }
    else
    {
        ConeVolume = 1.0f;
        ConeHF = 1.0f;
    }

    DryGain *= ConeVolume;
    if(WetGainAuto)
    {
        for(i = 0;i < NumSends;i++)
            WetGain[i] *= ConeVolume;
    }
    if(DryGainHFAuto)
        DryGainHF *= ConeHF;
    if(WetGainHFAuto)
    {
        for(i = 0;i < NumSends;i++)
            WetGainHF[i] *= ConeHF;
    }

    /* Clamp to Min/Max Gain */
    DryGain = clampf(DryGain, MinVolume, MaxVolume);
    for(i = 0;i < NumSends;i++)
        WetGain[i] = clampf(WetGain[i], MinVolume, MaxVolume);

    /* Apply gain and frequency filters */
    DryGain   *= ALSource->DirectGain * ListenerGain;
    DryGainHF *= ALSource->DirectGainHF;
    for(i = 0;i < NumSends;i++)
    {
        WetGain[i]   *= ALSource->Send[i].Gain * ListenerGain;
        WetGainHF[i] *= ALSource->Send[i].GainHF;
    }

    /* Calculate velocity-based doppler effect */
    if(DopplerFactor > 0.0f)
    {
        ALfloat VSS, VLS;

        if(SpeedOfSound < 1.0f)
        {
            DopplerFactor *= 1.0f/SpeedOfSound;
            SpeedOfSound   = 1.0f;
        }

        VSS = aluDotproduct(Velocity, SourceToListener) * DopplerFactor;
        VLS = aluDotproduct(ListenerVel, SourceToListener) * DopplerFactor;

        Pitch *= clampf(SpeedOfSound-VLS, 1.0f, SpeedOfSound*2.0f - 1.0f) /
                 clampf(SpeedOfSound-VSS, 1.0f, SpeedOfSound*2.0f - 1.0f);
    }

    BufferListItem = ALSource->queue;
    while(BufferListItem != NULL)
    {
        ALbuffer *ALBuffer;
        if((ALBuffer=BufferListItem->buffer) != NULL)
        {
            /* Calculate fixed-point stepping value, based on the pitch, buffer
             * frequency, and output frequency. */
            ALsizei maxstep = STACK_DATA_SIZE/sizeof(ALfloat) /
                              ALSource->NumChannels;
            maxstep -= ResamplerPadding[Resampler] +
                       ResamplerPrePadding[Resampler] + 1;
            maxstep = mini(maxstep, INT_MAX>>FRACTIONBITS);

            Pitch = Pitch * ALBuffer->Frequency / Frequency;
            if(Pitch > (ALfloat)maxstep)
                ALSource->Params.Step = maxstep<<FRACTIONBITS;
            else
            {
                ALSource->Params.Step = fastf2i(Pitch*FRACTIONONE);
                if(ALSource->Params.Step == 0)
                    ALSource->Params.Step = 1;
            }
            if(ALSource->Params.Step == FRACTIONONE)
                Resampler = PointResampler;

            break;
        }
        BufferListItem = BufferListItem->next;
    }
    if(Device->Hrtf)
        ALSource->Params.DryMix = SelectHrtfMixer(Resampler);
    else
        ALSource->Params.DryMix = SelectDirectMixer(Resampler);
    ALSource->Params.WetMix = SelectSendMixer(Resampler);

    if(Device->Hrtf)
    {
        /* Use a binaural HRTF algorithm for stereo headphone playback */
        ALfloat delta, ev = 0.0f, az = 0.0f;

        if(Distance > 0.0f)
        {
            ALfloat invlen = 1.0f/Distance;
            Position[0] *= invlen;
            Position[1] *= invlen;
            Position[2] *= invlen;

            /* Calculate elevation and azimuth only when the source is not at
             * the listener. This prevents +0 and -0 Z from producing
             * inconsistent panning. Also, clamp Y in case FP precision errors
             * cause it to land outside of -1..+1. */
            ev = asinf(clampf(Position[1], -1.0f, 1.0f));
            az = atan2f(Position[0], -Position[2]*ZScale);
        }

        /* Check to see if the HRIR is already moving. */
        if(ALSource->Hrtf.Moving)
        {
            /* Calculate the normalized HRTF transition factor (delta). */
            delta = CalcHrtfDelta(ALSource->Params.Direct.Hrtf.Gain, DryGain,
                                  ALSource->Params.Direct.Hrtf.Dir, Position);
            /* If the delta is large enough, get the moving HRIR target
             * coefficients, target delays, steppping values, and counter. */
            if(delta > 0.001f)
            {
                ALSource->Hrtf.Counter = GetMovingHrtfCoeffs(Device->Hrtf,
                                           ev, az, DryGain, delta,
                                           ALSource->Hrtf.Counter,
                                           ALSource->Params.Direct.Hrtf.Coeffs[0],
                                           ALSource->Params.Direct.Hrtf.Delay[0],
                                           ALSource->Params.Direct.Hrtf.CoeffStep,
                                           ALSource->Params.Direct.Hrtf.DelayStep);
                ALSource->Params.Direct.Hrtf.Gain = DryGain;
                ALSource->Params.Direct.Hrtf.Dir[0] = Position[0];
                ALSource->Params.Direct.Hrtf.Dir[1] = Position[1];
                ALSource->Params.Direct.Hrtf.Dir[2] = Position[2];
            }
        }
        else
        {
            /* Get the initial (static) HRIR coefficients and delays. */
            GetLerpedHrtfCoeffs(Device->Hrtf, ev, az, DryGain,
                                ALSource->Params.Direct.Hrtf.Coeffs[0],
                                ALSource->Params.Direct.Hrtf.Delay[0]);
            ALSource->Hrtf.Counter = 0;
            ALSource->Params.Direct.Hrtf.Gain = DryGain;
            ALSource->Params.Direct.Hrtf.Dir[0] = Position[0];
            ALSource->Params.Direct.Hrtf.Dir[1] = Position[1];
            ALSource->Params.Direct.Hrtf.Dir[2] = Position[2];
        }
    }
    else
    {
        ALfloat (*Matrix)[MaxChannels] = ALSource->Params.Direct.Gains;
        ALfloat DirGain = 0.0f;
        ALfloat AmbientGain;

        for(i = 0;i < MaxChannels;i++)
        {
            for(j = 0;j < MaxChannels;j++)
                Matrix[i][j] = 0.0f;
        }

        /* Normalize the length, and compute panned gains. */
        if(Distance > 0.0f)
        {
            ALfloat invlen = 1.0f/Distance;
            Position[0] *= invlen;
            Position[1] *= invlen;
            Position[2] *= invlen;

            DirGain = sqrtf(Position[0]*Position[0] + Position[2]*Position[2]);
            ComputeAngleGains(Device, atan2f(Position[0], -Position[2]*ZScale), 0.0f,
                              DryGain*DirGain, Matrix[0]);
        }

        /* Adjustment for vertical offsets. Not the greatest, but simple
         * enough. */
        AmbientGain = DryGain * sqrtf(1.0f/Device->NumChan) * (1.0f-DirGain);
        for(i = 0;i < (ALint)Device->NumChan;i++)
        {
            enum Channel chan = Device->Speaker2Chan[i];
            Matrix[0][chan] = maxf(Matrix[0][chan], AmbientGain);
        }
    }
    for(i = 0;i < NumSends;i++)
        ALSource->Params.Send[i].Gain = WetGain[i];

    /* Update filter coefficients. */
    cw = cosf(F_PI*2.0f * LOWPASSFREQREF / Frequency);

    ALSource->Params.Direct.iirFilter.coeff = lpCoeffCalc(DryGainHF, cw);
    for(i = 0;i < NumSends;i++)
    {
        ALfloat a = lpCoeffCalc(WetGainHF[i], cw);
        ALSource->Params.Send[i].iirFilter.coeff = a;
    }
}


static __inline ALfloat aluF2F(ALfloat val)
{ return val; }
static __inline ALint aluF2I(ALfloat val)
{
    if(val > 1.0f) return 2147483647;
    if(val < -1.0f) return -2147483647-1;
    return fastf2i((ALfloat)(val*2147483647.0));
}
static __inline ALuint aluF2UI(ALfloat val)
{ return aluF2I(val)+2147483648u; }
static __inline ALshort aluF2S(ALfloat val)
{ return aluF2I(val)>>16; }
static __inline ALushort aluF2US(ALfloat val)
{ return aluF2S(val)+32768; }
static __inline ALbyte aluF2B(ALfloat val)
{ return aluF2I(val)>>24; }
static __inline ALubyte aluF2UB(ALfloat val)
{ return aluF2B(val)+128; }

#define DECL_TEMPLATE(T, N, func)                                             \
static void Write_##T##_##N(ALCdevice *device, T *RESTRICT buffer,            \
                            ALuint SamplesToDo)                               \
{                                                                             \
    ALfloat (*RESTRICT DryBuffer)[MaxChannels] = device->DryBuffer;           \
    const enum Channel *ChanMap = device->DevChannels;                        \
    ALuint i, j;                                                              \
                                                                              \
    for(j = 0;j < N;j++)                                                      \
    {                                                                         \
        T *RESTRICT out = buffer + j;                                         \
        enum Channel chan = ChanMap[j];                                       \
                                                                              \
        for(i = 0;i < SamplesToDo;i++)                                        \
            out[i*N] = func(DryBuffer[i][chan]);                              \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat, 1, aluF2F)
DECL_TEMPLATE(ALfloat, 2, aluF2F)
DECL_TEMPLATE(ALfloat, 4, aluF2F)
DECL_TEMPLATE(ALfloat, 6, aluF2F)
DECL_TEMPLATE(ALfloat, 7, aluF2F)
DECL_TEMPLATE(ALfloat, 8, aluF2F)

DECL_TEMPLATE(ALuint, 1, aluF2UI)
DECL_TEMPLATE(ALuint, 2, aluF2UI)
DECL_TEMPLATE(ALuint, 4, aluF2UI)
DECL_TEMPLATE(ALuint, 6, aluF2UI)
DECL_TEMPLATE(ALuint, 7, aluF2UI)
DECL_TEMPLATE(ALuint, 8, aluF2UI)

DECL_TEMPLATE(ALint, 1, aluF2I)
DECL_TEMPLATE(ALint, 2, aluF2I)
DECL_TEMPLATE(ALint, 4, aluF2I)
DECL_TEMPLATE(ALint, 6, aluF2I)
DECL_TEMPLATE(ALint, 7, aluF2I)
DECL_TEMPLATE(ALint, 8, aluF2I)

DECL_TEMPLATE(ALushort, 1, aluF2US)
DECL_TEMPLATE(ALushort, 2, aluF2US)
DECL_TEMPLATE(ALushort, 4, aluF2US)
DECL_TEMPLATE(ALushort, 6, aluF2US)
DECL_TEMPLATE(ALushort, 7, aluF2US)
DECL_TEMPLATE(ALushort, 8, aluF2US)

DECL_TEMPLATE(ALshort, 1, aluF2S)
DECL_TEMPLATE(ALshort, 2, aluF2S)
DECL_TEMPLATE(ALshort, 4, aluF2S)
DECL_TEMPLATE(ALshort, 6, aluF2S)
DECL_TEMPLATE(ALshort, 7, aluF2S)
DECL_TEMPLATE(ALshort, 8, aluF2S)

DECL_TEMPLATE(ALubyte, 1, aluF2UB)
DECL_TEMPLATE(ALubyte, 2, aluF2UB)
DECL_TEMPLATE(ALubyte, 4, aluF2UB)
DECL_TEMPLATE(ALubyte, 6, aluF2UB)
DECL_TEMPLATE(ALubyte, 7, aluF2UB)
DECL_TEMPLATE(ALubyte, 8, aluF2UB)

DECL_TEMPLATE(ALbyte, 1, aluF2B)
DECL_TEMPLATE(ALbyte, 2, aluF2B)
DECL_TEMPLATE(ALbyte, 4, aluF2B)
DECL_TEMPLATE(ALbyte, 6, aluF2B)
DECL_TEMPLATE(ALbyte, 7, aluF2B)
DECL_TEMPLATE(ALbyte, 8, aluF2B)

#undef DECL_TEMPLATE

#define DECL_TEMPLATE(T)                                                      \
static void Write_##T(ALCdevice *device, T *buffer, ALuint SamplesToDo)       \
{                                                                             \
    switch(device->FmtChans)                                                  \
    {                                                                         \
        case DevFmtMono:                                                      \
            Write_##T##_1(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtStereo:                                                    \
            Write_##T##_2(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtQuad:                                                      \
            Write_##T##_4(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtX51:                                                       \
        case DevFmtX51Side:                                                   \
            Write_##T##_6(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtX61:                                                       \
            Write_##T##_7(device, buffer, SamplesToDo);                       \
            break;                                                            \
        case DevFmtX71:                                                       \
            Write_##T##_8(device, buffer, SamplesToDo);                       \
            break;                                                            \
    }                                                                         \
}

DECL_TEMPLATE(ALfloat)
DECL_TEMPLATE(ALuint)
DECL_TEMPLATE(ALint)
DECL_TEMPLATE(ALushort)
DECL_TEMPLATE(ALshort)
DECL_TEMPLATE(ALubyte)
DECL_TEMPLATE(ALbyte)

#undef DECL_TEMPLATE

ALvoid aluMixData(ALCdevice *device, ALvoid *buffer, ALsizei size)
{
    ALuint SamplesToDo;
    ALeffectslot **slot, **slot_end;
    ALsource **src, **src_end;
    ALCcontext *ctx;
    int fpuState;
    ALuint i, c;

    fpuState = SetMixerFPUMode();

    while(size > 0)
    {
        SamplesToDo = minu(size, BUFFERSIZE);
        memset(device->DryBuffer, 0, SamplesToDo*MaxChannels*sizeof(ALfloat));

        ALCdevice_Lock(device);
        ctx = device->ContextList;
        while(ctx)
        {
            ALenum DeferUpdates = ctx->DeferUpdates;
            ALenum UpdateSources = AL_FALSE;

            if(!DeferUpdates)
                UpdateSources = ExchangeInt(&ctx->UpdateSources, AL_FALSE);

            /* source processing */
            src = ctx->ActiveSources;
            src_end = src + ctx->ActiveSourceCount;
            while(src != src_end)
            {
                if((*src)->state != AL_PLAYING)
                {
                    --(ctx->ActiveSourceCount);
                    *src = *(--src_end);
                    continue;
                }

                if(!DeferUpdates && (ExchangeInt(&(*src)->NeedsUpdate, AL_FALSE) ||
                                     UpdateSources))
                    ALsource_Update(*src, ctx);

                MixSource(*src, device, SamplesToDo);
                src++;
            }

            /* effect slot processing */
            slot = ctx->ActiveEffectSlots;
            slot_end = slot + ctx->ActiveEffectSlotCount;
            while(slot != slot_end)
            {
                for(c = 0;c < SamplesToDo;c++)
                {
                    (*slot)->WetBuffer[c] += (*slot)->ClickRemoval[0];
                    (*slot)->ClickRemoval[0] -= (*slot)->ClickRemoval[0] * (1.0f/256.0f);
                }
                (*slot)->ClickRemoval[0] += (*slot)->PendingClicks[0];
                (*slot)->PendingClicks[0] = 0.0f;

                if(!DeferUpdates && ExchangeInt(&(*slot)->NeedsUpdate, AL_FALSE))
                    ALeffectState_Update((*slot)->EffectState, device, *slot);

                ALeffectState_Process((*slot)->EffectState, SamplesToDo,
                                      (*slot)->WetBuffer, device->DryBuffer);

                for(i = 0;i < SamplesToDo;i++)
                    (*slot)->WetBuffer[i] = 0.0f;

                slot++;
            }

            ctx = ctx->next;
        }

        slot = &device->DefaultSlot;
        if(*slot != NULL)
        {
            for(c = 0;c < SamplesToDo;c++)
            {
                (*slot)->WetBuffer[c] += (*slot)->ClickRemoval[0];
                (*slot)->ClickRemoval[0] -= (*slot)->ClickRemoval[0] * (1.0f/256.0f);
            }
            (*slot)->ClickRemoval[0] += (*slot)->PendingClicks[0];
            (*slot)->PendingClicks[0] = 0.0f;

            if(ExchangeInt(&(*slot)->NeedsUpdate, AL_FALSE))
                ALeffectState_Update((*slot)->EffectState, device, *slot);

            ALeffectState_Process((*slot)->EffectState, SamplesToDo,
                                  (*slot)->WetBuffer, device->DryBuffer);

            for(i = 0;i < SamplesToDo;i++)
                (*slot)->WetBuffer[i] = 0.0f;
        }
        ALCdevice_Unlock(device);

        /* Click-removal. Could do better; this only really handles immediate
         * changes between updates where a predictive sample could be
         * generated. Delays caused by effects and HRTF aren't caught. */
        if(device->FmtChans == DevFmtMono)
        {
            for(i = 0;i < SamplesToDo;i++)
            {
                device->DryBuffer[i][FrontCenter] += device->ClickRemoval[FrontCenter];
                device->ClickRemoval[FrontCenter] -= device->ClickRemoval[FrontCenter] * (1.0f/256.0f);
            }
            device->ClickRemoval[FrontCenter] += device->PendingClicks[FrontCenter];
            device->PendingClicks[FrontCenter] = 0.0f;
        }
        else if(device->FmtChans == DevFmtStereo)
        {
            /* Assumes the first two channels are FrontLeft and FrontRight */
            for(i = 0;i < SamplesToDo;i++)
            {
                for(c = 0;c < 2;c++)
                {
                    device->DryBuffer[i][c] += device->ClickRemoval[c];
                    device->ClickRemoval[c] -= device->ClickRemoval[c] * (1.0f/256.0f);
                }
            }
            for(c = 0;c < 2;c++)
            {
                device->ClickRemoval[c] += device->PendingClicks[c];
                device->PendingClicks[c] = 0.0f;
            }
            if(device->Bs2b)
            {
                for(i = 0;i < SamplesToDo;i++)
                    bs2b_cross_feed(device->Bs2b, &device->DryBuffer[i][0]);
            }
        }
        else
        {
            for(i = 0;i < SamplesToDo;i++)
            {
                for(c = 0;c < MaxChannels;c++)
                {
                    device->DryBuffer[i][c] += device->ClickRemoval[c];
                    device->ClickRemoval[c] -= device->ClickRemoval[c] * (1.0f/256.0f);
                }
            }
            for(c = 0;c < MaxChannels;c++)
            {
                device->ClickRemoval[c] += device->PendingClicks[c];
                device->PendingClicks[c] = 0.0f;
            }
        }

        if(buffer)
        {
            switch(device->FmtType)
            {
                case DevFmtByte:
                    Write_ALbyte(device, buffer, SamplesToDo);
                    break;
                case DevFmtUByte:
                    Write_ALubyte(device, buffer, SamplesToDo);
                    break;
                case DevFmtShort:
                    Write_ALshort(device, buffer, SamplesToDo);
                    break;
                case DevFmtUShort:
                    Write_ALushort(device, buffer, SamplesToDo);
                    break;
                case DevFmtInt:
                    Write_ALint(device, buffer, SamplesToDo);
                    break;
                case DevFmtUInt:
                    Write_ALuint(device, buffer, SamplesToDo);
                    break;
                case DevFmtFloat:
                    Write_ALfloat(device, buffer, SamplesToDo);
                    break;
            }
        }

        size -= SamplesToDo;
    }

    RestoreFPUMode(fpuState);
}


ALvoid aluHandleDisconnect(ALCdevice *device)
{
    ALCcontext *Context;

    ALCdevice_Lock(device);
    device->Connected = ALC_FALSE;

    Context = device->ContextList;
    while(Context)
    {
        ALsource **src, **src_end;

        src = Context->ActiveSources;
        src_end = src + Context->ActiveSourceCount;
        while(src != src_end)
        {
            if((*src)->state == AL_PLAYING)
            {
                (*src)->state = AL_STOPPED;
                (*src)->BuffersPlayed = (*src)->BuffersInQueue;
                (*src)->position = 0;
                (*src)->position_fraction = 0;
            }
            src++;
        }
        Context->ActiveSourceCount = 0;

        Context = Context->next;
    }
    ALCdevice_Unlock(device);
}
