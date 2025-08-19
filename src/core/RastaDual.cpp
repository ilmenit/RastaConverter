// For performance reasons (inlining etc.) we keep everythign in one big RastaConverter class
#include "rasta.h"
#include "Program.h"
#include "Evaluator.h"
#include "TargetPicture.h"
#include "debug_log.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdio>

// Externs and forward declarations from other translation units
extern const char *program_version;
extern OnOffMap on_off;
extern int solutions;
extern bool quiet;
unsigned char ConvertColorRegisterToRawData(e_target t);

void RastaConverter::ShowLastCreatedPictureDual()
{
#ifdef _DEBUG
	assert(cfg.dual_mode && "ShowLastCreatedPictureDual called while dual_mode is off");
#endif
	if (!cfg.dual_mode) { ShowLastCreatedPicture(); return; }

	// Ensure bitmaps exist
	if (!output_bitmap_A) output_bitmap_A = FreeImage_Allocate(cfg.width, cfg.height, 24);
	if (!output_bitmap_B) output_bitmap_B = FreeImage_Allocate(cfg.width, cfg.height, 24);
	if (!output_bitmap_blended) output_bitmap_blended = FreeImage_Allocate(cfg.width, cfg.height, 24);

	// Render A and B outputs from created pictures
	for (int y = 0; y < m_height; ++y)
	{
		for (int x = 0; x < m_width; ++x)
		{
			unsigned char idxA = (y < (int)m_eval_gstate.m_created_picture.size() && x < (int)m_eval_gstate.m_created_picture[y].size()) ? m_eval_gstate.m_created_picture[y][x] : 0;
			unsigned char idxB = (y < (int)m_created_picture_B.size() && x < (int)m_created_picture_B[y].size()) ? m_created_picture_B[y][x] : 0;
			rgb colA = atari_palette[idxA];
			rgb colB = atari_palette[idxB];
			RGBQUAD qa = { colA.b, colA.g, colA.r, 0 };
			RGBQUAD qb = { colB.b, colB.g, colB.r, 0 };
			FreeImage_SetPixelColor(output_bitmap_A, x, y, &qa);
			FreeImage_SetPixelColor(output_bitmap_B, x, y, &qb);

			// Blended: use precomputed pair sRGB if available; else simple 50/50 rgb
			RGBQUAD qm;
			if (m_dual_tables_ready && !m_pair_srgb.empty()) {
				size_t pairIdx = ((size_t)idxA * 128 + (size_t)idxB) * 3;
#ifdef _DEBUG
				assert(pairIdx + 2 < m_pair_srgb.size());
#endif
				qm.rgbRed   = m_pair_srgb[pairIdx + 0];
				qm.rgbGreen = m_pair_srgb[pairIdx + 1];
				qm.rgbBlue  = m_pair_srgb[pairIdx + 2];
			} else {
				qm.rgbRed   = (unsigned char)((colA.r + colB.r) >> 1);
				qm.rgbGreen = (unsigned char)((colA.g + colB.g) >> 1);
				qm.rgbBlue  = (unsigned char)((colA.b + colB.b) >> 1);
			}
			FreeImage_SetPixelColor(output_bitmap_blended, x, y, &qm);
		}
	}

	// Display according to current dual display mode
	int w = FreeImage_GetWidth(input_bitmap);
	switch (m_dual_display) {
		case DualDisplayMode::A: gui.DisplayBitmap(w, 0, output_bitmap_A); break;
		case DualDisplayMode::B: gui.DisplayBitmap(w, 0, output_bitmap_B); break;
		case DualDisplayMode::MIX: default: gui.DisplayBitmap(w, 0, output_bitmap_blended); break;
	}
}

void RastaConverter::PrecomputeDualTables()
{
#ifdef _DEBUG
	assert(cfg.dual_mode);
#endif
	if (m_dual_tables_ready) return;
	// Palette
	auto rgb_to_yuv = [](const rgb& c, float& y, float& u, float& v){
		float r=(float)c.r, g=(float)c.g, b=(float)c.b;
		y = 0.299f*r + 0.587f*g + 0.114f*b;
		u = (b - y) * 0.565f;
		v = (r - y) * 0.713f;
	};
	for (int i=0;i<128;++i) {
		float y,u,v; rgb_to_yuv(atari_palette[i], y,u,v);
		m_palette_y[i]=y; m_palette_u[i]=u; m_palette_v[i]=v;
	}
	// Target YUV per pixel
	const unsigned total = (unsigned)(m_width*m_height);
	m_target_y.resize(total); m_target_u.resize(total); m_target_v.resize(total);
	m_target_y8.resize(total); m_target_u8.resize(total); m_target_v8.resize(total);
	unsigned idx=0;
	for (int y=0;y<m_height;++y) {
		const screen_line& row = m_picture[y];
		for (int x=0;x<m_width;++x) {
			float Y,U,V; rgb_to_yuv(row[x], Y,U,V);
			m_target_y[idx]=Y; m_target_u[idx]=U; m_target_v[idx]=V; ++idx;
		}
	}
	// Pair YUV averages
	m_pair_Ysum.resize(128*128); m_pair_Usum.resize(128*128); m_pair_Vsum.resize(128*128);
	m_pair_Ydiff.resize(128*128); m_pair_Udiff.resize(128*128); m_pair_Vdiff.resize(128*128);
	m_pair_Ysum8.resize(128*128); m_pair_Usum8.resize(128*128); m_pair_Vsum8.resize(128*128);
	m_pair_Ydiff8.resize(128*128); m_pair_Udiff8.resize(128*128); m_pair_Vdiff8.resize(128*128);
	for (int a=0;a<128;++a) {
		for (int b=0;b<128;++b) {
			int p=(a<<7)|b;
			m_pair_Ysum[p] = 0.5f*(m_palette_y[a] + m_palette_y[b]);
			m_pair_Usum[p] = 0.5f*(m_palette_u[a] + m_palette_u[b]);
			m_pair_Vsum[p] = 0.5f*(m_palette_v[a] + m_palette_v[b]);
			m_pair_Ydiff[p] = fabsf(m_palette_y[a] - m_palette_y[b]);
			m_pair_Udiff[p] = fabsf(m_palette_u[a] - m_palette_u[b]);
			m_pair_Vdiff[p] = fabsf(m_palette_v[a] - m_palette_v[b]);
			auto q8 = [](float v, float offset, float scale)->unsigned char{
				float t = (v + offset) * scale; if (t < 0.0f) t = 0.0f; if (t > 255.0f) t = 255.0f; return (unsigned char)(t + 0.5f);
			};
			// Y is 0..255 already; U ~ [-144,144], V ~ [-182,182] roughly for Atari palette; use conservative ranges
			m_pair_Ysum8[p] = q8(m_pair_Ysum[p], 0.0f, 1.0f);
			m_pair_Usum8[p] = q8(m_pair_Usum[p], 160.0f, 1.0f);
			m_pair_Vsum8[p] = q8(m_pair_Vsum[p], 200.0f, 1.0f);
			m_pair_Ydiff8[p] = q8(m_pair_Ydiff[p], 0.0f, 1.0f);
			m_pair_Udiff8[p] = q8(m_pair_Udiff[p], 0.0f, 1.0f);
			m_pair_Vdiff8[p] = q8(m_pair_Vdiff[p], 0.0f, 1.0f);
		}
	}
	// Quantize target YUV to 8-bit with same shifts
	for (unsigned i=0;i<total;++i) {
		auto q8 = [](float v, float offset, float scale)->unsigned char{
			float t = (v + offset) * scale; if (t < 0.0f) t = 0.0f; if (t > 255.0f) t = 255.0f; return (unsigned char)(t + 0.5f);
		};
		m_target_y8[i] = q8(m_target_y[i], 0.0f, 1.0f);
		m_target_u8[i] = q8(m_target_u[i], 160.0f, 1.0f);
		m_target_v8[i] = q8(m_target_v[i], 200.0f, 1.0f);
	}
	// Blended sRGB pair table for output (3 bytes per pair)
	m_pair_srgb.resize(128*128*3);
	for (int a=0;a<128;++a) {
		for (int b=0;b<128;++b) {
			int p=(a<<7)|b; int off = p*3;
			if (cfg.dual_blending == "rgb") {
				// average in sRGB
				m_pair_srgb[off+0] = (unsigned char)((atari_palette[a].r + atari_palette[b].r)>>1);
				m_pair_srgb[off+1] = (unsigned char)((atari_palette[a].g + atari_palette[b].g)>>1);
				m_pair_srgb[off+2] = (unsigned char)((atari_palette[a].b + atari_palette[b].b)>>1);
			} else {
				// yuv blend -> convert back to sRGB with simple inverse (approx)
				float Y = m_pair_Ysum[p], U = m_pair_Usum[p], V = m_pair_Vsum[p];
				float R = Y + 1.403f*V;
				float B = Y + 1.773f*U;
				float G = (Y - 0.299f*R - 0.114f*B)/0.587f;
				auto clamp255 = [](float v)->unsigned char{ if (v<0) v=0; if (v>255) v=255; return (unsigned char)(v+0.5f); };
				m_pair_srgb[off+0] = clamp255(R);
				m_pair_srgb[off+1] = clamp255(G);
				m_pair_srgb[off+2] = clamp255(B);
			}
		}
	}
	m_dual_tables_ready = true;
}

void RastaConverter::UpdateCreatedFromResults(const std::vector<const line_cache_result*>& results,
	std::vector< std::vector<unsigned char> >& out_created)
{
	out_created.resize(m_height);
	for (int y=0;y<m_height;++y) {
		const line_cache_result* lr = results[y];
		if (lr && lr->color_row) {
			out_created[y].assign(lr->color_row, lr->color_row + m_width);
		} else {
			out_created[y].assign(m_width, 0);
		}
	}
}

void RastaConverter::UpdateTargetsFromResults(const std::vector<const line_cache_result*>& results,
	std::vector< std::vector<unsigned char> >& out_targets)
{
	out_targets.resize(m_height);
	for (int y=0;y<m_height;++y) {
		const line_cache_result* lr = results[y];
		if (lr && lr->target_row) {
			out_targets[y].assign(lr->target_row, lr->target_row + m_width);
		} else {
			out_targets[y].assign(m_width, (unsigned char)E_COLBAK);
		}
	}
}

bool RastaConverter::SaveScreenDataFromTargets(const char *filename, const std::vector< std::vector<unsigned char> >& targets)
{
	int x,y,a=0,b=0,c=0,d=0;
	FILE *fp=fopen(filename,"wb+");
	if (!fp) return false;
	for(y=0;y<m_height;++y)
	{
		for (x=0;x<m_width;x+=4)
		{
			unsigned char pix=0;
			a=ConvertColorRegisterToRawData((e_target)targets[y][x]);
			b=ConvertColorRegisterToRawData((e_target)targets[y][x+1]);
			c=ConvertColorRegisterToRawData((e_target)targets[y][x+2]);
			d=ConvertColorRegisterToRawData((e_target)targets[y][x+3]);
			pix |= a<<6; pix |= b<<4; pix |= c<<2; pix |= d;
			fwrite(&pix,1,1,fp);
		}
	}
	fclose(fp);
	return true;
}

void RastaConverter::SavePMGWithSprites(std::string name, const sprites_memory_t& sprites)
{
	size_t sprite,y,bit; unsigned char b;
	FILE *fp=fopen(name.c_str(),"wt+"); if (!fp) return;
	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"missiles\n");
	fprintf(fp,"\t.ds $100\n");
	for(sprite=0;sprite<4;++sprite)
	{
		fprintf(fp,"player%zu\n",sprite);
		fprintf(fp,"\t.he 00 00 00 00 00 00 00 00");
		for (y=0;y<240;++y)
		{
			b=0; for (bit=0;bit<8;++bit) { b|=(sprites[y][sprite][bit])<<(7-bit); }
			fprintf(fp," %02X",b);
			if (y%16==7) fprintf(fp,"\n\t.he");
		}
		fprintf(fp," 00 00 00 00 00 00 00 00\n");
	}
	fclose(fp);
}

void RastaConverter::MainLoopDual()
{
	Message("Dual-mode optimization started.");
	DBG_PRINT("[RASTA] MainLoopDual: start");

	// Mark optimization start time for statistics (seconds since start)
	m_eval_gstate.m_time_start = time(NULL);

	// Critical initialization that was missing!
	FindPossibleColors();
	Init();
	
	PrecomputeDualTables();
	DBG_PRINT("[RASTA] MainLoopDual: tables ready");

	// Dedicated evaluator for preview/initial calculations during bootstrap
	Evaluator bootstrapEval; bootstrapEval.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, cfg.initial_seed+101, cfg.cache_size);

	// Bootstrap A using single-frame evaluation for first_dual_steps
	raster_picture bestA = m_eval_gstate.m_best_pic;
	std::vector<const line_cache_result*> resultsA(m_height, nullptr);
	DBG_PRINT("[RASTA] Bootstrap A: calling ExecuteRasterProgram ...");
	bootstrapEval.RecachePicture(&bestA);
	distance_accum_t bestCostA = bootstrapEval.ExecuteRasterProgram(&bestA, resultsA.data());
	DBG_PRINT("[RASTA] Bootstrap A initial cost=%g", (double)bestCostA);
	{
		std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
		bestA.uncache_insns();
		m_eval_gstate.m_best_pic = bestA;
		m_eval_gstate.m_best_result = (double)bestCostA;
		UpdateCreatedFromResults(resultsA, m_eval_gstate.m_created_picture);
		UpdateTargetsFromResults(resultsA, m_eval_gstate.m_created_picture_targets);
		memcpy(&m_eval_gstate.m_sprites_memory, &bootstrapEval.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
		m_eval_gstate.m_initialized = true;
		m_eval_gstate.m_update_initialized = true;
		m_eval_gstate.m_condvar_update.notify_one();
	}

	// Multi-threaded bootstrap for A
	const unsigned long long targetE_A = m_eval_gstate.m_evaluations + cfg.first_dual_steps;
	int boot_threads = std::max(1, cfg.threads);
	std::vector<std::thread> bootWorkersA;
	bootWorkersA.reserve(boot_threads);
	for (int tid = 0; tid < boot_threads; ++tid) {
		bootWorkersA.emplace_back([this, tid, targetE_A, &bestA, &bestCostA]() {
			Evaluator& ev = m_evaluators[tid];
			std::vector<const line_cache_result*> line_results(m_height, nullptr);
			raster_picture localBest = bestA;
			distance_accum_t localCost = bestCostA;
			while (true) {
				// Early stop check (lock free read)
				if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_A) break;
				raster_picture cand = localBest;
				ev.MutateRasterProgram(&cand);
				distance_accum_t cost = ev.ExecuteRasterProgram(&cand, line_results.data());
				bool improved_flag = false;
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_A) break;
					++m_eval_gstate.m_evaluations;
					if (cost < bestCostA) {
						bestCostA = cost; localBest = cand; cand.uncache_insns();
						m_eval_gstate.m_best_pic = cand;
						m_eval_gstate.m_best_result = (double)bestCostA;
						improved_flag = true;
						// Throttle heavy copies to reduce overhead during bootstrap
						if ((m_eval_gstate.m_evaluations & 0x7FFULL) == 0ULL) {
							UpdateCreatedFromResults(line_results, m_eval_gstate.m_created_picture);
							UpdateTargetsFromResults(line_results, m_eval_gstate.m_created_picture_targets);
							memcpy(&m_eval_gstate.m_sprites_memory, &ev.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
							m_eval_gstate.m_update_improvement = true;
							m_eval_gstate.m_condvar_update.notify_one();
						}
					}
					if (m_eval_gstate.m_save_period > 0 && (m_eval_gstate.m_evaluations % (unsigned long long)m_eval_gstate.m_save_period) == 0ULL) {
						m_eval_gstate.m_update_autosave = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
				}
				// Skip mutation stats aggregation in dual mode bootstrap
			}
		});
	}

	// UI loop for bootstrap A
	clock_t last_rate_check_time = clock();
	unsigned long long last_eval = m_eval_gstate.m_evaluations;
	if (!quiet) { m_dual_display = DualDisplayMode::A; }
	while (!m_eval_gstate.m_finished && m_eval_gstate.m_evaluations < targetE_A) {
		if (!quiet) {
			switch (gui.NextFrame()) {
				case GUI_command::SAVE: SaveBestSolution(); break;
				case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
				case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
				case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); break;
				default: break;
			}
		}
		clock_t next_rate_check_time = clock();
		if (next_rate_check_time > last_rate_check_time + CLOCKS_PER_SEC / 4) {
			double clock_delta = (double)(next_rate_check_time - last_rate_check_time);
			m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) * (double)CLOCKS_PER_SEC / clock_delta;
			last_rate_check_time = next_rate_check_time;
			last_eval = m_eval_gstate.m_evaluations;
			if (cfg.save_period == -1) {
				using namespace std::literals::chrono_literals;
				auto now = std::chrono::steady_clock::now();
				if ( now - m_previous_save_time > 30s ) { m_previous_save_time = now; SaveBestSolution(); }
			}
			else if (m_eval_gstate.m_update_autosave) { m_eval_gstate.m_update_autosave = false; SaveBestSolution(); }
			// Periodic preview refresh of A for GUI
			if (!quiet) {
				raster_picture previewA;
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					previewA = m_eval_gstate.m_best_pic;
				}
				bootstrapEval.RecachePicture(&previewA);
				std::vector<const line_cache_result*> tickResultsA(m_height, nullptr);
				(void)bootstrapEval.ExecuteRasterProgram(&previewA, tickResultsA.data());
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					UpdateCreatedFromResults(tickResultsA, m_eval_gstate.m_created_picture);
					UpdateTargetsFromResults(tickResultsA, m_eval_gstate.m_created_picture_targets);
					memcpy(&m_eval_gstate.m_sprites_memory, &bootstrapEval.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
					m_eval_gstate.m_update_improvement = true;
					m_eval_gstate.m_condvar_update.notify_one();
				}
				ShowLastCreatedPictureDual();
			}
			if (!quiet) ShowMutationStats();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	for (auto &t : bootWorkersA) { if (t.joinable()) t.join(); }

	// Bootstrap B
	if (cfg.after_dual_steps == "copy") {
		// Copy the latest best A into B
		{
			std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
			m_best_pic_B = m_eval_gstate.m_best_pic;
		}
		m_best_pic_B.uncache_insns(); m_genB++; // copy program
		m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
		m_created_picture_B = m_eval_gstate.m_created_picture; // copy created for seed
		m_created_picture_targets_B = m_eval_gstate.m_created_picture_targets; // copy targets
		memcpy(&m_sprites_memory_B, &m_eval_gstate.m_sprites_memory, sizeof m_sprites_memory_B); // copy sprites
		// REMOVED: Old snapshot system - using efficient fixed frame system in alternation phase
	} else {
		// fresh random init for B and run single-frame for first_dual_steps evaluations
		m_best_pic_B = raster_picture(m_height);
		CreateRandomRasterPicture(&m_best_pic_B);
		std::vector<const line_cache_result*> resultsB(m_height, nullptr);
		DBG_PRINT("[RASTA] Bootstrap B: calling ExecuteRasterProgram ...");
		bootstrapEval.RecachePicture(&m_best_pic_B);
		distance_accum_t bestCostB = bootstrapEval.ExecuteRasterProgram(&m_best_pic_B, resultsB.data());
		DBG_PRINT("[RASTA] Bootstrap B initial cost=%g", (double)bestCostB);
		UpdateCreatedFromResults(resultsB, m_created_picture_B);
		UpdateTargetsFromResults(resultsB, m_created_picture_targets_B);
		memcpy(&m_sprites_memory_B, &bootstrapEval.GetSpritesMemory(), sizeof m_sprites_memory_B);

		// Multi-threaded bootstrap for B using the same evaluators
		const unsigned long long targetE_B = m_eval_gstate.m_evaluations + cfg.first_dual_steps;
		std::vector<std::thread> bootWorkersB;
		bootWorkersB.reserve(boot_threads);
		for (int tid = 0; tid < boot_threads; ++tid) {
			bootWorkersB.emplace_back([this, tid, targetE_B, &bestCostB]() {
				Evaluator& ev = m_evaluators[tid];
				std::vector<const line_cache_result*> line_results(m_height, nullptr);
				raster_picture localB = m_best_pic_B;
				while (true) {
					if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_B) break;
					raster_picture cand = localB;
					ev.MutateRasterProgram(&cand);
					distance_accum_t cost = ev.ExecuteRasterProgram(&cand, line_results.data());
					bool improved_flag = false;
					{
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						if (m_eval_gstate.m_finished || m_eval_gstate.m_evaluations >= targetE_B) break;
						++m_eval_gstate.m_evaluations;
						if (cost < bestCostB) {
							bestCostB = cost;
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							localB = cand;
							m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
							improved_flag = true;
							// Throttle heavy copies to reduce overhead during bootstrap
							if ((m_eval_gstate.m_evaluations & 0x7FFULL) == 0ULL) {
								UpdateCreatedFromResults(line_results, m_created_picture_B);
								UpdateTargetsFromResults(line_results, m_created_picture_targets_B);
								memcpy(&m_sprites_memory_B, &ev.GetSpritesMemory(), sizeof m_sprites_memory_B);
								m_eval_gstate.m_update_improvement = true;
								m_eval_gstate.m_condvar_update.notify_one();
							}
						}
						if (m_eval_gstate.m_save_period > 0 && (m_eval_gstate.m_evaluations % (unsigned long long)m_eval_gstate.m_save_period) == 0ULL) {
							m_eval_gstate.m_update_autosave = true;
							m_eval_gstate.m_condvar_update.notify_one();
						}
					}
					// Skip mutation stats aggregation in dual mode bootstrap
				}
			});
		}

		// UI loop for bootstrap B
		last_rate_check_time = clock();
		last_eval = m_eval_gstate.m_evaluations;
		if (!quiet) { m_dual_display = DualDisplayMode::B; }
		while (!m_eval_gstate.m_finished && m_eval_gstate.m_evaluations < targetE_B) {
			if (!quiet) {
				switch (gui.NextFrame()) {
					case GUI_command::SAVE: SaveBestSolution(); break;
					case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
					case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
					case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
					case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
					case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); break;
					default: break;
				}
			}
			clock_t next_rate_check_time = clock();
			if (next_rate_check_time > last_rate_check_time + CLOCKS_PER_SEC / 4) {
				double clock_delta = (double)(next_rate_check_time - last_rate_check_time);
				m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) * (double)CLOCKS_PER_SEC / clock_delta;
				last_rate_check_time = next_rate_check_time;
				last_eval = m_eval_gstate.m_evaluations;
				if (cfg.save_period == -1) {
					using namespace std::literals::chrono_literals;
					auto now = std::chrono::steady_clock::now();
					if ( now - m_previous_save_time > 30s ) { m_previous_save_time = now; SaveBestSolution(); }
				}
				else if (m_eval_gstate.m_update_autosave) { m_eval_gstate.m_update_autosave = false; SaveBestSolution(); }
				// Periodic preview refresh of B for GUI
				if (!quiet) {
					raster_picture previewB;
					{
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						previewB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
					}
					bootstrapEval.RecachePicture(&previewB);
					std::vector<const line_cache_result*> tickResultsB(m_height, nullptr);
					(void)bootstrapEval.ExecuteRasterProgram(&previewB, tickResultsB.data());
					{
						std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
						UpdateCreatedFromResults(tickResultsB, m_created_picture_B);
						UpdateTargetsFromResults(tickResultsB, m_created_picture_targets_B);
						memcpy(&m_sprites_memory_B, &bootstrapEval.GetSpritesMemory(), sizeof m_sprites_memory_B);
						m_eval_gstate.m_update_improvement = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
					ShowLastCreatedPictureDual();
				}
				if (!quiet) ShowMutationStats();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		for (auto &t : bootWorkersB) { if (t.joinable()) t.join(); }
		// OPTIMAL: Initialize fixed frame pointer buffers for alternating phase (no data copies)
		m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
		m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
		for (int i = 0; i < 2; ++i) {
			m_eval_gstate.m_dual_fixed_rows_buf[i].resize(m_height);
		}
		// Initialize B as the initial fixed frame (since we start with mutateB=false)
		int init_idx = 0;
		for (int y = 0; y < m_height; ++y) {
			if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
				// Wire pointer directly to existing B created picture row
				m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_created_picture_B[y].data();
			} else {
				// Fallback storage to ensure a valid pointer
				m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
				m_eval_gstate.m_dual_fixed_rows_buf[init_idx][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
			}
		}
		m_eval_gstate.m_dual_fixed_rows_active_index.store(init_idx, std::memory_order_release);
		m_eval_gstate.m_dual_fixed_frame_is_A.store(false, std::memory_order_relaxed); // B is initially fixed
	}

	// Alternating Stage - Calculate initial baseline cost before workers start
	DBG_PRINT("[RASTA] Alternating: calculating initial baseline cost");
	{
		Evaluator baselineEval;
		baselineEval.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), 
					  cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, 
					  cfg.initial_seed + 9999, cfg.cache_size);
		baselineEval.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
						  m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
						  m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
						  m_target_y.data(), m_target_u.data(), m_target_v.data());
		baselineEval.SetDualTables8(
			m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
			m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
			m_target_y8.data(), m_target_u8.data(), m_target_v8.data());
		baselineEval.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);

		// Ensure fixed-frame pointer buffer is initialized for baseline cost (B fixed initially)
		if (m_eval_gstate.m_dual_fixed_rows_buf[0].empty()) {
			m_eval_gstate.m_dual_fixed_rows_buf[0].resize(m_height);
		}
		for (int y = 0; y < m_height; ++y) {
			if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
				m_eval_gstate.m_dual_fixed_rows_buf[0][y] = m_created_picture_B[y].data();
			} else {
				if ((int)m_eval_gstate.m_dual_fixed_frame_B.size() != m_height) {
					m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
				}
				m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
				m_eval_gstate.m_dual_fixed_rows_buf[0][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
			}
		}
		m_eval_gstate.m_dual_fixed_rows_active_index.store(0, std::memory_order_release);
		const auto& other_rows_init = m_eval_gstate.m_dual_fixed_rows_buf[0];
		
		std::vector<const line_cache_result*> line_results_init(m_height, nullptr);
		raster_picture tmpA = m_eval_gstate.m_best_pic;
		distance_accum_t baseCost = baselineEval.ExecuteRasterProgramDual(&tmpA, line_results_init.data(), other_rows_init, false);
		
		DBG_PRINT("[RASTA] Alternating: initial blended cost=%g", (double)baseCost);
		std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
		m_eval_gstate.m_best_result = (double)baseCost;
		m_eval_gstate.m_update_improvement = true;
		m_eval_gstate.m_condvar_update.notify_one();
	}

	// Immediately show initial frame to ensure output is visible in dual mode
	if (!quiet) { m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); }

	// Loop until finished/max_evals using worker threads and snapshots
	const unsigned long long E0 = m_eval_gstate.m_evaluations;
	std::vector<std::thread> workers;
	int num_workers = std::max(1, cfg.threads);
	workers.reserve(num_workers);

	// Initialize atomic stage coordination for alternation
	m_eval_gstate.m_dual_stage_focus_B.store(false, std::memory_order_relaxed);
	m_eval_gstate.m_dual_stage_counter.store(0, std::memory_order_relaxed);

	for (int tid = 0; tid < num_workers; ++tid) {
		workers.emplace_back([this, E0, tid]() {
			// HIGH-PERFORMANCE WORKER THREAD - Minimal critical sections like fork
			Evaluator ev;
			ev.Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, cfg.initial_seed + 4242ULL + (unsigned long long)tid * 133ULL, cfg.cache_size, tid);
			ev.SetDualTables(m_palette_y, m_palette_u, m_palette_v,
					 m_pair_Ysum.data(), m_pair_Usum.data(), m_pair_Vsum.data(),
					 m_pair_Ydiff.data(), m_pair_Udiff.data(), m_pair_Vdiff.data(),
					 m_target_y.data(), m_target_u.data(), m_target_v.data());
			ev.SetDualTables8(
				m_pair_Ysum8.data(), m_pair_Usum8.data(), m_pair_Vsum8.data(),
				m_pair_Ydiff8.data(), m_pair_Udiff8.data(), m_pair_Vdiff8.data(),
				m_target_y8.data(), m_target_u8.data(), m_target_v8.data());
			ev.SetDualTemporalWeights((float)cfg.dual_luma, (float)cfg.dual_chroma);

			// Local working state for this thread (NO sharing between threads)
			std::vector<const line_cache_result*> line_results(m_height, nullptr);
			
			// Local copies of current best programs (avoid lock contention)
			raster_picture currentA = m_eval_gstate.m_best_pic;
			raster_picture currentB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;

			// Track local accepted cost for proper acceptance logic (like fork)
			double localAcceptedCost = m_eval_gstate.m_best_result;
			
			// Track current phase to detect switches for simple fixed frame snapshots
			bool local_mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);

			while (true) {
				// STAGE 1: Simple atomic stage coordination 
				bool mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);
				unsigned long long stage_counter = m_eval_gstate.m_dual_stage_counter.fetch_add(1, std::memory_order_relaxed) + 1;
				
				// Detect phase switch and update fixed frame snapshots
				if (stage_counter >= (unsigned long long)cfg.altering_dual_steps) {
					// Phase switch triggered - coordinate globally using exchange so only one flips
					if (m_eval_gstate.m_dual_stage_counter.exchange(0, std::memory_order_relaxed) 
						>= (unsigned long long)cfg.altering_dual_steps) {
						m_eval_gstate.m_dual_stage_focus_B.store(!mutateB, std::memory_order_relaxed);
					}
				}

				// Quick re-read after potential update
				mutateB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);

				// If phase switched, update local state and fixed frame snapshot
				if (local_mutateB != mutateB) {
					std::unique_lock<std::mutex> sync_lock{ m_eval_gstate.m_mutex };
					
					// Sync local copies with shared state
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						currentA = m_eval_gstate.m_best_pic;
						currentB = m_best_pic_B;
						localAcceptedCost = m_eval_gstate.m_best_result;
					}
					
					// SIMPLE: Update the pointer array for the fixed frame using double buffering
					std::unique_lock<std::mutex> fixed_lock{ m_eval_gstate.m_dual_fixed_frame_mutex };
					m_eval_gstate.m_dual_fixed_frame_is_A.store(mutateB, std::memory_order_relaxed); // If mutating B, A is fixed

					int next_idx = 1 - m_eval_gstate.m_dual_fixed_rows_active_index.load(std::memory_order_acquire);
					if ((int)m_eval_gstate.m_dual_fixed_rows_buf[next_idx].size() != m_height) {
						m_eval_gstate.m_dual_fixed_rows_buf[next_idx].resize(m_height);
					}
					if (mutateB) {
						// B will be mutated, so A is fixed - wire pointers to current A rows
						if ((int)m_eval_gstate.m_dual_fixed_frame_A.size() != m_height) {
							m_eval_gstate.m_dual_fixed_frame_A.resize(m_height);
						}
						for (int y = 0; y < m_height; ++y) {
							if (y < (int)m_eval_gstate.m_created_picture.size() && !m_eval_gstate.m_created_picture[y].empty()) {
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_eval_gstate.m_created_picture[y].data();
							} else {
								m_eval_gstate.m_dual_fixed_frame_A[y].assign(m_width, 0);
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_eval_gstate.m_dual_fixed_frame_A[y].data();
							}
						}
					} else {
						// A will be mutated, so B is fixed - wire pointers to current B rows
						if ((int)m_eval_gstate.m_dual_fixed_frame_B.size() != m_height) {
							m_eval_gstate.m_dual_fixed_frame_B.resize(m_height);
						}
						for (int y = 0; y < m_height; ++y) {
							if (y < (int)m_created_picture_B.size() && !m_created_picture_B[y].empty()) {
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_created_picture_B[y].data();
							} else {
								m_eval_gstate.m_dual_fixed_frame_B[y].assign(m_width, 0);
								m_eval_gstate.m_dual_fixed_rows_buf[next_idx][y] = m_eval_gstate.m_dual_fixed_frame_B[y].data();
							}
						}
					}
					m_eval_gstate.m_dual_fixed_rows_active_index.store(next_idx, std::memory_order_release);
					
					local_mutateB = mutateB;
				}

				// STAGE 2: Lock-free quick sync using a version check to avoid per-iteration lock
				if (m_eval_gstate.m_best_result < localAcceptedCost) {
					std::unique_lock<std::mutex> sync_lock{ m_eval_gstate.m_mutex };
					if (m_eval_gstate.m_best_result < localAcceptedCost) {
						currentA = m_eval_gstate.m_best_pic;
						currentB = m_best_pic_B;
						localAcceptedCost = m_eval_gstate.m_best_result;
					}
				}
				
				raster_picture cand = mutateB ? currentB : currentA;

				// STAGE 3: ZERO-COPY access to fixed frame - lock-free pointer array snapshot
				int read_idx = m_eval_gstate.m_dual_fixed_rows_active_index.load(std::memory_order_acquire);
				const auto& other_rows = m_eval_gstate.m_dual_fixed_rows_buf[read_idx];

				// STAGE 4: Heavy computation outside locks
				ev.MutateRasterProgram(&cand);
				distance_accum_t cost = ev.ExecuteRasterProgramDual(&cand, line_results.data(), other_rows, mutateB);

				// STAGE 5: MINIMAL critical section for shared state updates only
				{
					std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex };
					
					// Early termination check
					if (m_eval_gstate.m_finished || (cfg.max_evals > 0 && m_eval_gstate.m_evaluations >= m_eval_gstate.m_max_evals)) {
						break;
					}
					
					++m_eval_gstate.m_evaluations;
					
					// Proper acceptance logic (not just improvements) - matches fork pattern
					bool accept = (cost <= localAcceptedCost); // For now, use simple acceptance (can be enhanced later)
					bool improved = (cost < m_eval_gstate.m_best_result);
					
					if (accept) {
						localAcceptedCost = cost; // Update local accepted cost
					}
					
					if (improved) {
						// OPTIMAL: Update only the mutated frame (keep fixed frame frozen during phase)
						if (mutateB) {
							m_best_pic_B = cand; m_best_pic_B.uncache_insns();
							currentB = cand; // Update local copy
							m_genB++;
							m_eval_gstate.m_dual_generation_B.fetch_add(1, std::memory_order_acq_rel);
							UpdateCreatedFromResults(line_results, m_created_picture_B);
							UpdateTargetsFromResults(line_results, m_created_picture_targets_B);
							memcpy(&m_sprites_memory_B, &ev.GetSpritesMemory(), sizeof m_sprites_memory_B);
							// NO snapshot update during phase - frame A stays fixed!
						} else {
							m_eval_gstate.m_best_pic = cand; m_eval_gstate.m_best_pic.uncache_insns();
							currentA = cand; // Update local copy
							m_genA++;
							m_eval_gstate.m_dual_generation_A.fetch_add(1, std::memory_order_acq_rel);
							UpdateCreatedFromResults(line_results, m_eval_gstate.m_created_picture);
							UpdateTargetsFromResults(line_results, m_eval_gstate.m_created_picture_targets);
							memcpy(&m_eval_gstate.m_sprites_memory, &ev.GetSpritesMemory(), sizeof m_eval_gstate.m_sprites_memory);
							// NO snapshot update during phase - frame B stays fixed!
						}
						// Skip mutation stats aggregation in dual alternation per request
						m_eval_gstate.m_best_result = cost;
						m_eval_gstate.m_update_improvement = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
					
					// Check autosave and termination
					if (m_eval_gstate.m_save_period > 0 && (m_eval_gstate.m_evaluations % (unsigned long long)m_eval_gstate.m_save_period) == 0ULL) {
						m_eval_gstate.m_update_autosave = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
					if (cfg.max_evals > 0 && m_eval_gstate.m_evaluations >= m_eval_gstate.m_max_evals) {
						m_eval_gstate.m_finished = true;
						m_eval_gstate.m_condvar_update.notify_one();
					}
				}
			}
		});
	}

	// UI loop while workers progress
	while (!m_eval_gstate.m_finished && (cfg.max_evals == 0 || m_eval_gstate.m_evaluations < m_eval_gstate.m_max_evals)) {
		// UI update
		if (!quiet) {
			switch (gui.NextFrame()) {
				case GUI_command::SAVE: SaveBestSolution(); break;
				case GUI_command::STOP: m_eval_gstate.m_finished = true; break;
				case GUI_command::SHOW_A: m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_B: m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); break;
				case GUI_command::SHOW_MIX: m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); break;
				case GUI_command::REDRAW: ShowInputBitmap(); ShowLastCreatedPictureDual(); ShowMutationStats(); break;
				default: if (m_dual_display == DualDisplayMode::MIX) ShowLastCreatedPictureDual(); break;
			}
		}

		// Periodic stats/UI similar to single-frame
		clock_t next_rate_check_time = clock();
		if (next_rate_check_time > last_rate_check_time + CLOCKS_PER_SEC / 4) {
			double clock_delta = (double)(next_rate_check_time - last_rate_check_time);
			m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) * (double)CLOCKS_PER_SEC / clock_delta;
			last_rate_check_time = next_rate_check_time;
			last_eval = m_eval_gstate.m_evaluations;
			if (cfg.save_period == -1) {
				using namespace std::literals::chrono_literals;
				auto now = std::chrono::steady_clock::now();
				if ( now - m_previous_save_time > 30s ) { m_previous_save_time = now; SaveBestSolution(); }
			} else if (m_eval_gstate.m_update_autosave) {
				m_eval_gstate.m_update_autosave = false;
				SaveBestSolution();
			}
			if (!quiet) ShowMutationStats();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}

	for (auto &t : workers) { if (t.joinable()) t.join(); }
}


