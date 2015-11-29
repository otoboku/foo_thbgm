#include "SDK/foobar2000.h"
#include "helpers/helpers.h"

#include <map>
#include <string>
#include <vector>
using namespace std;
#include "thxmlparser.h"
#include "config.h"

#define needloop (loopforever || current_loop < loopcount)

static mainmenu_group_factory g_mainmenu_group(g_mainmenu_group_id, 
	mainmenu_groups::playback, mainmenu_commands::sort_priority_dontcare);
static cfg_bool loopforever(cfg_loop_forever, true);
static cfg_uint loopcount(cfg_loop_count, 1);
static cfg_bool read_thbgm_info(cfg_thbgm_readinfo, false);


const t_uint32 deltaread = 1024;
//double seek_seconds;
//t_uint32 samplerate;
//string pack;
//t_filesize m_offset;
//t_filesize m_headlen;
//t_filesize m_looplen;
//t_filesize m_totallen;
//t_filesize current_sample;
//t_uint32 current_loop;


class mainmenu_loopsetting : public mainmenu_commands {
public:
	enum {
		loop_forever,
		loop_count,
		thbgm_readinfo,
		count
	};

	t_uint32 get_command_count() {
		return count;
	}

	GUID get_command(t_uint32 p_index) {
		switch(p_index) {
			case loop_forever: return guid_loop_forever;
			case loop_count: return guid_loop_count;
			case thbgm_readinfo : return guid_thbgm_readinfo;
		}
	}

	void get_name(t_uint32 p_index, pfc::string_base &p_out) {
		switch(p_index) {
			case loop_forever:
				p_out = "ThBGM loop forever";
				break;
			case loop_count: 
				p_out = "ThBGM loop x";
				char counts[12];
				_itoa_s(loopcount, counts, 10);
				p_out.add_string(counts);
				break;
			case thbgm_readinfo:
				p_out = "ThBGM read raw metadata";
				break;
		}
	}

	bool get_description(t_uint32 p_index, pfc::string_base &p_out) {
		switch(p_index) {
			case loop_forever:
				p_out = "infinity loop mode";
				return true;
			case loop_count:
				p_out = "specify loop count";
				return true;
			case thbgm_readinfo:
				p_out = "read thbgm file infomation";
				return true;
		}
	}

	GUID get_parent() {
		return g_mainmenu_group_id;
	}

	bool get_display(t_uint32 p_index, pfc::string_base &p_text,
									t_uint32 &p_flags) {
		get_name(p_index, p_text);
		switch(p_index) {
			case loop_forever:
				p_flags = loopforever ? mainmenu_commands::flag_radiochecked : 0;
				break;
			case loop_count:
				p_flags = loopforever ? 0 : mainmenu_commands::flag_radiochecked;
				break;
			case thbgm_readinfo:
				p_flags = read_thbgm_info ? mainmenu_commands::flag_checked : 0;
				break;
		}
		return true;
	}

	void execute(t_uint32 p_index,service_ptr_t<service_base> p_callback) {
		switch(p_index) {
			case loop_forever:
				loopforever = true;
				break;
			case loop_count:
				loopforever = false;
				loopcount = atoi(_InputBox("Please specify loop counts"));
				if(!loopcount) loopcount = 1;
				if(loopcount > 65535) loopcount = 65535;
				break;
			case thbgm_readinfo:
				read_thbgm_info = !read_thbgm_info;
				break;
		}
	}
};

void truncate_chunk(audio_chunk & p_chunk, t_size p_samples) {
	p_chunk.set_sample_count(p_samples);
	p_chunk.set_data_size(p_samples * p_chunk.get_channel_count());
}

class input_raw {
private:
	file::ptr m_file;
	input_decoder::ptr decoder;
	bool first_packet;
	t_filesize current_sample;
	double seek_seconds;


public:
	t_uint32 current_loop;

	bool isWave;

	t_uint32 bits;
	t_uint32 channels;
	t_uint32 samplewidth;

	t_uint32 samplerate;
	t_filesize m_offset;
	t_filesize m_headlen;
	t_filesize m_looplen;
	t_filesize m_totallen;

	t_filesize m_filepos;
	t_filesize m_maxseeksize;
	pfc::array_t<t_uint8> m_buffer;

	inline t_uint64 length_to_samples(t_filesize p_length) {
		return p_length / samplewidth;
	}

	inline t_filesize samples_to_length(t_uint64 p_samples) {
		return p_samples * samplewidth;
	}

	void open(const char *p_path, t_input_open_reason p_reason,
						bool isWave, abort_callback &p_abort) {
		m_file.release();
		if(isWave) {
			input_open_file_helper(m_file, p_path, p_reason, p_abort);
		} else {
			decoder.release();
			input_entry::g_open_for_decoding(decoder, m_file, p_path, p_abort);
		}
	}

	void init(abort_callback &p_abort)
	{
		if(isWave) {
			m_buffer.set_size(samples_to_length(deltaread));
		}
		//decoder->initialize(0, input_flag_playback, p_abort);
		current_loop = 1;
	}

	t_size read(void *buffer, t_size size, abort_callback &p_abort) {
		return m_file->read(buffer, size, p_abort);
	}

	void raw_seek(t_filesize size, abort_callback &p_abort) {
		m_file->seek(size, p_abort);
	}

	void get_info(file_info &p_info, abort_callback &p_abort) {
		decoder->get_info(0, p_info, p_abort);
	}

	bool get_dynamic_info(file_info & p_out, double & p_timestamp_delta) {
		return decoder->get_dynamic_info(p_out,p_timestamp_delta);
	}

	bool get_dynamic_info_track(file_info & p_out, double & p_timestamp_delta) {
		return decoder->get_dynamic_info_track(p_out,p_timestamp_delta);
	}

	bool run(audio_chunk &p_chunk, abort_callback &p_abort) {
		if(first_packet) {
			decoder->initialize(0, input_flag_playback, p_abort);
			double time_offset = audio_math::samples_to_time(m_offset, samplerate);
			decoder->seek(seek_seconds + time_offset, p_abort);
			first_packet = false;
		}
		
		bool result = decoder->run(p_chunk, p_abort);
		if(!needloop) {
			return result;
		} else {
			if(current_sample > m_totallen) return false; //Decoding failure at xxx (bad allocation)
			t_size read_count = p_chunk.get_sample_count();
			current_sample += read_count;
			if(current_sample >= m_totallen || !result) {
				double looptime = audio_math::samples_to_time(m_headlen, samplerate);
				truncate_chunk(p_chunk, read_count  - (current_sample - m_totallen));
				current_sample = m_headlen;
				decoder->seek(audio_math::samples_to_time(m_offset + m_headlen, samplerate), p_abort);
				//seek(looptime);
				if(!loopforever) current_loop++;
				//return run(p_chunk, p_abort);
			}
			return result;
		}
	}

	bool run_wav(audio_chunk &p_chunk, abort_callback &p_abort) {
			if(m_filepos >= m_maxseeksize) return false; 
			t_size deltaread_size = pfc::downcast_guarded<t_size>
				(pfc::min_t(samples_to_length(deltaread), m_maxseeksize - m_filepos));
			t_size deltaread_done = 
				read(m_buffer.get_ptr(), deltaread_size, p_abort);
			m_filepos += deltaread_done;
		
			if(needloop && m_filepos >= m_maxseeksize) {
				//t_filesize remain = samples_to_length(deltaread) - deltaread_done;
				//m_filepos = m_headlen + remain;
				m_filepos = m_headlen;
				raw_seek(m_offset + m_headlen, p_abort);
				//deltaread_done += raw.read(m_buffer.get_ptr()
				//	+ deltaread_done, remain, p_abort);
				if(!loopforever) current_loop++;
			}

			p_chunk.set_data_fixedpoint(m_buffer.get_ptr(), deltaread_done,
				samplerate, channels, bits,
				audio_chunk::g_guess_channel_config(channels));

			// return false时 最后一块不播放
			//if(!needloop && m_filepos >= m_maxseeksize) {
			//	return false;
			//}
			return true;
	}

	void seek(double p_seconds, abort_callback &p_abort) {
		current_sample = audio_math::time_to_samples(p_seconds, samplerate);
		seek_seconds = p_seconds;
		first_packet = true;
		double time_offset = audio_math::samples_to_time(m_offset, samplerate);
		decoder->seek(p_seconds + time_offset, p_abort);
	}

	void seek_wav(double p_seconds, abort_callback &p_abort) {
		t_uint64 samples = audio_math::time_to_samples(p_seconds, samplerate);
		m_filepos = samples * samplewidth;
		m_maxseeksize = m_headlen + m_looplen;
		if(m_filepos > m_maxseeksize)
			m_filepos = m_maxseeksize;
		raw_seek(m_offset + m_filepos, p_abort);
	}
};

class input_thxml {
protected:
	file::ptr m_file;
	pfc::string8 basepath;
	vector<map<string, string> > bgmlist;
	pfc::string8 title;
	pfc::string8 artist;
	t_uint32 bits;
	t_uint32 channels;
	t_uint32 samplewidth;
	pfc::string8 encoding;
	pfc::string8 codec;
	pfc::string8 totaltracks;

	t_uint32 samplerate;
	string pack;
	t_filesize m_offset;
	t_filesize m_headlen;
	t_filesize m_looplen;
	t_filesize m_totallen;
	
	bool isWave;
	bool isArchive;
	input_raw * raw;

	inline t_uint64 length_to_samples(t_filesize p_length) {
		return p_length / samplewidth;
	}

	inline t_filesize samples_to_length(t_uint64 p_samples) {
		return p_samples * samplewidth;
	}

	void open_raw(t_uint32 p_subsong, abort_callback &p_abort) {
		string bgm_path = bgmlist[p_subsong]["file"];
		string real_path;
		if(isArchive) {
			t_uint32 fl = basepath.length() + bgm_path.find_first_of('|');
			char flstr[4];
			_itoa_s(fl, flstr, 10);
			real_path = "unpack://";
			real_path.append(pack);
			real_path.append("|");
			real_path.append(flstr);
			real_path.append("|");
			real_path.append(basepath);
		} else {
			real_path = basepath;
		}
		real_path.append(bgm_path);
		input_raw * ptr = new service_impl_t<input_raw>();
		//raw = service_impl_t<input_raw>();
		
ptr->isWave = isWave;

ptr->bits = bits;
ptr->channels = channels;
ptr->samplewidth = samplewidth;

ptr->samplerate = samplerate;
ptr->m_offset = m_offset;
ptr->m_headlen = m_headlen;
ptr->m_looplen = m_looplen;
ptr->m_totallen = m_totallen;
ptr->init(p_abort);
		ptr->open(real_path.c_str(), input_open_decode, isWave, p_abort);
		raw = ptr;
	}

/*	// get_info 用，使用open_raw 会因为current_sample定义在类内无法播放
	input_raw* open_raw_temp(t_uint32 p_subsong, abort_callback &p_abort) {
		string bgm_path = bgmlist[p_subsong]["file"];
		string real_path;
		if(isArchive) {
			t_uint32 fl = basepath.length() + bgm_path.find_first_of('|');
			char flstr[4];
			_itoa_s(fl, flstr, 10);
			real_path = "unpack://";
			real_path.append(pack);
			real_path.append("|");
			real_path.append(flstr);
			real_path.append("|");
			real_path.append(basepath);
		} else {
			real_path = basepath;
		}
		real_path.append(bgm_path);
		input_raw* raw_temp = new service_impl_t<input_raw>();
		raw_temp->open(real_path.c_str(), input_open_decode, isWave, p_abort);
		return raw_temp;
	}
*/
	// get_info 用，使用open_raw 会因为current_sample定义在类内无法播放
	void get_raw_info(t_uint32 p_subsong, file_info &p_info, abort_callback &p_abort) {
		string bgm_path = bgmlist[p_subsong]["file"];
		string real_path;
		if(isArchive) {
			t_uint32 fl = basepath.length() + bgm_path.find_first_of('|');
			char flstr[4];
			_itoa_s(fl, flstr, 10);
			real_path = "unpack://";
			real_path.append(pack);
			real_path.append("|");
			real_path.append(flstr);
			real_path.append("|");
			real_path.append(basepath);
		} else {
			real_path = basepath;
		}
		real_path.append(bgm_path);

		file::ptr m_file;
		input_info_reader::ptr reader;
		//input_raw* raw_temp = new service_impl_t<input_raw_reader>();
		//raw_temp->open(real_path.c_str(), input_open_info_read, 0, p_abort);
		//raw_temp->get_info(p_info, p_abort);
		//delete raw_temp;
		input_entry::g_open_for_info_read(reader, m_file, real_path.c_str(), p_abort);
		reader->get_info(0, p_info, p_abort);
	}

public:
	void open(file::ptr p_filehint, const char *p_path,
						t_input_open_reason p_reason, abort_callback &p_abort) {
		if(p_reason == input_open_info_write) {
			throw exception_io_unsupported_format();
		}
		m_file = p_filehint;
		input_open_file_helper(m_file, p_path, p_reason, p_abort);
		
		const char *pathlen = strrchr(p_path, '\\');
		int baselen = pathlen - p_path + 1;
		basepath.set_string(p_path, baselen);
		t_filesize length = m_file->get_size(p_abort);
		thxmlparser parser = thxmlparser();
		parser.parsestream(
					(char*) (m_file->read_string_ex(length, p_abort).get_ptr()));
		bgmlist = parser.thbgm;
		pack = bgmlist[0]["pack"];
		isArchive = pack != "";
	}

	t_uint32 get_subsong_count() {
		t_filesize count = bgmlist.size() - 1;
		totaltracks = pfc::format_int(count);
		return count;
	}

	t_uint32 get_subsong(t_uint32 p_subsong) {
		isWave = bgmlist[++p_subsong]["codec"] == "PCM" && !isArchive;
		codec = bgmlist[p_subsong]["codec"].c_str();
		encoding = bgmlist[p_subsong]["encoding"].c_str();
		title = bgmlist[p_subsong]["title"].c_str();
		artist = bgmlist[p_subsong]["artist"].c_str();

		string pos = bgmlist[p_subsong]["pos"];
		size_t pos_head = pos.find(',');
		size_t head_loop = pos.rfind(',');
		m_offset = _atoi64(pos.substr(0, pos_head).c_str());
		m_headlen = _atoi64(pos.substr(++pos_head, head_loop).c_str());
		m_looplen = _atoi64(pos.substr(++head_loop).c_str());
		m_totallen = m_headlen + m_looplen;

		samplerate = atoi(bgmlist[p_subsong]["samplerate"].c_str());
		bits = atoi(bgmlist[p_subsong]["bits"].c_str());
		channels = atoi(bgmlist[p_subsong]["channels"].c_str());
		samplewidth = bits * channels / 8;

		assert(p_subsong < get_subsong_count() + 1);
		return p_subsong;
	}

	void get_info(t_uint32 p_subsong, file_info &p_info,
								abort_callback &p_abort) {
		bool isWave;
		pfc::string8 title;
		pfc::string8 artist;
		t_uint32 bits;
		t_uint32 channels;
		t_uint32 samplewidth;
		pfc::string8 encoding;
		pfc::string8 codec;
		pfc::string8 totaltracks;

		t_uint32 samplerate;
		t_filesize m_offset;
		t_filesize m_headlen;
		t_filesize m_looplen;
		t_filesize m_totallen;

		isWave = bgmlist[p_subsong]["codec"] == "PCM" && !isArchive;
		codec = bgmlist[p_subsong]["codec"].c_str();
		encoding = bgmlist[p_subsong]["encoding"].c_str();
		title = bgmlist[p_subsong]["title"].c_str();
		artist = bgmlist[p_subsong]["artist"].c_str();

		string pos = bgmlist[p_subsong]["pos"];
		size_t pos_head = pos.find(',');
		size_t head_loop = pos.rfind(',');
		m_offset = _atoi64(pos.substr(0, pos_head).c_str());
		m_headlen = _atoi64(pos.substr(++pos_head, head_loop).c_str());
		m_looplen = _atoi64(pos.substr(++head_loop).c_str());
		m_totallen = m_headlen + m_looplen;

		samplerate = atoi(bgmlist[p_subsong]["samplerate"].c_str());
		bits = atoi(bgmlist[p_subsong]["bits"].c_str());
		channels = atoi(bgmlist[p_subsong]["channels"].c_str());
		samplewidth = bits * channels / 8;

		totaltracks = pfc::format_int(bgmlist.size() - 1);

		// 首先调用，解决 播放时 批量reload info出错，貌似不是这个问题...
		if(!isWave && read_thbgm_info) {
			get_raw_info(p_subsong, p_info, p_abort);
			//input_raw *raw_temp = open_raw_temp(p_subsong, p_abort);
			//raw_temp->get_info(p_info, p_abort);
			//delete raw_temp;
		}
		else
		{
			p_info.info_set_int("samplerate", samplerate);
			p_info.info_set_int("channels", channels);
			p_info.info_set_int("bitspersample", bits);
			p_info.info_set("encoding", encoding);
			p_info.info_set("codec", codec);
		}

		if(isWave) {
			p_info.info_set_bitrate((bits * channels * samplerate + 500) / 1000);
			p_info.set_length(audio_math::samples_to_time(
				length_to_samples(m_headlen + m_looplen), samplerate));
		} else {
			p_info.set_length(audio_math::samples_to_time(
				m_headlen + m_looplen, samplerate));
		}

		p_info.meta_set("ALBUM", bgmlist[0]["album"].c_str());
		p_info.meta_set("ALBUM ARTIST", bgmlist[0]["albumartist"].c_str());
		p_info.meta_set("TITLE", title);
		p_info.meta_set("ARTIST", artist);
		p_info.meta_set("TRACKNUMBER", pfc::format_int(p_subsong));
		p_info.meta_set("TOTALTRACKS", totaltracks);
		//p_info.meta_set("LOOPSTART", pfc::format_int(m_list[p_subsong-1].m_loopstart));
		//p_info.meta_set("LOOPLENGTH", pfc::format_int(m_list[p_subsong-1].m_looplength));
		if(isWave) {
			p_info.meta_set("LOOPSTART", pfc::format_int(m_headlen/samplewidth));
			p_info.meta_set("LOOPLENGTH", pfc::format_int(m_looplen/samplewidth));	
		} else {
			p_info.meta_set("LOOPSTART", pfc::format_int(m_headlen));
			p_info.meta_set("LOOPLENGTH", pfc::format_int(m_looplen));	
		}
	}

	t_filestats get_file_stats(abort_callback & p_abort) {
		return m_file->get_stats(p_abort);
	}

	void decode_initialize(t_uint32 p_subsong, unsigned p_flags,
												abort_callback &p_abort) {
		open_raw(get_subsong(--p_subsong), p_abort);



		//current_loop = 1;

		decode_seek(0, p_abort);
	}

	bool decode_run(audio_chunk &p_chunk, abort_callback &p_abort) {
		if(isWave) {
			return raw->run_wav(p_chunk, p_abort);
		} else {
			return raw->run(p_chunk, p_abort);
		}
	}

	void decode_seek(double p_seconds, abort_callback &p_abort) {
		if(isWave) {
			raw->seek_wav(p_seconds, p_abort);
		} else {
			raw->seek(p_seconds, p_abort);
		}
	}

	bool decode_can_seek() {return true;}
//	bool decode_get_dynamic_info_track(file_info &p_out,
//			double &p_timestamp_delta) {return false;}
//	bool decode_get_dynamic_info(file_info &p_info,
//			double &p_timestamp_delta) {return false;}
	bool decode_get_dynamic_info(file_info &p_out, double &p_timestamp_delta) {
		//if(!isWave && read_thbgm_info) {
		if(!isWave) {
			return raw->get_dynamic_info(p_out,p_timestamp_delta);
		}
		else
			return false;
	}

	bool decode_get_dynamic_info_track(file_info &p_out, double &p_timestamp_delta) {
		if(!isWave) {
			return raw->get_dynamic_info_track(p_out,p_timestamp_delta);
		}
		else
			return false;
	}

	void decode_on_idle(abort_callback &p_abort) {}
	void retag_set_info(t_uint32 p_subsong, const file_info &p_info,
			abort_callback &p_abort) {}
	void retag_commit(abort_callback &p_abort) {}

	static bool g_is_our_content_type(const char *p_content_type) {return false;}
	static bool g_is_our_path(const char *p_path, const char *p_extension) {
		return stricmp_utf8(p_extension, "thxml") == 0;
	}
};

static mainmenu_commands_factory_t<mainmenu_loopsetting> loopsetting_factory;
static input_factory_t<input_thxml> g_input_thbgm_factory;
DECLARE_FILE_TYPE("Touhou-like BGM XML-Tag File", "*.thxml");
DECLARE_COMPONENT_VERSION("ThBGM Player", "1.1.120522.22.moe", 
"Play BGM files of Touhou and some related doujin games.\n\n"
"If you have any feature request and bug report,\n"
"feel free to contact me at my E-mail address below.\n\n"
"(C) nyfair <nyfair2012@gmail.com>");
