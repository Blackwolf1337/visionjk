#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# Opus, the codec of the voice chat (codemp/cgame/cg_voice.c). Built as a static relocatable library and linked
# into the cgame, which is the only user.
#
# The bundled copy comes from the opus 1.6.1 release (opus-1.6.1.tar.gz, SHA256
# 6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1, as on opus-codec.org/downloads). It holds what
# a build in plain C with floating point needs: the sources named in CELT_SOURCES, SILK_SOURCES,
# SILK_SOURCES_FLOAT, OPUS_SOURCES and OPUS_SOURCES_FLOAT of the .mk files of the release, the headers of their
# directories and include/. Left out: dnn/ (23 MB of weights for features that are off), the fixed point
# build, the assembler and intrinsics for x86, ARM and MIPS, tests, demos and the build systems of the release.
# To update: take the same set from a newer release. No file of it is changed.

set(OpusDir "${CMAKE_CURRENT_SOURCE_DIR}")

set(OpusSources
	"${OpusDir}/celt/bands.c"
	"${OpusDir}/celt/celt.c"
	"${OpusDir}/celt/celt_encoder.c"
	"${OpusDir}/celt/celt_decoder.c"
	"${OpusDir}/celt/cwrs.c"
	"${OpusDir}/celt/entcode.c"
	"${OpusDir}/celt/entdec.c"
	"${OpusDir}/celt/entenc.c"
	"${OpusDir}/celt/kiss_fft.c"
	"${OpusDir}/celt/laplace.c"
	"${OpusDir}/celt/mathops.c"
	"${OpusDir}/celt/mdct.c"
	"${OpusDir}/celt/modes.c"
	"${OpusDir}/celt/pitch.c"
	"${OpusDir}/celt/celt_lpc.c"
	"${OpusDir}/celt/quant_bands.c"
	"${OpusDir}/celt/rate.c"
	"${OpusDir}/celt/vq.c"
	"${OpusDir}/silk/CNG.c"
	"${OpusDir}/silk/code_signs.c"
	"${OpusDir}/silk/init_decoder.c"
	"${OpusDir}/silk/decode_core.c"
	"${OpusDir}/silk/decode_frame.c"
	"${OpusDir}/silk/decode_parameters.c"
	"${OpusDir}/silk/decode_indices.c"
	"${OpusDir}/silk/decode_pulses.c"
	"${OpusDir}/silk/decoder_set_fs.c"
	"${OpusDir}/silk/dec_API.c"
	"${OpusDir}/silk/enc_API.c"
	"${OpusDir}/silk/encode_indices.c"
	"${OpusDir}/silk/encode_pulses.c"
	"${OpusDir}/silk/gain_quant.c"
	"${OpusDir}/silk/interpolate.c"
	"${OpusDir}/silk/LP_variable_cutoff.c"
	"${OpusDir}/silk/NLSF_decode.c"
	"${OpusDir}/silk/NSQ.c"
	"${OpusDir}/silk/NSQ_del_dec.c"
	"${OpusDir}/silk/PLC.c"
	"${OpusDir}/silk/shell_coder.c"
	"${OpusDir}/silk/tables_gain.c"
	"${OpusDir}/silk/tables_LTP.c"
	"${OpusDir}/silk/tables_NLSF_CB_NB_MB.c"
	"${OpusDir}/silk/tables_NLSF_CB_WB.c"
	"${OpusDir}/silk/tables_other.c"
	"${OpusDir}/silk/tables_pitch_lag.c"
	"${OpusDir}/silk/tables_pulses_per_block.c"
	"${OpusDir}/silk/VAD.c"
	"${OpusDir}/silk/control_audio_bandwidth.c"
	"${OpusDir}/silk/quant_LTP_gains.c"
	"${OpusDir}/silk/VQ_WMat_EC.c"
	"${OpusDir}/silk/HP_variable_cutoff.c"
	"${OpusDir}/silk/NLSF_encode.c"
	"${OpusDir}/silk/NLSF_VQ.c"
	"${OpusDir}/silk/NLSF_unpack.c"
	"${OpusDir}/silk/NLSF_del_dec_quant.c"
	"${OpusDir}/silk/process_NLSFs.c"
	"${OpusDir}/silk/stereo_LR_to_MS.c"
	"${OpusDir}/silk/stereo_MS_to_LR.c"
	"${OpusDir}/silk/check_control_input.c"
	"${OpusDir}/silk/control_SNR.c"
	"${OpusDir}/silk/init_encoder.c"
	"${OpusDir}/silk/control_codec.c"
	"${OpusDir}/silk/A2NLSF.c"
	"${OpusDir}/silk/ana_filt_bank_1.c"
	"${OpusDir}/silk/biquad_alt.c"
	"${OpusDir}/silk/bwexpander_32.c"
	"${OpusDir}/silk/bwexpander.c"
	"${OpusDir}/silk/debug.c"
	"${OpusDir}/silk/decode_pitch.c"
	"${OpusDir}/silk/inner_prod_aligned.c"
	"${OpusDir}/silk/lin2log.c"
	"${OpusDir}/silk/log2lin.c"
	"${OpusDir}/silk/LPC_analysis_filter.c"
	"${OpusDir}/silk/LPC_inv_pred_gain.c"
	"${OpusDir}/silk/table_LSF_cos.c"
	"${OpusDir}/silk/NLSF2A.c"
	"${OpusDir}/silk/NLSF_stabilize.c"
	"${OpusDir}/silk/NLSF_VQ_weights_laroia.c"
	"${OpusDir}/silk/pitch_est_tables.c"
	"${OpusDir}/silk/resampler.c"
	"${OpusDir}/silk/resampler_down2_3.c"
	"${OpusDir}/silk/resampler_down2.c"
	"${OpusDir}/silk/resampler_private_AR2.c"
	"${OpusDir}/silk/resampler_private_down_FIR.c"
	"${OpusDir}/silk/resampler_private_IIR_FIR.c"
	"${OpusDir}/silk/resampler_private_up2_HQ.c"
	"${OpusDir}/silk/resampler_rom.c"
	"${OpusDir}/silk/sigm_Q15.c"
	"${OpusDir}/silk/sort.c"
	"${OpusDir}/silk/sum_sqr_shift.c"
	"${OpusDir}/silk/stereo_decode_pred.c"
	"${OpusDir}/silk/stereo_encode_pred.c"
	"${OpusDir}/silk/stereo_find_predictor.c"
	"${OpusDir}/silk/stereo_quant_pred.c"
	"${OpusDir}/silk/LPC_fit.c"
	"${OpusDir}/silk/float/apply_sine_window_FLP.c"
	"${OpusDir}/silk/float/corrMatrix_FLP.c"
	"${OpusDir}/silk/float/encode_frame_FLP.c"
	"${OpusDir}/silk/float/find_LPC_FLP.c"
	"${OpusDir}/silk/float/find_LTP_FLP.c"
	"${OpusDir}/silk/float/find_pitch_lags_FLP.c"
	"${OpusDir}/silk/float/find_pred_coefs_FLP.c"
	"${OpusDir}/silk/float/LPC_analysis_filter_FLP.c"
	"${OpusDir}/silk/float/LTP_analysis_filter_FLP.c"
	"${OpusDir}/silk/float/LTP_scale_ctrl_FLP.c"
	"${OpusDir}/silk/float/noise_shape_analysis_FLP.c"
	"${OpusDir}/silk/float/process_gains_FLP.c"
	"${OpusDir}/silk/float/regularize_correlations_FLP.c"
	"${OpusDir}/silk/float/residual_energy_FLP.c"
	"${OpusDir}/silk/float/warped_autocorrelation_FLP.c"
	"${OpusDir}/silk/float/wrappers_FLP.c"
	"${OpusDir}/silk/float/autocorrelation_FLP.c"
	"${OpusDir}/silk/float/burg_modified_FLP.c"
	"${OpusDir}/silk/float/bwexpander_FLP.c"
	"${OpusDir}/silk/float/energy_FLP.c"
	"${OpusDir}/silk/float/inner_product_FLP.c"
	"${OpusDir}/silk/float/k2a_FLP.c"
	"${OpusDir}/silk/float/LPC_inv_pred_gain_FLP.c"
	"${OpusDir}/silk/float/pitch_analysis_core_FLP.c"
	"${OpusDir}/silk/float/scale_copy_vector_FLP.c"
	"${OpusDir}/silk/float/scale_vector_FLP.c"
	"${OpusDir}/silk/float/schur_FLP.c"
	"${OpusDir}/silk/float/sort_FLP.c"
	"${OpusDir}/src/opus.c"
	"${OpusDir}/src/opus_decoder.c"
	"${OpusDir}/src/opus_encoder.c"
	"${OpusDir}/src/extensions.c"
	"${OpusDir}/src/opus_multistream.c"
	"${OpusDir}/src/opus_multistream_encoder.c"
	"${OpusDir}/src/opus_multistream_decoder.c"
	"${OpusDir}/src/repacketizer.c"
	"${OpusDir}/src/opus_projection_encoder.c"
	"${OpusDir}/src/opus_projection_decoder.c"
	"${OpusDir}/src/mapping_matrix.c"
	"${OpusDir}/src/analysis.c"
	"${OpusDir}/src/mlp.c"
	"${OpusDir}/src/mlp_data.c"
)

add_library(bundled_opus STATIC ${OpusSources})
set_target_properties(bundled_opus PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
target_include_directories(bundled_opus PUBLIC "${OpusDir}/include")
target_include_directories(bundled_opus PRIVATE "${OpusDir}" "${OpusDir}/celt" "${OpusDir}/silk" "${OpusDir}/silk/float")

# What config.h of the release would say. Arrays on the stack: of variable length where the compiler has them,
# else alloca. The checks that are cheap enough for a release stay on (ENABLE_HARDENING): a frame that would
# break the decoder ends the program there and does not go on with broken memory.
target_compile_definitions(bundled_opus PRIVATE OPUS_BUILD HAVE_LRINT HAVE_LRINTF ENABLE_HARDENING)
if(MSVC)
	target_compile_definitions(bundled_opus PRIVATE USE_ALLOCA _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_definitions(bundled_opus PRIVATE VAR_ARRAYS)
	# the warnings of the tree are for its own code
	target_compile_options(bundled_opus PRIVATE -w)
endif()
