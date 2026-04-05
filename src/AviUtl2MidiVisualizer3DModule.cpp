#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "..\vendor\aviutl2_sdk\module2.h"
#include "..\vendor\aviutl2_sdk\logger2.h"

#ifndef AMV3D_VERSION
#define AMV3D_VERSION L"vdev"
#endif

namespace {

struct Reader {
	const uint8_t* data = nullptr;
	size_t size = 0;
	size_t pos = 0;

	bool has(size_t length) const {
		return pos + length <= size;
	}

	bool read_u8(uint8_t& value) {
		if (!has(1)) return false;
		value = data[pos++];
		return true;
	}

	bool read_u16be(uint16_t& value) {
		if (!has(2)) return false;
		value = (static_cast<uint16_t>(data[pos]) << 8) | static_cast<uint16_t>(data[pos + 1]);
		pos += 2;
		return true;
	}

	bool read_u32be(uint32_t& value) {
		if (!has(4)) return false;
		value = (static_cast<uint32_t>(data[pos]) << 24) |
			(static_cast<uint32_t>(data[pos + 1]) << 16) |
			(static_cast<uint32_t>(data[pos + 2]) << 8) |
			static_cast<uint32_t>(data[pos + 3]);
		pos += 4;
		return true;
	}

	bool read_varlen(uint32_t& value) {
		value = 0;
		for (int i = 0; i < 4; ++i) {
			uint8_t byte = 0;
			if (!read_u8(byte)) return false;
			value = (value << 7) | (byte & 0x7f);
			if ((byte & 0x80) == 0) return true;
		}
		return false;
	}

	bool skip(size_t length) {
		if (!has(length)) return false;
		pos += length;
		return true;
	}

	uint8_t peek() const {
		return has(1) ? data[pos] : 0;
	}
};

struct TempoSegment {
	uint64_t tick = 0;
	double seconds = 0.0;
	uint32_t usec_per_quarter = 500000;
};

struct TimeSignatureSegment {
	uint64_t tick = 0;
	int numerator = 4;
	int denominator = 4;
};

struct TickValuePoint {
	uint64_t tick = 0;
	double value = 0.0;
};

struct TimeValuePoint {
	double time = 0.0;
	double value = 0.0;
};

struct ChannelParseState {
	int expression = 127;
	int bend_raw = 8192;
	double bend_range = 2.0;
	int rpn_msb = 127;
	int rpn_lsb = 127;
	int data_msb = 2;
	int data_lsb = 0;
	std::vector<TickValuePoint> expression_points;
	std::vector<TickValuePoint> bend_points;

	ChannelParseState() {
		expression_points.push_back({ 0, 127.0 });
		bend_points.push_back({ 0, 0.0 });
	}
};

struct ChannelTimeline {
	std::vector<TimeValuePoint> expression;
	std::vector<TimeValuePoint> bend;
};

struct BeatMarker {
	double time = 0.0;
	int beat_in_bar = 0;
};

struct NoteEvent {
	uint64_t start_tick = 0;
	uint64_t end_tick = 0;
	double start_sec = 0.0;
	double end_sec = 0.0;
	int track = 0;
	int channel = 0;
	int note = 0;
	int velocity = 0;
	double attack_alpha = 1.0;
};

struct OpenNote {
	uint64_t start_tick = 0;
	uint8_t velocity = 0;
	double attack_alpha = 1.0;
};

struct MidiCache {
	std::wstring source_path;
	std::vector<TempoSegment> tempos;
	std::vector<TimeSignatureSegment> time_signatures;
	std::vector<NoteEvent> notes;
	std::vector<std::array<ChannelTimeline, 16>> timelines;
	std::vector<BeatMarker> beats;
	double duration_sec = 0.0;
	double max_note_duration_sec = 0.0;
	uint64_t max_tick = 0;
	int track_count = 0;
	int min_note = 127;
	int max_note = 0;
	uint16_t division = 0;
	bool loaded = false;
};

MidiCache g_cache;
std::string g_last_error;
LOG_HANDLE* g_logger = nullptr;
bool g_limit_warned = false;

void set_last_error(const std::string& message) {
	g_last_error = message;
}

void log_warn(const wchar_t* message) {
	if (g_logger && g_logger->warn) {
		g_logger->warn(g_logger, message);
	}
}

void log_error(const wchar_t* message) {
	if (g_logger && g_logger->error) {
		g_logger->error(g_logger, message);
	}
}

std::wstring utf8_to_wide(std::string_view text) {
	if (text.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring wide(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
	return wide;
}

bool load_file(const std::wstring& path, std::vector<uint8_t>& bytes, std::string& error) {
	FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
		error = "MIDI file could not be opened";
		return false;
	}

	if (_fseeki64(file, 0, SEEK_END) != 0) {
		error = "Failed to seek MIDI file";
		fclose(file);
		return false;
	}
	const auto size = _ftelli64(file);
	if (size < 0) {
		error = "Failed to read MIDI file size";
		fclose(file);
		return false;
	}
	if (_fseeki64(file, 0, SEEK_SET) != 0) {
		error = "Failed to rewind MIDI file";
		fclose(file);
		return false;
	}

	bytes.resize(static_cast<size_t>(size));
	if (!bytes.empty()) {
		if (fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
			error = "Failed to read MIDI file bytes";
			fclose(file);
			return false;
		}
	}
	fclose(file);
	return true;
}

double current_bend_semitones(const ChannelParseState& state) {
	return (static_cast<double>(state.bend_raw) - 8192.0) * state.bend_range / 8192.0;
}

void push_tick_point(std::vector<TickValuePoint>& points, uint64_t tick, double value) {
	if (!points.empty() && points.back().tick == tick) {
		points.back().value = value;
		return;
	}
	if (!points.empty() && points.back().value == value) {
		return;
	}
	points.push_back({ tick, value });
}

void close_open_notes(std::array<std::vector<OpenNote>, 16 * 128>& active_notes, uint64_t end_tick, int track_index, std::vector<NoteEvent>& notes) {
	for (int key = 0; key < static_cast<int>(active_notes.size()); ++key) {
		auto& stack = active_notes[key];
		while (!stack.empty()) {
			const auto open = stack.back();
			stack.pop_back();
			NoteEvent note{};
			note.start_tick = open.start_tick;
			note.end_tick = end_tick;
			note.track = track_index;
			note.channel = key / 128;
			note.note = key % 128;
			note.velocity = open.velocity;
			note.attack_alpha = open.attack_alpha;
			notes.push_back(note);
		}
	}
}

bool parse_track(
	const uint8_t* track_data,
	size_t track_size,
	int track_index,
	std::vector<TempoSegment>& tempos,
	std::vector<TimeSignatureSegment>& time_signatures,
	std::vector<NoteEvent>& notes,
	std::array<ChannelParseState, 16>& channels,
	uint64_t& end_tick,
	std::string& error
) {
	Reader reader{ track_data, track_size, 0 };
	std::array<std::vector<OpenNote>, 16 * 128> active_notes;
	uint8_t running_status = 0;
	uint64_t tick = 0;

	while (reader.pos < reader.size) {
		uint32_t delta = 0;
		if (!reader.read_varlen(delta)) {
			error = "Invalid delta-time in MIDI track";
			return false;
		}
		tick += delta;

		uint8_t status = 0;
		uint8_t data1 = 0;
		bool has_data1 = false;
		const auto next = reader.peek();
		if (next < 0x80) {
			if (running_status == 0) {
				error = "Running status used before a status byte";
				return false;
			}
			status = running_status;
			if (!reader.read_u8(data1)) {
				error = "Unexpected EOF while reading MIDI event";
				return false;
			}
			has_data1 = true;
		} else {
			if (!reader.read_u8(status)) {
				error = "Unexpected EOF while reading MIDI status";
				return false;
			}
			if (status < 0xf0) {
				running_status = status;
			}
		}

		if (status == 0xff) {
			uint8_t meta_type = 0;
			uint32_t length = 0;
			if (!reader.read_u8(meta_type) || !reader.read_varlen(length) || !reader.has(length)) {
				error = "Invalid MIDI meta event";
				return false;
			}
			const auto meta = reader.data + reader.pos;
			reader.pos += length;
			if (meta_type == 0x51 && length == 3) {
				const uint32_t tempo =
					(static_cast<uint32_t>(meta[0]) << 16) |
					(static_cast<uint32_t>(meta[1]) << 8) |
					static_cast<uint32_t>(meta[2]);
				tempos.push_back({ tick, 0.0, tempo });
			} else if (meta_type == 0x58 && length >= 2) {
				const int numerator = std::max(1, static_cast<int>(meta[0]));
				const int denominator = (meta[1] < 31) ? (1 << meta[1]) : 4;
				time_signatures.push_back({ tick, numerator, denominator });
			}
			if (meta_type == 0x2f) {
				break;
			}
			continue;
		}

		if (status == 0xf0 || status == 0xf7) {
			uint32_t length = 0;
			if (!reader.read_varlen(length) || !reader.skip(length)) {
				error = "Invalid MIDI sysex event";
				return false;
			}
			continue;
		}

		const uint8_t kind = status & 0xf0;
		const int channel = status & 0x0f;
		if (kind < 0x80 || kind > 0xe0) {
			error = "Unsupported MIDI channel event";
			return false;
		}

		auto read_data_byte = [&](uint8_t& value) -> bool {
			if (has_data1) {
				value = data1;
				has_data1 = false;
				return true;
			}
			return reader.read_u8(value);
		};

		uint8_t a = 0;
		uint8_t b = 0;
		if (!read_data_byte(a)) {
			error = "Unexpected EOF while reading MIDI event data";
			return false;
		}
		if (kind != 0xc0 && kind != 0xd0) {
			if (!reader.read_u8(b)) {
				error = "Unexpected EOF while reading MIDI event data";
				return false;
			}
		}

		auto& state = channels[channel];
		if (kind == 0x90 && b != 0) {
			const double attack_alpha = (static_cast<double>(b) / 127.0) * (static_cast<double>(state.expression) / 127.0);
			active_notes[channel * 128 + a].push_back({ tick, b, attack_alpha });
			continue;
		}

		if (kind == 0x80 || (kind == 0x90 && b == 0)) {
			auto& stack = active_notes[channel * 128 + a];
			if (!stack.empty()) {
				const auto open = stack.back();
				stack.pop_back();
				NoteEvent note{};
				note.start_tick = open.start_tick;
				note.end_tick = tick;
				note.track = track_index;
				note.channel = channel;
				note.note = a;
				note.velocity = open.velocity;
				note.attack_alpha = open.attack_alpha;
				notes.push_back(note);
			}
			continue;
		}

		if (kind == 0xb0) {
			if (a == 11) {
				state.expression = b;
				push_tick_point(state.expression_points, tick, static_cast<double>(state.expression));
			} else if (a == 101) {
				state.rpn_msb = b;
			} else if (a == 100) {
				state.rpn_lsb = b;
			} else if (a == 6) {
				state.data_msb = b;
				if (state.rpn_msb == 0 && state.rpn_lsb == 0) {
					state.bend_range = static_cast<double>(state.data_msb) + static_cast<double>(state.data_lsb) / 100.0;
					push_tick_point(state.bend_points, tick, current_bend_semitones(state));
				}
			} else if (a == 38) {
				state.data_lsb = b;
				if (state.rpn_msb == 0 && state.rpn_lsb == 0) {
					state.bend_range = static_cast<double>(state.data_msb) + static_cast<double>(state.data_lsb) / 100.0;
					push_tick_point(state.bend_points, tick, current_bend_semitones(state));
				}
			}
			continue;
		}

		if (kind == 0xe0) {
			state.bend_raw = (static_cast<int>(b) << 7) | static_cast<int>(a);
			push_tick_point(state.bend_points, tick, current_bend_semitones(state));
		}
	}

	end_tick = tick;
	close_open_notes(active_notes, end_tick, track_index, notes);
	return true;
}

void build_tempo_map(std::vector<TempoSegment>& tempos, uint16_t division) {
	if (tempos.empty() || tempos.front().tick != 0) {
		tempos.push_back({ 0, 0.0, 500000 });
	}

	std::stable_sort(tempos.begin(), tempos.end(), [](const TempoSegment& lhs, const TempoSegment& rhs) {
		return lhs.tick < rhs.tick;
	});

	std::vector<TempoSegment> normalized;
	for (const auto& tempo : tempos) {
		if (!normalized.empty() && normalized.back().tick == tempo.tick) {
			normalized.back().usec_per_quarter = tempo.usec_per_quarter;
		} else {
			normalized.push_back(tempo);
		}
	}

	double seconds = 0.0;
	for (size_t i = 0; i < normalized.size(); ++i) {
		normalized[i].seconds = seconds;
		if (i + 1 < normalized.size()) {
			const auto delta_ticks = normalized[i + 1].tick - normalized[i].tick;
			seconds += (static_cast<double>(delta_ticks) * static_cast<double>(normalized[i].usec_per_quarter)) /
				(static_cast<double>(division) * 1000000.0);
		}
	}
	tempos = std::move(normalized);
}

void build_time_signature_map(std::vector<TimeSignatureSegment>& time_signatures) {
	if (time_signatures.empty() || time_signatures.front().tick != 0) {
		time_signatures.push_back({ 0, 4, 4 });
	}

	std::stable_sort(time_signatures.begin(), time_signatures.end(), [](const TimeSignatureSegment& lhs, const TimeSignatureSegment& rhs) {
		return lhs.tick < rhs.tick;
	});

	std::vector<TimeSignatureSegment> normalized;
	for (const auto& signature : time_signatures) {
		if (!normalized.empty() && normalized.back().tick == signature.tick) {
			normalized.back().numerator = signature.numerator;
			normalized.back().denominator = signature.denominator;
		} else {
			normalized.push_back(signature);
		}
	}
	time_signatures = std::move(normalized);
}

double tick_to_seconds(const std::vector<TempoSegment>& tempos, uint16_t division, uint64_t tick) {
	const auto it = std::upper_bound(tempos.begin(), tempos.end(), tick, [](uint64_t value, const TempoSegment& tempo) {
		return value < tempo.tick;
	});
	const auto& tempo = (it == tempos.begin()) ? tempos.front() : *std::prev(it);
	const auto delta_ticks = tick - tempo.tick;
	return tempo.seconds + (static_cast<double>(delta_ticks) * static_cast<double>(tempo.usec_per_quarter)) /
		(static_cast<double>(division) * 1000000.0);
}

double tick_to_seconds_double(const std::vector<TempoSegment>& tempos, uint16_t division, double tick) {
	const auto it = std::upper_bound(tempos.begin(), tempos.end(), tick, [](double value, const TempoSegment& tempo) {
		return value < static_cast<double>(tempo.tick);
	});
	const auto& tempo = (it == tempos.begin()) ? tempos.front() : *std::prev(it);
	const double delta_ticks = tick - static_cast<double>(tempo.tick);
	return tempo.seconds + (delta_ticks * static_cast<double>(tempo.usec_per_quarter)) /
		(static_cast<double>(division) * 1000000.0);
}

std::vector<TimeValuePoint> convert_points(const std::vector<TickValuePoint>& points, const std::vector<TempoSegment>& tempos, uint16_t division) {
	std::vector<TimeValuePoint> converted;
	converted.reserve(points.size());
	for (const auto& point : points) {
		const double time = tick_to_seconds(tempos, division, point.tick);
		if (!converted.empty() && converted.back().time == time) {
			converted.back().value = point.value;
		} else if (!converted.empty() && converted.back().value == point.value) {
			continue;
		} else {
			converted.push_back({ time, point.value });
		}
	}
	return converted;
}

void build_beat_markers(
	std::vector<BeatMarker>& beats,
	const std::vector<TimeSignatureSegment>& time_signatures,
	const std::vector<TempoSegment>& tempos,
	uint16_t division,
	uint64_t max_tick
) {
	beats.clear();
	if (division == 0) return;

	const double end_tick = static_cast<double>(max_tick) + static_cast<double>(division);
	for (size_t i = 0; i < time_signatures.size(); ++i) {
		const auto& signature = time_signatures[i];
		const double segment_start = static_cast<double>(signature.tick);
		const double segment_end = (i + 1 < time_signatures.size())
			? static_cast<double>(time_signatures[i + 1].tick)
			: end_tick;
		const double beat_ticks = (static_cast<double>(division) * 4.0) / static_cast<double>(std::max(1, signature.denominator));
		if (beat_ticks <= 0.0 || segment_start > segment_end) continue;

		int beat_in_bar = 0;
		for (double tick = segment_start; tick <= segment_end + 1e-9; tick += beat_ticks) {
			if (i + 1 < time_signatures.size() && tick >= segment_end - 1e-9) break;
			beats.push_back({ tick_to_seconds_double(tempos, division, tick), beat_in_bar });
			beat_in_bar = (beat_in_bar + 1) % std::max(1, signature.numerator);
		}
	}
}

double sample_timeline(const std::vector<TimeValuePoint>& points, double time, double default_value) {
	if (points.empty()) return default_value;
	const auto it = std::upper_bound(points.begin(), points.end(), time, [](double value, const TimeValuePoint& point) {
		return value < point.time;
	});
	if (it == points.begin()) return points.front().value;
	return std::prev(it)->value;
}

bool parse_midi_file(const std::wstring& path, MidiCache& cache, std::string& error) {
	std::vector<uint8_t> bytes;
	if (!load_file(path, bytes, error)) {
		return false;
	}

	Reader reader{ bytes.data(), bytes.size(), 0 };
	uint32_t chunk_tag = 0;
	uint32_t chunk_size = 0;
	if (!reader.read_u32be(chunk_tag) || !reader.read_u32be(chunk_size) || chunk_tag != 0x4d546864) {
		error = "MIDI header chunk (MThd) not found";
		return false;
	}
	if (chunk_size < 6 || !reader.has(chunk_size)) {
		error = "Invalid MIDI header size";
		return false;
	}

	uint16_t format = 0;
	uint16_t track_count = 0;
	uint16_t division = 0;
	if (!reader.read_u16be(format) || !reader.read_u16be(track_count) || !reader.read_u16be(division)) {
		error = "Failed to parse MIDI header";
		return false;
	}
	reader.skip(chunk_size - 6);
	if ((division & 0x8000) != 0) {
		error = "SMPTE time division is not supported in this prototype";
		return false;
	}

	std::vector<TempoSegment> tempos;
	std::vector<TimeSignatureSegment> time_signatures;
	std::vector<NoteEvent> notes;
	std::vector<std::array<ChannelParseState, 16>> parse_states(track_count);
	uint64_t max_tick = 0;

	for (uint16_t track_index = 0; track_index < track_count; ++track_index) {
		if (!reader.read_u32be(chunk_tag) || !reader.read_u32be(chunk_size) || chunk_tag != 0x4d54726b) {
			error = "MIDI track chunk (MTrk) not found";
			return false;
		}
		if (!reader.has(chunk_size)) {
			error = "Invalid MIDI track size";
			return false;
		}

		uint64_t track_end_tick = 0;
		if (!parse_track(reader.data + reader.pos, chunk_size, static_cast<int>(track_index), tempos, time_signatures, notes, parse_states[track_index], track_end_tick, error)) {
			return false;
		}
		max_tick = std::max(max_tick, track_end_tick);
		reader.pos += chunk_size;
	}

	build_tempo_map(tempos, division);
	build_time_signature_map(time_signatures);
	for (auto& note : notes) {
		note.start_sec = tick_to_seconds(tempos, division, note.start_tick);
		note.end_sec = tick_to_seconds(tempos, division, note.end_tick);
		if (note.end_sec < note.start_sec) {
			std::swap(note.start_sec, note.end_sec);
		}
	}

	std::sort(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
		if (lhs.start_sec != rhs.start_sec) return lhs.start_sec < rhs.start_sec;
		if (lhs.track != rhs.track) return lhs.track < rhs.track;
		if (lhs.channel != rhs.channel) return lhs.channel < rhs.channel;
		return lhs.note < rhs.note;
	});

	cache = {};
	cache.source_path = path;
	cache.tempos = std::move(tempos);
	cache.time_signatures = std::move(time_signatures);
	cache.notes = std::move(notes);
	cache.track_count = static_cast<int>(track_count);
	cache.division = division;
	cache.loaded = true;
	cache.max_tick = max_tick;
	cache.duration_sec = tick_to_seconds(cache.tempos, division, max_tick);
	cache.timelines.resize(track_count);

	for (int track = 0; track < cache.track_count; ++track) {
		for (int channel = 0; channel < 16; ++channel) {
			cache.timelines[track][channel].expression = convert_points(parse_states[track][channel].expression_points, cache.tempos, division);
			cache.timelines[track][channel].bend = convert_points(parse_states[track][channel].bend_points, cache.tempos, division);
		}
	}

	if (cache.notes.empty()) {
		cache.min_note = 60;
		cache.max_note = 72;
	} else {
		for (const auto& note : cache.notes) {
			cache.min_note = std::min(cache.min_note, note.note);
			cache.max_note = std::max(cache.max_note, note.note);
			cache.max_note_duration_sec = std::max(cache.max_note_duration_sec, note.end_sec - note.start_sec);
			cache.duration_sec = std::max(cache.duration_sec, note.end_sec);
		}
	}

	build_beat_markers(cache.beats, cache.time_signatures, cache.tempos, cache.division, cache.max_tick);

	return true;
}

bool ensure_midi_loaded(const std::wstring& path, std::string& error) {
	if (path.empty()) {
		error = "MIDI file path is empty";
		return false;
	}
	if (g_cache.loaded && g_cache.source_path == path) {
		return true;
	}
	return parse_midi_file(path, g_cache, error);
}

bool ensure_cache_available(SCRIPT_MODULE_PARAM* param) {
	if (g_cache.loaded) return true;
	param->set_error("MIDI がまだ読み込まれていません");
	log_error(L"MIDI cache is not loaded");
	return false;
}

void push_empty_array(SCRIPT_MODULE_PARAM* param) {
	double dummy = 0.0;
	param->push_result_array_double(&dummy, 0);
}

std::pair<std::vector<NoteEvent>::const_iterator, std::vector<NoteEvent>::const_iterator> find_visible_notes(double window_start, double window_end) {
	const double search_start = window_start - g_cache.max_note_duration_sec;
	const auto begin = std::lower_bound(g_cache.notes.begin(), g_cache.notes.end(), search_start, [](const NoteEvent& note, double time) {
		return note.start_sec < time;
	});
	const auto end = std::upper_bound(g_cache.notes.begin(), g_cache.notes.end(), window_end, [](double time, const NoteEvent& note) {
		return time < note.start_sec;
	});
	return { begin, end };
}

void load_midi(SCRIPT_MODULE_PARAM* param) {
	if (param->get_param_num() != 1) {
		param->set_error("load_midi(path) は1引数です");
		return;
	}
	const auto path_utf8 = param->get_param_string(0);
	if (!path_utf8 || !path_utf8[0]) {
		param->set_error("MIDI ファイルパスが空です");
		return;
	}

	std::string error;
	if (!ensure_midi_loaded(utf8_to_wide(path_utf8), error)) {
		set_last_error(error);
		param->set_error(g_last_error.c_str());
		log_error(utf8_to_wide(g_last_error).c_str());
		return;
	}

	set_last_error({});
	g_limit_warned = false;
	param->push_result_boolean(true);
}

void get_info(SCRIPT_MODULE_PARAM* param) {
	if (!ensure_cache_available(param)) return;

	LPCSTR keys[] = {
		"duration",
		"track_count",
		"channel_count",
		"note_count",
		"min_note",
		"max_note",
		"max_note_duration",
	};
	double values[] = {
		g_cache.duration_sec,
		static_cast<double>(g_cache.track_count),
		16.0,
		static_cast<double>(g_cache.notes.size()),
		static_cast<double>(g_cache.min_note),
		static_cast<double>(g_cache.max_note),
		g_cache.max_note_duration_sec,
	};
	param->push_result_table_double(keys, values, static_cast<int>(std::size(keys)));
}

void get_visible_segments(SCRIPT_MODULE_PARAM* param) {
	if (!ensure_cache_available(param)) return;
	if (param->get_param_num() < 3) {
		param->set_error("get_visible_segments(time,before,after[,limit]) の引数が不足しています");
		return;
	}

	const double current_time = param->get_param_double(0);
	const double before = std::max(0.0, param->get_param_double(1));
	const double after = std::max(0.0, param->get_param_double(2));
	int limit = (param->get_param_num() >= 4) ? param->get_param_int(3) : 4096;
	if (limit <= 0) limit = 1;

	const double window_start = current_time - before;
	const double window_end = current_time + after;
	const auto [begin, end] = find_visible_notes(window_start, window_end);

	std::vector<double> result;
	result.reserve(static_cast<size_t>(limit) * 8);
	for (auto it = begin; it != end; ++it) {
		if (it->end_sec <= window_start || it->start_sec >= window_end) continue;

		const auto& timeline = g_cache.timelines[it->track][it->channel];
		const bool active = it->start_sec <= current_time && current_time <= it->end_sec;
		const double alpha = active
			? (static_cast<double>(it->velocity) / 127.0) * (sample_timeline(timeline.expression, current_time, 127.0) / 127.0)
			: it->attack_alpha;

		const double segment_start = std::max(it->start_sec, window_start);
		const double segment_end = std::min(it->end_sec, window_end);
		double cursor = segment_start;
		double bend_value = sample_timeline(timeline.bend, cursor, 0.0);

		auto bend_it = std::upper_bound(timeline.bend.begin(), timeline.bend.end(), cursor, [](double value, const TimeValuePoint& point) {
			return value < point.time;
		});
		while (bend_it != timeline.bend.end() && bend_it->time < segment_end) {
			if (bend_it->time > cursor) {
				result.push_back(cursor);
				result.push_back(bend_it->time);
				result.push_back(static_cast<double>(it->note) + bend_value);
				result.push_back(static_cast<double>(it->channel));
				result.push_back(alpha);
				result.push_back(it->start_sec);
				result.push_back(it->end_sec);
				result.push_back(static_cast<double>(it->note));
				if (static_cast<int>(result.size() / 8) >= limit) break;
			}
			cursor = bend_it->time;
			bend_value = bend_it->value;
			++bend_it;
		}
		if (static_cast<int>(result.size() / 8) >= limit) {
			if (!g_limit_warned) {
				g_limit_warned = true;
				log_warn(L"visible segment limit reached");
			}
			break;
		}
		if (segment_end > cursor) {
			result.push_back(cursor);
			result.push_back(segment_end);
			result.push_back(static_cast<double>(it->note) + bend_value);
			result.push_back(static_cast<double>(it->channel));
			result.push_back(alpha);
			result.push_back(it->start_sec);
			result.push_back(it->end_sec);
			result.push_back(static_cast<double>(it->note));
			if (static_cast<int>(result.size() / 8) >= limit) {
				if (!g_limit_warned) {
					g_limit_warned = true;
					log_warn(L"visible segment limit reached");
				}
				break;
			}
		}
	}

	if (result.empty()) {
		push_empty_array(param);
		return;
	}
	param->push_result_array_double(result.data(), static_cast<int>(result.size()));
}

void get_visible_beats(SCRIPT_MODULE_PARAM* param) {
	if (!ensure_cache_available(param)) return;
	if (param->get_param_num() < 3) {
		param->set_error("get_visible_beats(time,before,after[,limit]) の引数が不足しています");
		return;
	}

	const double current_time = param->get_param_double(0);
	const double before = std::max(0.0, param->get_param_double(1));
	const double after = std::max(0.0, param->get_param_double(2));
	int limit = (param->get_param_num() >= 4) ? param->get_param_int(3) : 2048;
	if (limit <= 0) limit = 1;
	const double window_start = current_time - before;
	const double window_end = current_time + after;

	std::vector<double> result;
	result.reserve(static_cast<size_t>(limit) * 2);
	const auto begin = std::lower_bound(g_cache.beats.begin(), g_cache.beats.end(), window_start, [](const BeatMarker& beat, double time) {
		return beat.time < time;
	});
	for (auto it = begin; it != g_cache.beats.end() && it->time <= window_end; ++it) {
		result.push_back(it->time);
		result.push_back(static_cast<double>(it->beat_in_bar));
		if (static_cast<int>(result.size() / 2) >= limit) break;
	}

	if (result.empty()) {
		push_empty_array(param);
		return;
	}
	param->push_result_array_double(result.data(), static_cast<int>(result.size()));
}

void get_recent_active_notes(SCRIPT_MODULE_PARAM* param) {
	if (!ensure_cache_available(param)) return;
	if (param->get_param_num() < 2) {
		param->set_error("get_recent_active_notes(time,duration_ms[,limit]) の引数が不足しています");
		return;
	}

	const double current_time = param->get_param_double(0);
	const double duration_ms = std::max(0.0, param->get_param_double(1));
	const double duration_sec = duration_ms / 1000.0;
	int limit = (param->get_param_num() >= 3) ? param->get_param_int(2) : 1024;
	if (limit <= 0) limit = 1;

	const auto begin = std::lower_bound(g_cache.notes.begin(), g_cache.notes.end(), current_time - duration_sec, [](const NoteEvent& note, double time) {
		return note.start_sec < time;
	});

	std::vector<double> result;
	result.reserve(static_cast<size_t>(limit) * 4);
	for (auto it = begin; it != g_cache.notes.end() && it->start_sec <= current_time; ++it) {
		if (!(it->start_sec <= current_time && current_time <= it->end_sec)) continue;

		const double elapsed_ms = (current_time - it->start_sec) * 1000.0;
		if (elapsed_ms < 0.0 || elapsed_ms > duration_ms) continue;

		const auto& timeline = g_cache.timelines[it->track][it->channel];
		const double bend = sample_timeline(timeline.bend, current_time, 0.0);
		result.push_back(static_cast<double>(it->note) + bend);
		result.push_back(static_cast<double>(it->channel));
		result.push_back(elapsed_ms);
		result.push_back(static_cast<double>(it->note));
		if (static_cast<int>(result.size() / 4) >= limit) break;
	}

	if (result.empty()) {
		push_empty_array(param);
		return;
	}
	param->push_result_array_double(result.data(), static_cast<int>(result.size()));
}

void get_last_error(SCRIPT_MODULE_PARAM* param) {
	param->push_result_string(g_last_error.c_str());
}

SCRIPT_MODULE_FUNCTION functions[] = {
	{ L"load_midi", load_midi },
	{ L"get_info", get_info },
	{ L"get_visible_segments", get_visible_segments },
	{ L"get_visible_notes", get_visible_segments },
	{ L"get_visible_beats", get_visible_beats },
	{ L"get_recent_active_notes", get_recent_active_notes },
	{ L"get_last_error", get_last_error },
	{ nullptr }
};

SCRIPT_MODULE_TABLE script_module_table = {
	L"AviUtl2 MIDI Visualizer 3D script module (" AMV3D_VERSION L")",
	functions
};

} // namespace

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD) {
	return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	g_logger = handle;
}

EXTERN_C __declspec(dllexport) SCRIPT_MODULE_TABLE* GetScriptModuleTable(void) {
	return &script_module_table;
}
