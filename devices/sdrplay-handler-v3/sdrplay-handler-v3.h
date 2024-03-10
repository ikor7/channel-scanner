#
/*
 *    Copyright (C) 2014 .. 2019
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of dab cmdline programs
 *
 *    Dab cmdline is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    Dab cmdline is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with dab cmdline; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#include        <stdint.h>
#include        <thread>
#include        <atomic>
#include        <vector>
#include	"ringbuffer.h"
#include	"device-handler.h"
#include	<sdrplay_api.h>

class	xml_fileWriter;

class	sdrplayHandler_v3: public deviceHandler {
public:
		sdrplayHandler_v3       (RingBuffer<std::complex<float>> *,
                                         const std::string &,
                                         int32_t        frequency,
                                         int16_t        ppmCorrection,
                                         int16_t        GRdB,
                                         int16_t        lnaState,
                                         bool           autogain,
                                         uint16_t       deviceIndex,
                                         int16_t        antenna);
		~sdrplayHandler_v3	();
	bool    restartReader           (int32_t);
        void    stopReader		();
	void	startDumping		(const std::string &);
	void	stopDumping		();
        int16_t bitDepth		();
	std::string deviceName		();
//	The following items should be visible from outsize
//	the callback functions refer to them
        RingBuffer<std::complex<float>> *_I_Buffer;
        float   denominator;
	xml_fileWriter  *xmlWriter;
        std::atomic<bool> dumping;

        void    update_PowerOverload (sdrplay_api_EventParamsT *params);
        std::atomic<bool>       running;
        std::atomic<bool>	radioRuns;;
private:
        void                    run             ();
        sdrplay_api_DeviceT             *chosenDevice;
        sdrplay_api_DeviceParamsT       *deviceParams;
        sdrplay_api_CallbackFnsT        cbFns;
        sdrplay_api_RxChannelParamsT    *chParams;
        std::thread                     threadHandle;

        bool                    failFlag;
        int16_t                 hwVersion;
        int32_t                 vfoFrequency;
	std::atomic<int32_t>	newFrequency;
        int16_t                 ppmCorrection;
        int16_t                 GRdB;
        int16_t                 lnaState;
        bool                    autogain;
        uint16_t                deviceIndex;
	int16_t			antenna;
	int16_t			nrBits;

	std::string		deviceModel;
	std::string		recorderVersion;
	FILE			*xmlFile;
};

