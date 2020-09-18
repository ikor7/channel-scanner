#
/*
 *    Copyright (C) 2020
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of dab-cmdline
 *
 *    dab-cmdline is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation version 2 of the License.
 *
 *    dab-cmdline is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with dab-cmdline if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include	"pluto-handler.h"
#include	<unistd.h>
#include	"ad9361.h"
#include	<stdio.h>
#include	<sndfile.h>
/* static scratch mem for strings */
static char tmpstr[64];

/* helper function generating channel names */
static
char*	get_ch_name (const char* type, int id) {
        snprintf (tmpstr, sizeof(tmpstr), "%s%d", type, id);
        return tmpstr;
}

enum iodev { RX, TX };

/* returns ad9361 phy device */
static
struct iio_device* get_ad9361_phy (struct iio_context *ctx) {
struct iio_device *dev = iio_context_find_device (ctx, "ad9361-phy");
	return dev;
}

/* finds AD9361 streaming IIO devices */
static
bool get_ad9361_stream_dev (struct iio_context *ctx,
	                    enum iodev d, struct iio_device **dev) {
	switch (d) {
	case TX:
	   *dev = iio_context_find_device (ctx, "cf-ad9361-dds-core-lpc");
	   return *dev != NULL;

	case RX:
	   *dev = iio_context_find_device (ctx, "cf-ad9361-lpc");
	   return *dev != NULL;

	default: 
	   return false;
	}
}

/* finds AD9361 streaming IIO channels */
static
bool get_ad9361_stream_ch (__notused struct iio_context *ctx,
	                   enum iodev d, struct iio_device *dev,
	                   int chid, struct iio_channel **chn) {
	*chn = iio_device_find_channel (dev,
	                                get_ch_name ("voltage", chid),
	                                d == TX);
	if (!*chn)
	   *chn = iio_device_find_channel (dev,
	                                   get_ch_name ("altvoltage", chid),
	                                   d == TX);
	return *chn != NULL;
}

/* finds AD9361 phy IIO configuration channel with id chid */
static
bool	get_phy_chan (struct iio_context *ctx,
	              enum iodev d, int chid, struct iio_channel **chn) {
	switch (d) {
	   case RX:
	      *chn = iio_device_find_channel (get_ad9361_phy (ctx),
	                                      get_ch_name ("voltage", chid),
	                                      false);
	      return *chn != NULL;

	   case TX:
	      *chn = iio_device_find_channel (get_ad9361_phy (ctx),
	                                      get_ch_name ("voltage", chid),
	                                      true);
	      return *chn != NULL;

	   default: 
	      return false;
	}
}

/* finds AD9361 local oscillator IIO configuration channels */
static
bool	get_lo_chan (struct iio_context *ctx,
	             enum iodev d, struct iio_channel **chn) {
// LO chan is always output, i.e. true
	switch (d) {
	   case RX:
	      *chn = iio_device_find_channel (get_ad9361_phy (ctx),
	                                      get_ch_name ("altvoltage", 0),
	                                      true);
	      return *chn != NULL;

	   case TX:
	      *chn = iio_device_find_channel (get_ad9361_phy (ctx),
	                                      get_ch_name ("altvoltage", 1),
	                                      true);
	      return *chn != NULL;

	   default: 
	      return false;
	}
}

/* applies streaming configuration through IIO */
bool cfg_ad9361_streaming_ch (struct iio_context *ctx,
	                      struct stream_cfg *cfg,
	                      enum iodev type, int chid) {
struct iio_channel *chn = NULL;
int	ret;

// Configure phy and lo channels
	printf("* Acquiring AD9361 phy channel %d\n", chid);
	if (!get_phy_chan (ctx, type, chid, &chn)) {
	   return false;
	}
	ret = iio_channel_attr_write (chn,
	                              "rf_port_select", cfg -> rfport);
	if (ret < 0)
	   return false;
	ret = iio_channel_attr_write_longlong (chn,
	                                       "rf_bandwidth", cfg -> bw_hz);
	ret = iio_channel_attr_write_longlong (chn,
	                                       "sampling_frequency",
	                                       cfg -> fs_hz);

// Configure LO channel
	printf("* Acquiring AD9361 %s lo channel\n", type == TX ? "TX" : "RX");
	if (!get_lo_chan (ctx, type, &chn)) {
	   return false;
	}
	ret = iio_channel_attr_write_longlong (chn,
	                                       "frequency", cfg -> lo_hz);
	return true;
}

static inline
std::complex<float> cmul (std::complex<float> x, float y) {
	return std::complex<float> (real (x) * y, imag (x) * y);
}
static
SNDFILE	*testFile;
SF_INFO	sf_info;

	plutoHandler::plutoHandler  (RingBuffer<std::complex<float>>*b,
	                             int32_t	frequency,
	                             int	gainValue,
	                             bool	agcMode,
	                             int32_t	fmFrequency):
	                               deviceHandler (b) {
	this	-> _I_Buffer		= b;
	this	-> _O_Buffer		= new RingBuffer<std::complex<float>> (32 * 32768);
	this	-> ctx			= nullptr;
	this	-> rxbuf		= nullptr;
	this	-> txbuf		= nullptr;
	this	-> rx0_i		= nullptr;
	this	-> rx0_q		= nullptr;
	this	-> tx0_i		= nullptr;
	this	-> tx0_q		= nullptr;

	rx_cfg. bw_hz			= 1536000;
	rx_cfg. fs_hz			= RX_RATE;
	rx_cfg. lo_hz			= frequency;
	rx_cfg. rfport			= "A_BALANCED";

	tx_cfg. bw_hz			= 192000;
	tx_cfg. fs_hz			= FM_RATE;
	tx_cfg. lo_hz			= fmFrequency;
	tx_cfg. rfport			= "A";
//
//	step 1: establish a context
//
	ctx	= iio_create_default_context ();
	if (ctx == nullptr) {
	   ctx = iio_create_local_context ();
	}

	if (ctx == nullptr) {
	   ctx = iio_create_network_context ("pluto.local");
	}

	if (ctx == nullptr) {
	   ctx = iio_create_network_context ("192.168.2.1");
	}

	if (ctx == nullptr) {
	   fprintf (stderr, "No pluto found, fatal\n");
	   throw (24);
	}
//

	sf_info. samplerate	= 192000;
	sf_info. channels	= 2;
	sf_info. format		= SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	testFile		= sf_open ("/tmp/testfile.wav",
	                          SFM_WRITE, &sf_info);
	if (iio_context_get_devices_count (ctx) <= 0) {
	   fprintf (stderr, "no devices, fatal");
	   throw (25);
	}

	fprintf (stderr, "* Acquiring AD9361 streaming devices\n");
	if (!get_ad9361_stream_dev (ctx, TX, &tx)) {
	   fprintf (stderr, "No TX device found\n");
	   throw (26);
	}

	if (!get_ad9361_stream_dev (ctx, RX, &rx)) {
	   fprintf (stderr, "No RX device found\n");
	   throw (27);
	}

	fprintf (stderr, "* Configuring AD9361 for streaming\n");
	if (!cfg_ad9361_streaming_ch (ctx, &rx_cfg, RX, 0)) {
	   fprintf (stderr, "RX port 0 not found\n");
	   throw (28);
	}

	struct iio_channel *chn;
	if (get_phy_chan (ctx, RX, 0, &chn)) {
	   int ret;
	   if (agcMode)
	      ret = iio_channel_attr_write (chn,
	                                    "gain_control_mode",
	                                    "slow_attack");
	   else {
	      ret = iio_channel_attr_write (chn,
	                                    "gain_control_mode",
	                                    "manual");
	      ret = iio_channel_attr_write_longlong (chn,
	                                             "hardwaregain",
	                                             gainValue);
	   }

	   if (ret < 0)
	      fprintf (stderr, "setting agc/gain did not work\n");
	}

	if (get_phy_chan (ctx, TX, 0, &chn)) {
	   int ret;
	   ret = iio_channel_attr_write_longlong (chn,
	                                             "hardwaregain",
	                                             0);
	   if (ret < 0)
	      fprintf (stderr, "setting transmit gain did not work\n");
	}
	else
	   fprintf (stderr, "cound not obtain TX channel\n");

	if (!cfg_ad9361_streaming_ch (ctx, &tx_cfg, TX, 0)) {
	   fprintf (stderr, "TX port 0 not found");
	   throw (29);
	}

	fprintf (stderr, "* Initializing AD9361 IIO streaming channels\n");
	if (!get_ad9361_stream_ch (ctx, RX, rx, 0, &rx0_i)) {
	   fprintf (stderr, "RX chan i not found");
	   throw (30);
	}
	
	if (!get_ad9361_stream_ch (ctx, RX, rx, 1, &rx0_q)) {
	   fprintf (stderr,"RX chan q not found");
	   throw (31);
	}
	
	if (!get_ad9361_stream_ch (ctx, TX, tx, 0, &tx0_i)) {
	   fprintf (stderr, "TX chan i not found");
	   throw (32);
	}

	if (!get_ad9361_stream_ch(ctx, TX, tx, 1, &tx0_q)) {
	   fprintf (stderr, "TX chan q not found");
	   throw (33);
	}

        iio_channel_enable (rx0_i);
        iio_channel_enable (rx0_q);
        iio_channel_enable (tx0_i);
        iio_channel_enable (tx0_q);

        rxbuf = iio_device_create_buffer (rx, 256*1024, false);
	if (rxbuf == nullptr) {
	   fprintf (stderr, "could not create RX buffer, fatal");
	   iio_context_destroy (ctx);
	   throw (35);
	}

        txbuf = iio_device_create_buffer (tx, 1024 * 1024, false);
	if (txbuf == nullptr) {
	   fprintf (stderr, "could not create TX buffer, fatal");
	   iio_context_destroy (ctx);
	   throw (35);
	}

	iio_buffer_set_blocking_mode (rxbuf, true);
//
//	and set up interpolation table
	float	divider		= (float)DIVIDER;
	float	denominator	= DAB_RATE / divider;
	for (int i = 0; i < DAB_RATE / DIVIDER; i ++) {
           float inVal  = float (RX_RATE / divider);
           mapTable_int [i]     =  int (floor (i * (inVal / denominator)));
           mapTable_float [i]   = i * (inVal / denominator) - mapTable_int [i];
        }
        convIndex       = 0;

	(void)  ad9361_set_bb_rate_custom_filter_manual (get_ad9361_phy (ctx),
	                                                 RX_RATE,
	                                                 1540000 / 2,
	                                                 1.1 * 1540000 / 2,
	                                                 1920000,
	                                                 1536000);

	running. store (false);
}

	plutoHandler::~plutoHandler () {
	stopReader ();
	iio_buffer_destroy (rxbuf);
	iio_buffer_destroy (txbuf);
	iio_context_destroy (ctx);
}

void	plutoHandler::sendSample	(std::complex<float> v) {
std::complex<float> buf [FM_RATE / 192000];
float temp [2];
	temp [0] = real (v);
	temp [1] = imag (v);
	memset (buf, 0, FM_RATE / 192000 * sizeof (std::complex<float>));
	buf [0] = cmul (v, 10);
	_O_Buffer	-> putDataIntoBuffer (buf, FM_RATE / 192000);
//	sf_writef_float (testFile, temp, 1);
}

bool	plutoHandler::restartReader	(int32_t freq) {
struct	iio_channel *lo_channel;

	if (running. load())
	   return true;		// should not happen

	get_lo_chan (ctx, RX, &lo_channel);
	rx_cfg. lo_hz	= freq;
	int ret	= iio_channel_attr_write_longlong
	                             (lo_channel,
	                                   "frequency", rx_cfg. lo_hz);
	if (ret < 0) {
	   fprintf (stderr, "error in selected frequency\n");
	   return false;
	}

	threadHandle_r	= std::thread (&plutoHandler::run_receiver, this);
	threadHandle_t	= std::thread (&plutoHandler::run_transmitter, this);
	return true;
}

void	plutoHandler::stopReader() {
	if (!running. load())
	   return;
	running. store (false);
	usleep (50000);
	threadHandle_r. join ();
	threadHandle_t. join ();
}

void	plutoHandler::run_receiver	() {
char	*p_end, *p_dat;
int	p_inc;
int	nbytes_rx;
std::complex<float> localBuf [DAB_RATE / DIVIDER];

	running. store (true);
	while (running. load ()) {
	   nbytes_rx	= iio_buffer_refill	(rxbuf);
	   p_inc	= iio_buffer_step	(rxbuf);
	   p_end	= (char *)(iio_buffer_end  (rxbuf));

	   for (p_dat = (char *)iio_buffer_first (rxbuf, rx0_i);
	        p_dat < p_end; p_dat += p_inc) {
	      const int16_t i_p = ((int16_t *)p_dat) [0];
	      const int16_t q_p = ((int16_t *)p_dat) [1];
	      std::complex<float>sample = std::complex<float> (i_p / 2048.0,
	                                                       q_p / 2048.0);
	      convBuffer [convIndex ++] = sample;
	      if (convIndex > CONV_SIZE) {
	         for (int j = 0; j < DAB_RATE / DIVIDER; j ++) {
	            int16_t inpBase	= mapTable_int [j];
	            float   inpRatio	= mapTable_float [j];
	            localBuf [j]	= cmul (convBuffer [inpBase + 1],
	                                                          inpRatio) +
                                     cmul (convBuffer [inpBase], 1 - inpRatio);
                 }
	         _I_Buffer ->  putDataIntoBuffer (localBuf,
	                                          DAB_RATE / DIVIDER);
	         convBuffer [0] = convBuffer [CONV_SIZE];
	         convIndex = 1;
	      }
	   }
	}
}

void	plutoHandler::run_transmitter () {
char	*p_begin	= (char *)(iio_buffer_start (txbuf));
char	*p_end		= (char *)(iio_buffer_end  (txbuf));
int	p_inc		= iio_buffer_step          (txbuf);
int	bufferLength	= int (p_end - p_begin);
int	sourceSize	= bufferLength / (2 * sizeof (int16_t));
std::complex<float> buffer [sourceSize];

	fprintf (stderr, "Transmitter starts\n");
	while (running. load ()) {
	   int amount	= _O_Buffer -> getDataFromBuffer (buffer, sourceSize);
	   int index	= 0;
	   for (char *p_dat = (char *)iio_buffer_first (txbuf, tx0_i);
                p_dat < p_end; p_dat += p_inc) {
	      int16_t *i_p = &((int16_t *)p_dat) [0];
	      int16_t *q_p = &((int16_t *)p_dat) [1];
	      if (index < amount) {
	         *i_p = (int16_t)(real (buffer [index]) * 4096) << 4;
	         *q_p = (int16_t)(imag (buffer [index]) * 4096) << 4;
	         index ++;
	      }
	      else {
	         *i_p	= 0;
	         *q_p	= 0;
	      }
	   }
	   int  nbytes_tx	= iio_buffer_push (txbuf);
//	   sf_writef_float (testFile, (float *)buffer, bufferLength / sizeof (std::complex<float>));
//	   fprintf (stderr, "%d %d\n", nbytes_tx, amount);
	}
	sf_close (testFile);
}