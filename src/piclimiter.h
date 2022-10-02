#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#if defined( WIN32 ) || defined( WIN64 )
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif


typedef struct
{
	FILE *pf_in;
	FILE *pf_out;
	int32_t i_don;
	int32_t i_timescale;
	int32_t i_picture_duration;
	int64_t i64_video_buffer_size_bits;
	int32_t i_verbose;

#define PICLIMITER_MODE_YUV  1
#define PICLIMITER_MODE_HEVC 2
	int32_t i_mode;

	/* shared */
	int32_t i_target_picture_history_count;


	/* hevc start */
	int32_t i_current_picture_history_count;
#define PICLIMITER_MAX_PICTURE_HISTORY_COUNT 128
	int32_t rgi_picture_history_size[ PICLIMITER_MAX_PICTURE_HISTORY_COUNT ];

	// eh, 16Mb should be enough
#define PICLIMIER_MAX_BUFFER_SIZE ( 1 << 24 )
	int32_t i_buffer_size_bytes;
	int32_t i_buffer_fill_bytes;
	uint8_t rgui8_buffer[ PICLIMIER_MAX_BUFFER_SIZE ];
	bool b_synced;
	/* hevc stop */

	/* yuv start */
	int32_t i_picture_buffer_size;
	int32_t i_buffer_size_pictures;
	int32_t i_buffer_fill_pictures;
#define PICLIMITER_MAX_PICTURE_BUFFER_SIZE 10
	uint8_t *rgpui8_picturebuffer[ PICLIMITER_MAX_PICTURE_BUFFER_SIZE ];
	/* yuv stop */

	int64_t i64_last_pic_duration_jitter;
	uint16_t ui16_last_pic_ms;
} piclimiter_t;

