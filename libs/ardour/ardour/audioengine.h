/*
 * Copyright (C) 2006-2011 David Robillard <d@drobilla.net>
 * Copyright (C) 2006-2019 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2007-2012 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2012-2019 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2013-2015 Tim Mayberry <mojofunk@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __ardour_audioengine_h__
#define __ardour_audioengine_h__

#ifdef WAF_BUILD
#include "libardour-config.h"
#endif

#include <iostream>
#include <list>
#include <set>
#include <cmath>
#include <exception>
#include <string>

#include <glibmm/threads.h>

#include "pbd/signals.h"
#include "pbd/pthread_utils.h"
#include "pbd/g_atomic_compat.h"

#include "ardour/ardour.h"
#include "ardour/data_type.h"
#include "ardour/session_handle.h"
#include "ardour/libardour_visibility.h"
#include "ardour/types.h"
#include "ardour/chan_count.h"
#include "ardour/port_manager.h"

class MTDM;

namespace ARDOUR {

class InternalPort;
class MidiPort;
class MIDIDM;
class Port;
class Session;
class ProcessThread;
class AudioBackend;
struct AudioBackendInfo;

class LIBARDOUR_API AudioEngine : public PortManager, public SessionHandlePtr
{
  public:

	static AudioEngine* create ();

	virtual ~AudioEngine ();

	int discover_backends();
	std::vector<const AudioBackendInfo*> available_backends() const;
	std::string current_backend_name () const;
	boost::shared_ptr<AudioBackend> set_backend (const std::string&, const std::string& arg1, const std::string& arg2);
	boost::shared_ptr<AudioBackend> current_backend() const { return _backend; }
	bool setup_required () const;

	ProcessThread* main_thread() const { return _main_thread; }

	/* START BACKEND PROXY API
	 *
	 * See audio_backend.h for full documentation and semantics. These wrappers
	 * just forward to a backend implementation.
	 */

	int            start (bool for_latency_measurement=false);
	int            stop (bool for_latency_measurement=false);
	int            freewheel (bool start_stop);
	float          get_dsp_load() const ;
	void           transport_start ();
	void           transport_stop ();
	TransportState transport_state ();
	void           transport_locate (samplepos_t pos);
	samplepos_t     transport_sample();
	samplecnt_t     sample_rate () const;
	pframes_t      samples_per_cycle () const;
	int            usecs_per_cycle () const;
	size_t         raw_buffer_size (DataType t);
	samplepos_t     sample_time ();
	samplepos_t     sample_time_at_cycle_start ();
	pframes_t      samples_since_cycle_start ();
	bool           get_sync_offset (pframes_t& offset) const;

	std::string    get_last_backend_error () const { return _last_backend_error_string; }

	int            create_process_thread (boost::function<void()> func);
	int            join_process_threads ();
	bool           in_process_thread ();
	uint32_t       process_thread_count ();

	/* internal backends
	 * -20 : main thread
	 * -21 : additional I/O threads e.g. MIDI
	 * -22 : client/process threads
	 *
	 * search for
	 * - pbd_realtime_pthread_create
	 * - pbd_set_thread_priority
	 */
	virtual int    client_real_time_priority () { return PBD_RT_PRI_PROC; }

	int            backend_reset_requested();
	void           request_backend_reset();
	void           request_device_list_update();
	void           launch_device_control_app();

	bool           is_realtime() const;

	// for the user which hold state_lock to check if reset operation is pending
	bool           is_reset_requested() const { return g_atomic_int_get (&_hw_reset_request_count); }

	int set_device_name (const std::string&);
	int set_sample_rate (float);
	int set_buffer_size (uint32_t);
	int set_interleaved (bool yn);
	int set_input_channels (uint32_t);
	int set_output_channels (uint32_t);
	int set_systemic_input_latency (uint32_t);
	int set_systemic_output_latency (uint32_t);

	/* END BACKEND PROXY API */

	bool freewheeling() const { return _freewheeling; }
	bool running() const { return _running; }

	Glib::Threads::Mutex& process_lock() { return _process_lock; }
	Glib::Threads::RecMutex& state_lock() { return _state_lock; }

	int request_buffer_size (pframes_t samples) {
		return set_buffer_size (samples);
	}

	samplecnt_t processed_samples() const { return _processed_samples; }

	void set_session (Session *);
	void remove_session (); // not a replacement for SessionHandle::session_going_away()
	Session* session() const { return _session; }

	class NoBackendAvailable : public std::exception {
	    public:
		virtual const char *what() const throw() { return "could not connect to engine backend"; }
	};

	void split_cycle (pframes_t offset);

	int  reset_timebase ();

	void update_latencies ();

	/* this signal is sent for every process() cycle while freewheeling.
	   (the regular process() call to session->process() is not made)
	*/

	PBD::Signal1<void, pframes_t> Freewheel;

	PBD::Signal0<void> Xrun;

	/** this signal is emitted if the sample rate changes */
	PBD::Signal1<void, samplecnt_t> SampleRateChanged;

	/** this signal is emitted if the buffer size changes */
	PBD::Signal1<void, pframes_t> BufferSizeChanged;

	/** this signal is emitted if the device cannot operate properly */
	PBD::Signal0<void> DeviceError;

	/* this signal is emitted if the device list changed */

	PBD::Signal0<void> DeviceListChanged;

	/* this signal is sent if the backend ever disconnects us */

	PBD::Signal1<void,const char*> Halted;

	/* these two are emitted when the engine itself is
	   started and stopped
	*/

	PBD::Signal1<void,uint32_t> Running;
	PBD::Signal0<void> Stopped;

	/* these two are emitted when a device reset is initiated/finished
	 */

	PBD::Signal0<void> DeviceResetStarted;
	PBD::Signal0<void> DeviceResetFinished;

	static AudioEngine* instance() { return _instance; }
	static void destroy();
	void died ();

	/* The backend will cause these at the appropriate time(s) */
	int  process_callback (pframes_t nframes);
	int  buffer_size_change (pframes_t nframes);
	int  sample_rate_change (pframes_t nframes);
	void freewheel_callback (bool);
	void timebase_callback (TransportState state, pframes_t nframes, samplepos_t pos, int new_position);
	int  sync_callback (TransportState state, samplepos_t position);
	int  port_registration_callback ();
	void latency_callback (bool for_playback);
	void halted_callback (const char* reason);

	/* checks if current thread is properly set up for audio processing */
	static bool thread_initialised_for_audio_processing ();

	/* sets up the process callback thread */
	static void thread_init_callback (void *);

	/* latency measurement */

	MTDM* mtdm() { return _mtdm; }
	MIDIDM* mididm() { return _mididm; }

	int  prepare_for_latency_measurement ();
	int  start_latency_detection (bool);
	void stop_latency_detection ();
	void set_latency_input_port (const std::string&);
	void set_latency_output_port (const std::string&);
	uint32_t latency_signal_delay () const { return _latency_signal_latency; }

	enum LatencyMeasurement {
		MeasureNone,
		MeasureAudio,
		MeasureMIDI
	};

	LatencyMeasurement measuring_latency () const { return _measuring_latency; }

	/* These two are used only in builds where SILENCE_AFTER_SECONDS was
	 * set. BecameSilent will be emitted when the audioengine goes silent.
	 * reset_silence_countdown() can be used to reset the silence
	 * countdown, whose duration will be reduced to half of its previous
	 * value.
	 */

	PBD::Signal0<void> BecameSilent;
	void reset_silence_countdown ();

	void add_pending_port_deletion (Port*);
	void queue_latency_update (bool);

  private:
	AudioEngine ();

	static AudioEngine*       _instance;

	Glib::Threads::Mutex       _process_lock;
	Glib::Threads::RecMutex    _state_lock;
	Glib::Threads::Cond        session_removed;
	bool                       session_remove_pending;
	sampleoffset_t             session_removal_countdown;
	gain_t                     session_removal_gain;
	gain_t                     session_removal_gain_step;
	bool                      _running;
	bool                      _freewheeling;
	/// number of samples between each check for changes in monitor input
	samplecnt_t                monitor_check_interval;
	/// time of the last monitor check in samples
	samplecnt_t                last_monitor_check;
	/// the number of samples processed since start() was called
	samplecnt_t               _processed_samples;
	Glib::Threads::Thread*     m_meter_thread;
	ProcessThread*            _main_thread;
	MTDM*                     _mtdm;
	MIDIDM*                   _mididm;
	LatencyMeasurement        _measuring_latency;
	PortEngine::PortPtr       _latency_input_port;
	PortEngine::PortPtr       _latency_output_port;
	samplecnt_t               _latency_flush_samples;
	std::string               _latency_input_name;
	std::string               _latency_output_name;
	samplecnt_t               _latency_signal_latency;
	bool                      _stopped_for_latency;
	bool                      _started_for_latency;
	bool                      _in_destructor;

	std::string               _last_backend_error_string;

	Glib::Threads::Thread*    _hw_reset_event_thread;
	GATOMIC_QUAL gint         _hw_reset_request_count;
	Glib::Threads::Cond       _hw_reset_condition;
	Glib::Threads::Mutex      _reset_request_lock;
	GATOMIC_QUAL gint         _stop_hw_reset_processing;
	Glib::Threads::Thread*    _hw_devicelist_update_thread;
	GATOMIC_QUAL gint         _hw_devicelist_update_count;
	Glib::Threads::Cond       _hw_devicelist_update_condition;
	Glib::Threads::Mutex      _devicelist_update_lock;
	GATOMIC_QUAL gint         _stop_hw_devicelist_processing;
	uint32_t                  _start_cnt;
	uint32_t                  _init_countdown;
	GATOMIC_QUAL gint         _pending_playback_latency_callback;
	GATOMIC_QUAL gint         _pending_capture_latency_callback;

	void start_hw_event_processing();
	void stop_hw_event_processing();
	void do_reset_backend();
	void do_devicelist_update();

	typedef std::map<std::string,AudioBackendInfo*> BackendMap;
	BackendMap _backends;
	AudioBackendInfo* backend_discover (const std::string&);
	void drop_backend ();

#ifdef SILENCE_AFTER
	samplecnt_t _silence_countdown;
	uint32_t   _silence_hit_cnt;
#endif

};

} // namespace ARDOUR

#endif /* __ardour_audioengine_h__ */
